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
#include "ui/widgets/button.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/window.hpp"

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

        constexpr std::string_view kPaintWindowName = "paint_ground";
        constexpr std::string_view kPaintIcon = "Land_Brush_Paint";
        constexpr std::string_view kPaintTitle = "Land_Brush_PaintHelp";
        constexpr std::size_t kSwatchColumns = 7;
        constexpr float kSwatch = 40; ///< A surface's picture and the room round it
        constexpr std::string_view kBrushName = "Land_Brush_Radius";
        constexpr std::string_view kRoundName = "Rct3pp_BrushRound";
        constexpr float kBrushRow = 24; ///< Under the pictures: the brush's size and shape
        constexpr float kBrushNumber = 60;
        constexpr float kRoundWidth = 80;

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
            for (const park::PathSpot t : park::brush_tiles(*tile, l.brush, l.round, scene.grid.width, scene.grid.height))
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

        ui::Window* paint_window(const ParkScene& scene)
        {
            auto* window = static_cast<ui::Window*>(scene.desktop->root().find(kPaintWindowName));
            return window == nullptr || window->closing() ? nullptr : window;
        }

        void open_paint_window(ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            const LandTools& l = scene.land;
            const ui::WindowStyle style = ui::game_skins::window_style();
            const std::size_t rows = (l.surfaces.size() + kSwatchColumns - 1) / kSwatchColumns;
            const float width = style.content_inset.x0 + style.content_inset.x1 + kSwatchColumns * kSwatch;
            const float swatches = static_cast<float>(std::max<std::size_t>(rows, 1)) * kSwatch;
            const float height = style.content_inset.y0 + style.content_inset.y1 + swatches + kBrushRow;
            auto& window = scene.desktop->root().add<ui::Window>(std::string(kPaintWindowName), style, width, height);
            window.title = std::u16string(assets.strings->get(kPaintTitle));
            window.icon = kPaintIcon;
            window.placement = ui::top_left(16, 60, width, height);
            for (std::size_t i = 0; i < l.surfaces.size(); ++i)
            {
                const park::GroundSurface& s = l.surfaces[i];
                ui::Button& b = window.content().add<ui::Button>(s.icon, std::string(ui::game_skins::kMenuButtonSmall), s.icon);
                b.placement = ui::top_left(static_cast<float>(i % kSwatchColumns) * kSwatch, static_cast<float>(i / kSwatchColumns) * kSwatch, kSwatch, kSwatch);
                b.tooltip = std::u16string(assets.strings->get(s.help));
                b.toggle = true;
                b.on = i == l.surface;
            }
            ui::Spinner& brush = window.content().add<ui::Spinner>(std::string(kBrushName), ui::game_skins::spinner_style());
            brush.minimum = 1;
            brush.maximum = park::kLargestBrush;
            brush.set_value(l.brush);
            brush.tooltip = std::u16string(assets.strings->get("Land_Brush_RadiusHelp"));
            brush.placement = ui::top_left(0, swatches + 4, kBrushNumber, kBrushRow - 8);
            ui::Button& round = window.content().add<ui::Button>(std::string(kRoundName), std::string(ui::game_skins::kButton), std::string { });
            round.text = std::u16string(assets.strings->get(kRoundName));
            round.toggle = true;
            round.on = l.round;
            round.placement = ui::top_left(kBrushNumber + 8, swatches + 2, kRoundWidth, kBrushRow - 4);
        }

        /// Takes the surface picked in the window since the last frame, keeping only it pressed in.
        void take_pick(LandTools& l, ui::Window& window)
        {
            // The surfaces' buttons come first, then the brush's controls.
            const auto& buttons = window.content().children();
            const std::size_t swatches = std::min(buttons.size(), l.surfaces.size());
            for (std::size_t i = 0; i < swatches; ++i)
            {
                if (static_cast<ui::Button&>(*buttons[i]).take_click())
                    l.surface = i;
            }
            for (std::size_t i = 0; i < swatches; ++i)
                static_cast<ui::Button&>(*buttons[i]).on = i == l.surface;
            if (auto* brush = dynamic_cast<ui::Spinner*>(window.content().find(kBrushName)); brush != nullptr && brush->take_change())
                l.brush = brush->value();
            if (auto* round = dynamic_cast<ui::Button*>(window.content().find(kRoundName)); round != nullptr && round->take_click())
                l.round = round->on;
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
        {
            read_surfaces(scene.land, assets);
            open_paint_window(scene, assets);
        }
    }

    void stop_land_tools(ParkScene& scene)
    {
        LandTools& l = scene.land;
        if (ui::Window* window = paint_window(scene))
            window->close();
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
        if (l.tool == LandTools::Tool::Paint)
        {
            ui::Window* window = paint_window(scene);
            if (window == nullptr)
            {
                stop_land_tools(scene); // Closed by the player
                return;
            }
            take_pick(l, *window);
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
