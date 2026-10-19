// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/land_tools.hpp"

#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/footpath_builder.hpp"
#include "park/ground_brush.hpp"
#include "park/ride_placement.hpp"
#include "park/water_fill.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_scene.hpp"
#include "scene/ride_placing.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/land_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace rct3::scene
{

    namespace
    {
        // Prices, in hundredths: a step of one tile, and painting one tile.
        constexpr std::int32_t kStepCost = 100;
        constexpr std::int32_t kPaintCost = 50;
        constexpr std::int32_t kWaterCost = 100; ///< A step of a lake's level
        constexpr float kShapeCost = 25;         ///< A height unit of one corner point moved by a brush
        constexpr float kSmoothRate = 2.5f;      ///< Of the way to where a smoothing brush wants the ground, a second
        constexpr float kSmoothPass = 0.05f;     ///< Seconds between smoothing passes
        constexpr float kWaterStep = 1;          ///< Of a lake's level, in park units.

        constexpr std::string_view kPanelName = "land_panel";

        /// A tab's tools by their pictures, `usable` the first of them that work so far.
        ui::LandTab tab_of(const ui::StringTable& strings, std::string_view icon, int slot, std::initializer_list<std::string_view> tools, std::size_t usable)
        {
            ui::LandTab t { std::string(icon), std::u16string(strings.get(std::string(icon) + "Help")), slot, { } };
            for (const std::string_view tool : tools)
                t.tools.push_back({ std::string(tool), std::u16string(strings.get(std::string(tool) + "Help")), t.tools.size() < usable });
            return t;
        }

        bool affordable(const ParkScene& scene, std::int32_t cost)
        {
            return !scene.park.cash || *scene.park.cash >= cost;
        }

        void pay(ParkScene& scene, std::int32_t cost)
        {
            if (scene.park.cash)
                *scene.park.cash -= cost;
        }

        /// Takes the held tile one step up or down; false when it may not go.
        bool step(ParkScene& scene, bool up)
        {
            LandTools& l = scene.land;
            if (!affordable(scene, kStepCost))
                return false;
            const park::RideSite site { l.tile->col, l.tile->row, 0, park::shaped_height(scene.grid.at(l.tile->col, l.tile->row), up), { 1, 1 } };
            if (park::site_problem(site, scene.grid, scene.footpaths.paths, l.solids) != park::SiteProblem::None)
                return false;
            park::level_ground(scene.grid, site);
            pay(scene, kStepCost);
            l.steps += up ? 1 : -1;
            return true;
        }

        void shape(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (!l.tile)
            {
                l.tile = over ? std::nullopt : ground_tile(scene, at, size);
                if (!l.tile)
                    return;
                l.press_y = at.y;
                l.steps = 0;
                // The ground under a stroke cannot reach things standing on it, so they are only gathered once.
                l.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
            }
            const auto wanted = static_cast<int>(std::lround((l.press_y - at.y) / park::kSlopeDragStep));
            bool moved = false;
            while (l.steps != wanted && step(scene, wanted > l.steps))
                moved = true;
            if (moved)
                rebuild_ground(device, scene, assets);
        }

        /// The raising and lowering brushes, by their place in the panel's tabs.
        constexpr std::size_t kRaiseTab = 2;
        constexpr std::size_t kLowerTab = 3;
        constexpr std::size_t kSmoothTab = 1;
        constexpr std::size_t kCliffTab = 1; ///< Of painting

        /// The smoothing tab's tools, in its order, and where a levelling one levels to.
        enum class Level : std::uint8_t
        {
            None,
            Pointer, ///< The height under the pointer, as it moves
            Press,   ///< The height where the stroke began
            PressWhole,
        };
        struct SmoothTool
        {
            park::GroundSmoothing tool;
            Level level = Level::None;
        };
        constexpr std::array kSmoothTools {
            SmoothTool { park::GroundSmoothing::Average }, SmoothTool { park::GroundSmoothing::Random }, SmoothTool { park::GroundSmoothing::Level, Level::Pointer }, SmoothTool { park::GroundSmoothing::Level, Level::Press }, SmoothTool { park::GroundSmoothing::WholeUnits }, SmoothTool { park::GroundSmoothing::Level, Level::PressWhole }, SmoothTool { park::GroundSmoothing::JoinEdges }, SmoothTool { park::GroundSmoothing::TileEdges },
        };
        constexpr std::array kRaiseShapes { park::GroundShape::Ridge, park::GroundShape::Mountain, park::GroundShape::Mesa, park::GroundShape::Hill };
        constexpr std::array kLowerShapes { park::GroundShape::Canyon, park::GroundShape::Trough, park::GroundShape::Crater };

        /// Whether a brush may move the ground of tile (`c`, `r`): owned outright, without paths or things
        /// standing on it (the stroke's solids).
        bool shapeable(const ParkScene& scene, int c, int r)
        {
            return park::owned_outright(scene.grid.at(c, r)) && !park::blocked(scene.land.solids, { c, r }, -1e6f, 1e6f) && std::ranges::none_of(scene.footpaths.paths, [&](const save::PathTile& p) { return p.col == c && p.row == r; });
        }

        bool lowers(park::GroundShape shape)
        {
            return std::ranges::contains(kLowerShapes, shape);
        }

        void brush_shape(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (!l.tile)
            {
                const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
                if (!spot || !scene.grid.contains(static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y))))
                    return;
                l.tile = park::PathSpot { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                l.middle = *spot;
                l.press_y = at.y;
                l.amount = 0;
                l.paid_for = 0;
                // Across the view: the camera looks along (sin yaw, cos yaw) over the ground.
                l.along = { std::cos(scene.camera.yaw), -std::sin(scene.camera.yaw) };
                l.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
                l.before = park::corner_heights(scene.grid, [&](int c, int r) { return shapeable(scene, c, r); });
            }
            const float steps = (l.press_y - at.y) / park::kSlopeDragStep;
            const float amount = std::round(std::max(lowers(*l.shape) ? -steps : steps, 0.0f) * 4) / 4; // In quarter units
            if (amount == l.amount)
                return;
            save::TerrainGrid trial = scene.grid;
            const float moved = park::shape_ground(trial, l.before, *l.shape, l.middle, static_cast<float>(l.brush) / 2, l.along, amount);
            const auto cost = static_cast<std::int32_t>(std::lround(std::max(moved - l.paid_for, 0.0f) * kShapeCost));
            if (!affordable(scene, cost))
                return;
            scene.grid = std::move(trial);
            pay(scene, cost);
            l.paid_for = std::max(l.paid_for, moved);
            l.amount = amount;
            rebuild_ground(device, scene, assets);
        }

        void smooth(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, float elapsed)
        {
            LandTools& l = scene.land;
            const SmoothTool tool = kSmoothTools[*l.smoothing];
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            const float reach = static_cast<float>(l.brush) / 2;
            if (!l.tile)
            {
                if (!spot || !scene.grid.contains(static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y))))
                    return;
                l.tile = park::PathSpot { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                l.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
                l.held = kSmoothPass; // A click smooths once
                l.target = park::mean_height(scene.grid, *spot, std::max(reach, 1.0f));
                if (tool.level == Level::PressWhole)
                    l.target = std::round(l.target);
            }
            if (!spot)
                return;
            if (tool.level == Level::Pointer)
                l.target = park::mean_height(scene.grid, *spot, 1);
            l.held += elapsed;
            bool moved = false;
            for (; l.held >= kSmoothPass; l.held -= kSmoothPass)
            {
                save::TerrainGrid trial = scene.grid;
                const float amount = park::smooth_ground(trial, tool.tool, *spot, reach, kSmoothRate * kSmoothPass, l.target, ++l.passes, [&](int c, int r) { return shapeable(scene, c, r); });
                const auto cost = static_cast<std::int32_t>(std::lround(amount * kShapeCost));
                if (amount <= 0 || !affordable(scene, cost))
                    continue;
                scene.grid = std::move(trial);
                pay(scene, cost);
                moved = true;
            }
            if (moved)
                rebuild_ground(device, scene, assets);
        }

        void paint(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            const auto tile = over ? std::nullopt : ground_tile(scene, at, size);
            if (!tile || tile == l.tile || (l.cliff ? *l.cliff >= l.cliffs.size() : l.surface >= l.surfaces.size()))
                return;
            l.tile = tile;
            const std::uint8_t surface = l.cliff ? l.cliffs[*l.cliff].index : l.surfaces[l.surface].index;
            bool painted = false;
            for (const park::PathSpot t : park::brush_tiles(*tile, l.brush, true, scene.grid.width, scene.grid.height))
            {
                save::TerrainCell& cell = scene.grid.at(t.col, t.row);
                std::uint8_t& face = l.cliff ? cell.cliff : cell.surface;
                if (face == surface || !park::owned_outright(cell) || !affordable(scene, kPaintCost))
                    continue;
                face = surface;
                pay(scene, kPaintCost);
                painted = true;
            }
            if (painted)
                rebuild_ground(device, scene, assets);
        }

        /// Sets the water filled from the held tile to its level `steps` from the stroke's start; false when it
        /// may not go there.
        bool flood(ParkScene& scene, int steps)
        {
            LandTools& l = scene.land;
            if (!affordable(scene, kWaterCost))
                return false;
            const auto& paths = scene.footpaths.paths;
            const auto allowed = [&](int c, int r) { return park::owned_outright(scene.grid.at(c, r)) && std::ranges::none_of(paths, [&](const save::PathTile& p) { return p.col == c && p.row == r && !p.elevation; }); };
            const auto lake = park::fill_water(scene.grid, l.tile->col, l.tile->row, l.level_before + static_cast<float>(steps) * kWaterStep, allowed);
            if (!lake)
                return false;
            // The lakes the new water reaches, and the one it was poured in, become part of it.
            save::WaterLayer water = l.water_before;
            std::erase_if(water.bodies, [&](const save::WaterBody& b) { return park::holds(b, l.tile->col, l.tile->row) || std::ranges::any_of(lake->tris, [&](const save::WaterTri& t) { return park::holds(b, t.col, t.row); }); });
            if (!lake->tris.empty())
                water.bodies.push_back(*lake);
            scene.footpaths.water = std::move(water);
            pay(scene, kWaterCost);
            l.steps = steps;
            return true;
        }

        void pour(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (!l.tile)
            {
                l.tile = over ? std::nullopt : ground_tile(scene, at, size);
                if (!l.tile)
                    return;
                l.press_y = at.y;
                l.steps = 0;
                l.water_before = scene.footpaths.water;
                const auto& bodies = l.water_before.bodies;
                const auto lake = std::ranges::find_if(bodies, [&](const save::WaterBody& b) { return park::holds(b, l.tile->col, l.tile->row); });
                l.level_before = lake != bodies.end() ? lake->level : std::floor(scene.grid.at(l.tile->col, l.tile->row).min_height());
            }
            const auto wanted = static_cast<int>(std::lround((l.press_y - at.y) / park::kSlopeDragStep));
            bool moved = false;
            // One step at a time, so the water stops at the last level it could reach.
            while (l.steps != wanted && flood(scene, l.steps + (wanted > l.steps ? 1 : -1)))
                moved = true;
            if (moved)
                rebuild_ground(device, scene, assets);
        }

        void read_surfaces(LandTools& l, const ui::InterfaceAssets& assets)
        {
            if (std::exchange(l.surfaces_read, true) || !assets.install)
                return;
            for (const std::string_view name : park::kGroundFiles)
            {
                const auto found = assets.install->find(name);
                if (!found)
                    continue;
                for (const auto& [paint, into] : { std::pair { park::GroundPaint::Surface, &l.surfaces }, std::pair { park::GroundPaint::Cliff, &l.cliffs } })
                {
                    auto more = park::ground_surfaces(*found, paint);
                    if (!more)
                    {
                        logging::logger("park").warning("{}: {}", name, more.error());
                        break;
                    }
                    into->insert(into->end(), std::make_move_iterator(more->begin()), std::make_move_iterator(more->end()));
                }
            }
        }

        ui::LandPanel* panel_of(const ParkScene& scene)
        {
            return static_cast<ui::LandPanel*>(scene.desktop->root().find(kPanelName));
        }

        /// The panel with the tabs of the tool out: shaping's, or painting's with the surfaces read.
        void open_panel(ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            const LandTools& l = scene.land;
            const ui::StringTable& strings = *assets.strings;
            ui::LandPanel* panel = panel_of(scene);
            if (panel == nullptr)
                panel = &scene.desktop->root().add<ui::LandPanel>(std::string(kPanelName), ui::game_skins::land_panel_style(), strings);
            std::vector<ui::LandTab> tabs;
            ui::LandPick chosen;
            if (l.tool == LandTools::Tool::Paint)
            {
                ui::LandTab base { "Land_Brush_Paint_Base", std::u16string(strings.get("Land_Brush_Paint_BaseHelp")), 4, { } };
                for (const park::GroundSurface& s : l.surfaces)
                    base.tools.push_back({ s.icon, std::u16string(strings.get(s.help)), true });
                tabs.push_back(std::move(base));
                ui::LandTab cliff { "Land_Brush_Paint_Cliff", std::u16string(strings.get("Land_Brush_Paint_CliffHelp")), 5, { } };
                for (const park::GroundSurface& s : l.cliffs)
                    cliff.tools.push_back({ s.icon, std::u16string(strings.get(s.help)), true });
                tabs.push_back(std::move(cliff));
                chosen = l.cliff ? ui::LandPick { kCliffTab, *l.cliff } : ui::LandPick { 0, l.surface };
            }
            else
            {
                // Of the tile tools only the first works so far; the others show what is to come.
                tabs.push_back(tab_of(strings, "Land_Vertex", 2, { "Land_Vertex_Neighbour", "Land_Vertex_Neighbour2", "Land_Vertex_Freeform", "Land_Vertex_Freeform2", "Land_Vertex_Quantised1", "Land_Vertex_Quantised2" }, 1));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Smooth", 3, { "Land_Brush_Height_Smooth_Average", "Land_Brush_Height_Smooth_Random", "Land_Brush_Height_Smooth_FlatMove", "Land_Brush_Height_Smooth_Flat", "Land_Brush_Height_Smooth_Flat1", "Land_Brush_Height_Smooth_Flat2", "Land_Brush_Height_Smooth_Round", "Land_Brush_Height_Smooth_Square" }, kSmoothTools.size()));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Raise", 4, { "Land_Brush_Height_Raise_Ridge", "Land_Brush_Height_Raise_Mountain", "Land_Brush_Height_Raise_Mesa", "Land_Brush_Height_Raise_Hill" }, kRaiseShapes.size()));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Lower", 5, { "Land_Brush_Height_Lower_Canyon", "Land_Brush_Height_Lower_Ditch", "Land_Brush_Height_Lower_Crater" }, kLowerShapes.size()));
                if (l.smoothing)
                    chosen = { kSmoothTab, *l.smoothing };
                else if (l.shape)
                {
                    const auto raise = std::ranges::find(kRaiseShapes, *l.shape);
                    chosen = raise != kRaiseShapes.end() ? ui::LandPick { kRaiseTab, static_cast<std::size_t>(raise - kRaiseShapes.begin()) } : ui::LandPick { kLowerTab, static_cast<std::size_t>(std::ranges::find(kLowerShapes, *l.shape) - kLowerShapes.begin()) };
                }
            }
            panel->show(std::move(tabs), chosen, l.brush, park::kLargestBrush);
        }
    } // namespace

    void toggle_land_tool(ParkScene& scene, LandTools::Tool tool, const ui::InterfaceAssets& assets)
    {
        const bool out = scene.land.tool == tool;
        stop_land_tools(scene);
        if (out)
            return;
        scene.land.tool = tool;
        if (tool == LandTools::Tool::Paint)
            read_surfaces(scene.land, assets);
        if (tool != LandTools::Tool::Water)
            open_panel(scene, assets);
    }

    void stop_land_tools(ParkScene& scene)
    {
        LandTools& l = scene.land;
        if (ui::LandPanel* panel = panel_of(scene))
            panel->hide();
        l.tool = LandTools::Tool::None;
        l.tile.reset();
        l.solids = { };
        l.before = { };
        l.water_before = { };
    }

    bool land_tool(const ParkScene& scene)
    {
        return scene.land.tool != LandTools::Tool::None;
    }

    void update_land_tools(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, float elapsed)
    {
        LandTools& l = scene.land;
        if (ui::LandPanel* panel = panel_of(scene); panel != nullptr && (l.tool == LandTools::Tool::Shape || l.tool == LandTools::Tool::Paint))
        {
            if (panel->take_closed())
            {
                stop_land_tools(scene); // Closed by the player
                return;
            }
            l.brush = panel->brush();
            if (const auto pick = panel->take_pick(); pick && l.tool == LandTools::Tool::Paint)
            {
                l.tile.reset();
                if (pick->tab == kCliffTab)
                    l.cliff = pick->tool;
                else
                {
                    l.cliff.reset();
                    l.surface = pick->tool;
                }
            }
            else if (pick && l.tool == LandTools::Tool::Shape)
            {
                l.tile.reset();
                l.smoothing.reset();
                if (pick->tab == kSmoothTab && pick->tool < kSmoothTools.size())
                    l.smoothing = pick->tool;
                if (pick->tab == kRaiseTab && pick->tool < kRaiseShapes.size())
                    l.shape = kRaiseShapes[pick->tool];
                else if (pick->tab == kLowerTab && pick->tool < kLowerShapes.size())
                    l.shape = kLowerShapes[pick->tool];
                else
                    l.shape.reset();
            }
        }
        if (l.tool == LandTools::Tool::None || !down)
        {
            l.tile.reset();
            return;
        }
        if (l.tool == LandTools::Tool::Shape && l.smoothing)
            smooth(device, scene, assets, at, size, over, elapsed);
        else if (l.tool == LandTools::Tool::Shape && l.shape)
            brush_shape(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Shape)
            shape(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Paint)
            paint(device, scene, assets, at, size, over);
        else
            pour(device, scene, assets, at, size, over);
    }

} // namespace rct3::scene
