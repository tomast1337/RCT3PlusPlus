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
#include <cmath>
#include <string>

namespace rct3::scene
{

    namespace
    {
        // Our own prices, in hundredths: a step of one tile, and painting one tile.
        constexpr std::int32_t kStepCost = 100;
        constexpr std::int32_t kPaintCost = 50;
        constexpr std::int32_t kWaterCost = 100; ///< A step of a lake's level
        constexpr float kWaterStep = 1;          ///< Of a lake's level, in park units. Our own value.

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

        void paint(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            const auto tile = over ? std::nullopt : ground_tile(scene, at, size);
            if (!tile || tile == l.tile || l.surface >= l.surfaces.size())
                return;
            l.tile = tile;
            const std::uint8_t surface = l.surfaces[l.surface].index;
            bool painted = false;
            for (const park::PathSpot t : park::brush_tiles(*tile, l.brush, true, scene.grid.width, scene.grid.height))
            {
                save::TerrainCell& cell = scene.grid.at(t.col, t.row);
                if (cell.surface == surface || !park::owned_outright(cell) || !affordable(scene, kPaintCost))
                    continue;
                cell.surface = surface;
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
                auto more = park::ground_surfaces(*found);
                if (!more)
                {
                    logging::logger("park").warning("{}: {}", name, more.error());
                    continue;
                }
                l.surfaces.insert(l.surfaces.end(), std::make_move_iterator(more->begin()), std::make_move_iterator(more->end()));
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
                // Cliff faces are not painted yet.
                tabs.push_back(tab_of(strings, "Land_Brush_Paint_Cliff", 5, { "Land_Paint_Cliff0", "Land_Paint_Cliff1", "Land_Paint_Cliff2", "Land_Paint_Cliff3", "Land_Paint_Cliff4", "Land_Paint_Cliff5" }, 0));
                chosen = { 0, l.surface };
            }
            else
            {
                // Of the shaping tools only the tile one works so far; the others show what is to come.
                tabs.push_back(tab_of(strings, "Land_Vertex", 2, { "Land_Vertex_Neighbour", "Land_Vertex_Neighbour2", "Land_Vertex_Freeform", "Land_Vertex_Freeform2", "Land_Vertex_Quantised1", "Land_Vertex_Quantised2" }, 1));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Smooth", 3, { "Land_Brush_Height_Smooth_Average", "Land_Brush_Height_Smooth_Random", "Land_Brush_Height_Smooth_FlatMove", "Land_Brush_Height_Smooth_Flat", "Land_Brush_Height_Smooth_Flat1", "Land_Brush_Height_Smooth_Flat2", "Land_Brush_Height_Smooth_Round", "Land_Brush_Height_Smooth_Square" }, 0));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Raise", 4, { "Land_Brush_Height_Raise_Ridge", "Land_Brush_Height_Raise_Mountain", "Land_Brush_Height_Raise_Mesa", "Land_Brush_Height_Raise_Hill" }, 0));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Lower", 5, { "Land_Brush_Height_Lower_Canyon", "Land_Brush_Height_Lower_Ditch", "Land_Brush_Height_Lower_Crater" }, 0));
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
        l.water_before = { };
    }

    bool land_tool(const ParkScene& scene)
    {
        return scene.land.tool != LandTools::Tool::None;
    }

    void update_land_tools(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over)
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
            if (const auto pick = panel->take_pick(); pick && l.tool == LandTools::Tool::Paint && pick->tab == 0)
                l.surface = pick->tool;
        }
        if (l.tool == LandTools::Tool::None || !down)
        {
            l.tile.reset();
            return;
        }
        if (l.tool == LandTools::Tool::Shape)
            shape(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Paint)
            paint(device, scene, assets, at, size, over);
        else
            pour(device, scene, assets, at, size, over);
    }

} // namespace rct3::scene
