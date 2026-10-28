// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/land_tools.hpp"

#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/footpath_builder.hpp"
#include "park/ground_brush.hpp"
#include "park/object_pick.hpp"
#include "park/ride_placement.hpp"
#include "park/water_fill.hpp"
#include "park/waterfall_making.hpp"
#include "park/waterfall_mesh.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_refresh.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"
#include "simulation/import_park.hpp"
#include "simulation/park_messages.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/land_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace rct3::scene
{

    namespace
    {
        // Prices, in hundredths: painting one tile.
        constexpr std::int32_t kPaintCost = 50;
        constexpr float kShapeCost = 25;      ///< A height unit of one corner point moved by a brush
        constexpr float kSmoothRate = 2.5f;   ///< Of the way to where a smoothing brush wants the ground, a second
        constexpr float kSmoothPass = 0.05f;  ///< Seconds between smoothing passes
        constexpr float kRumbleQuiet = 0.25f; ///< Chosen by eye: the rumble of a shaping stroke while the ground stays still
        constexpr float kRumbleFade = 0.6f;   ///< Chosen by eye: seconds it takes to fall back to that once the ground stops moving

        constexpr std::string_view kPanelName = "land_panel";
        constexpr std::string_view kNoMoney = "PlaceObject_NotEnoughMoney";
        constexpr std::string_view kWaterRefused = "Rct3pp_WaterRefused";
        constexpr float kInFront = 0.01f; ///< Back along the pointer's ray from the ground it meets, in world units

        /// A tab's tools by their pictures, `usable` the first of them that work so far.
        ui::LandTab tab_of(const ui::StringTable& strings, std::string_view icon, int slot, std::initializer_list<std::string_view> tools, std::size_t usable)
        {
            ui::LandTab t { std::string(icon), std::u16string(strings.get(std::format("{}Help", icon))), slot, { } };
            for (const std::string_view tool : tools)
                t.tools.push_back({ std::string(tool), std::u16string(strings.get(std::format("{}Help", tool))), t.tools.size() < usable });
            return t;
        }

        bool affordable(const ParkScene& scene, std::int32_t cost)
        {
            return !scene.park.cash || *scene.park.cash >= cost;
        }

        /// Pays `cost` for work on `tile`, floating it up from there.
        void pay(ParkScene& scene, std::int32_t cost, save::Tile tile)
        {
            if (scene.park.cash)
                *scene.park.cash -= cost;
            float_money(scene, tile_point(scene.grid, tile), -cost);
        }

        /// The raising and lowering brushes, by their place in the panel's tabs.
        constexpr std::size_t kRaiseTab = 2;
        constexpr std::size_t kLowerTab = 3;
        constexpr std::size_t kSmoothTab = 1;
        constexpr std::size_t kCliffTab = 1; ///< Of painting
        constexpr std::size_t kTileTab = 0;
        /// The tile tools' pairs, in their tab's order.
        constexpr std::array kTileShapings { park::TileShaping::Snap, park::TileShaping::Free, park::TileShaping::Whole };
        constexpr glm::vec4 kMarkColour { 1.0f, 1.0f, 1.0f, 0.85f };

        /// The smoothing tab's tools, in its order, and where a levelling one levels to.
        enum class Level : std::uint8_t
        {
            None,
            Pointer, ///< The height under the pointer, as it moves
            Press,   ///< The height where the stroke began
        };
        struct SmoothTool
        {
            park::GroundSmoothing tool;
            Level level = Level::None;
        };
        constexpr std::array kSmoothTools {
            SmoothTool { park::GroundSmoothing::Average }, SmoothTool { park::GroundSmoothing::Level, Level::Pointer }, SmoothTool { park::GroundSmoothing::Level, Level::Press }, SmoothTool { park::GroundSmoothing::WholeUnits }, SmoothTool { park::GroundSmoothing::JoinEdges }, SmoothTool { park::GroundSmoothing::TileEdges },
        };
        constexpr std::array kRaiseShapes { park::GroundShape::Ridge, park::GroundShape::Mountain, park::GroundShape::Mesa, park::GroundShape::Hill };
        constexpr std::array kLowerShapes { park::GroundShape::Canyon, park::GroundShape::Trough, park::GroundShape::Crater };

        std::uint32_t tile_key(save::Tile t)
        {
            return static_cast<std::uint32_t>(t.row) << 16 | static_cast<std::uint16_t>(t.col);
        }

        /// Whether a tool may move the ground of `tile` at all this stroke: owned outright and not refused yet.
        bool shapeable(const ParkScene& scene, save::Tile tile)
        {
            return park::owned_outright(scene.grid.at(tile)) && !scene.land.refused.contains(tile_key(tile));
        }

        /// Gathers what stands on the ground as a stroke starts; the ground under a stroke cannot reach it.
        void start_stroke(ParkScene& scene)
        {
            LandTools& l = scene.land;
            l.holds = park::ground_holds(scene.objects, scene.models, scene.footpaths.paths, scene.grid);
            l.refused = l.holds.held;
            if (l.tool == LandTools::Tool::Shape)
                l.rumble = kRumbleQuiet;
        }

        /// Ends a stroke: seats the scenery standing on the ground it moved on its new height.
        void end_stroke(ParkScene& scene)
        {
            LandTools& l = scene.land;
            l.rumble.reset();
            if (!l.stirred)
                return;
            if (scene.object_build.valid())
                (void)scene.object_build.get(); // It reads the items being moved
            if (park::seat_followers(scene.objects, l.holds, scene.models, scene.grid, (*l.stirred)[0], (*l.stirred)[1]))
            {
                scene.levels = { };
                scene.since_rebuild = kObjectRebuildInterval;
            }
            l.stirred.reset();
        }

        /// Shapes a copy of the ground `from` with `shape` (which returns what it moved) again until what stands on it lets
        /// every tile it moved go, holding those it refuses for the rest of the stroke. Nothing when a tile refused
        /// still moved.
        template<typename Shape> std::optional<std::pair<save::TerrainGrid, float>> try_shape(ParkScene& scene, const save::TerrainGrid& from, Shape&& shape)
        {
            LandTools& l = scene.land;
            for (;;)
            {
                save::TerrainGrid trial = from;
                const float moved = shape(trial);
                bool again = false;
                for (int r = 0; r < trial.height; ++r)
                {
                    for (int c = 0; c < trial.width; ++c)
                    {
                        const auto& was = from.at(c, r).corners;
                        const auto& now = trial.at(c, r).corners;
                        if (was == now || !park::refuses(l.holds, { c, r }, was, now))
                            continue;
                        if (!l.refused.insert(tile_key({ c, r })).second)
                            return std::nullopt;
                        again = true;
                        // A brush's points round it stay where they are too.
                        if (l.before.columns == trial.width + 1)
                        {
                            for (int dr = 0; dr < 2; ++dr)
                                for (int dc = 0; dc < 2; ++dc)
                                    l.before.held[static_cast<std::size_t>(r + dr) * static_cast<std::size_t>(l.before.columns) + static_cast<std::size_t>(c + dc)] = true;
                        }
                    }
                }
                if (!again)
                    return std::pair { std::move(trial), moved };
            }
        }

        /// Takes on the shaped ground `trial`, the scenery on it following.
        void take_ground(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, save::TerrainGrid trial, bool drain = true)
        {
            LandTools& l = scene.land;
            // The meshes built beside the frame read the ground.
            if (scene.object_build.valid())
            {
                if (auto built = scene.object_build.get())
                    show_objects(device, scene, std::move(*built));
            }
            if (scene.people_build.valid())
                scene.people_build.wait();
            const save::TerrainGrid was = std::exchange(scene.grid, std::move(trial));
            std::optional<std::array<save::Tile, 2>> step;
            for (int r = 0; r < was.height; ++r)
                for (int c = 0; c < was.width; ++c)
                {
                    if (scene.grid.at(c, r).corners == was.at(c, r).corners)
                        continue;
                    if (!step)
                        step = std::array { save::Tile { c, r }, save::Tile { c, r } };
                    (*step)[0] = { std::min((*step)[0].col, c), std::min((*step)[0].row, r) };
                    (*step)[1] = { std::max((*step)[1].col, c), std::max((*step)[1].row, r) };
                }
            // Water the new ground may not hold goes, as do the waterfalls beside it.
            const bool drained = drain && park::drain_moved(scene.footpaths.water, scene.footpaths.waterfalls, was, scene.grid);
            if (drained)
                show_park_particles(scene.particles, park::waterfall_spray(scene.footpaths.waterfalls), &ParkParticles::sprays);
            // A drained lake may reach far from the step; a moved corner shades the tiles beside those round it.
            std::optional<park::TileRect> touched;
            if (step && !drained)
                touched = park::TileRect { (*step)[0].col, (*step)[0].row, (*step)[1].col + 1, (*step)[1].row + 1 }.grown(1);
            rebuild_ground(device, scene, assets, touched);
            if (!step)
                return;
            if (l.rumble)
                l.rumble = 1.0f;
            if (!l.stirred)
                l.stirred = step;
            (*l.stirred)[0] = { std::min((*l.stirred)[0].col, (*step)[0].col), std::min((*l.stirred)[0].row, (*step)[0].row) };
            (*l.stirred)[1] = { std::max((*l.stirred)[1].col, (*step)[1].col), std::max((*l.stirred)[1].row, (*step)[1].row) };
            // Items standing at the ground's height are drawn there once built again; the stroke's end seats them all.
            constexpr int kWidest = 4; // Tiles an item reaches from the one it stands on
            const auto near = [&](std::size_t i) {
                const save::PlacedObject& o = scene.objects[i];
                return o.col + kWidest >= (*step)[0].col && o.col - kWidest <= (*step)[1].col && o.row + kWidest >= (*step)[0].row && o.row - kWidest <= (*step)[1].row;
            };
            if (std::ranges::any_of(l.holds.followers, near))
            {
                scene.levels = { };
                scene.since_rebuild = kObjectRebuildInterval;
            }
        }

        /// The tiles a tile tool works on with the pointer at `spot`, from the first to the last, and the part.
        struct TileTarget
        {
            save::Tile first;
            save::Tile last;
            park::TilePart part = park::TilePart::Whole;
        };
        std::optional<TileTarget> tile_target(const ParkScene& scene, std::optional<glm::vec2> spot)
        {
            if (!spot)
                return std::nullopt;
            const save::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            if (!scene.grid.contains(tile.col, tile.row))
                return std::nullopt;
            // The first of each pair works on one tile, the second on the brush's square.
            if (scene.land.tile_tool % 2 == 0)
                return TileTarget { tile, tile, park::part_at({ spot->x - static_cast<float>(tile.col), spot->y - static_cast<float>(tile.row) }) };
            const auto tiles = park::brush_tiles(tile, scene.land.brush, false, scene.grid.width, scene.grid.height);
            TileTarget t { tile, tile };
            for (const save::Tile s : tiles)
            {
                t.first = { std::min(t.first.col, s.col), std::min(t.first.row, s.row) };
                t.last = { std::max(t.last.col, s.col), std::max(t.last.row, s.row) };
            }
            return t;
        }

        void shape(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (!l.tile)
            {
                const auto target = over ? std::nullopt : tile_target(scene, ground_spot(scene, at, size));
                if (!target)
                    return;
                l.tile = target->first;
                l.press_y = at.y;
                l.amount = 0;
                l.paid_for = 0;
                start_stroke(scene);
                l.stroke = park::begin_tile_stroke(scene.grid, target->first.col, target->first.row, target->last.col, target->last.row, target->part);
            }
            const float amount = std::round((l.press_y - at.y) / park::kSlopeDragStep * 4) / 4; // In quarter units
            if (amount == l.amount)
                return;
            auto trial = try_shape(scene, scene.grid, [&](save::TerrainGrid& g) { return park::shape_tiles(g, l.stroke, kTileShapings[l.tile_tool / 2], amount, [&](save::Tile t) { return shapeable(scene, t); }); });
            if (!trial)
                return;
            const float moved = trial->second;
            const auto cost = static_cast<std::int32_t>(std::lround(std::max(moved - l.paid_for, 0.0f) * kShapeCost));
            if (!affordable(scene, cost))
                return;
            pay(scene, cost, *l.tile);
            l.paid_for = std::max(l.paid_for, moved);
            l.amount = amount;
            take_ground(device, scene, assets, std::move(trial->first));
        }

        /// Makes the marks on the ground again when what the tool out works on changed.
        void mark(render::Device& device, ParkScene& scene, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            park::TerrainMesh mesh;
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            const auto whole = [&](save::Tile t) { park::tile_marks(mesh, scene.grid, t.col, t.row, t.col, t.row, park::TilePart::Whole, kMarkColour); };
            const bool tiles = l.tool == LandTools::Tool::Shape && !l.smoothing && !l.shape;
            if (tiles && l.tile)
                park::tile_marks(mesh, scene.grid, l.stroke.col0, l.stroke.row0, l.stroke.col1, l.stroke.row1, l.stroke.part, kMarkColour);
            else if (tiles)
            {
                if (const auto t = tile_target(scene, spot))
                    park::tile_marks(mesh, scene.grid, t->first.col, t->first.row, t->last.col, t->last.row, t->part, kMarkColour);
            }
            else if (l.tool != LandTools::Tool::None)
            {
                // A raising or lowering stroke stays where it began; the other tools follow the pointer.
                std::optional<save::Tile> middle = l.tool == LandTools::Tool::Shape && l.shape ? l.tile : std::nullopt;
                if (!middle && l.tool == LandTools::Tool::Water && l.tile)
                    middle = l.tile;
                if (!middle && l.tool == LandTools::Tool::Waterfall && l.upper_lake && !spot)
                    middle = l.upper_tile;
                if (!middle && spot)
                    middle = save::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                if (middle && (l.tool == LandTools::Tool::Water || l.tool == LandTools::Tool::Waterfall) && scene.grid.contains(middle->col, middle->row))
                    whole(*middle);
                else if (middle)
                {
                    for (const save::Tile t : park::brush_tiles(*middle, l.brush, l.tool != LandTools::Tool::Buy, scene.grid.width, scene.grid.height))
                        whole(t);
                }
            }
            std::vector<glm::vec3> marked;
            for (const park::GroundVertex& v : mesh.vertices)
                marked.push_back(v.position);
            if (marked == l.marked && (scene.land_marks.count > 0) == !marked.empty())
                return;
            l.marked = std::move(marked);
            set_view_lines(device, scene.land_marks, mesh);
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
                l.tile = save::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                l.middle = *spot;
                l.press_y = at.y;
                l.amount = 0;
                l.paid_for = 0;
                // Across the view: the camera looks along (sin yaw, cos yaw) over the ground.
                l.along = { std::cos(scene.camera.yaw), -std::sin(scene.camera.yaw) };
                start_stroke(scene);
                l.before = park::corner_heights(scene.grid, [&](save::Tile t) { return shapeable(scene, t); });
            }
            const float steps = (l.press_y - at.y) / park::kSlopeDragStep;
            const float amount = std::round(std::max(lowers(*l.shape) ? -steps : steps, 0.0f) * 4) / 4; // In quarter units
            if (amount == l.amount)
                return;
            auto trial = try_shape(scene, scene.grid, [&](save::TerrainGrid& g) { return park::shape_ground(g, l.before, *l.shape, l.middle, static_cast<float>(l.brush) / 2, l.along, amount); });
            if (!trial)
                return;
            const float moved = trial->second;
            const auto cost = static_cast<std::int32_t>(std::lround(std::max(moved - l.paid_for, 0.0f) * kShapeCost));
            if (!affordable(scene, cost))
                return;
            pay(scene, cost, *l.tile);
            l.paid_for = std::max(l.paid_for, moved);
            l.amount = amount;
            take_ground(device, scene, assets, std::move(trial->first));
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
                l.tile = save::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                start_stroke(scene);
                l.held = kSmoothPass; // A click smooths once
                l.target = park::mean_height(scene.grid, *spot, std::max(reach, 1.0f));
            }
            if (!spot)
                return;
            if (tool.level == Level::Pointer)
                l.target = park::mean_height(scene.grid, *spot, 1);
            l.held += elapsed;
            std::optional<save::TerrainGrid> shaped; // By this frame's passes
            for (; l.held >= kSmoothPass; l.held -= kSmoothPass)
            {
                const std::uint64_t seed = ++l.passes;
                auto trial = try_shape(scene, shaped ? *shaped : scene.grid, [&](save::TerrainGrid& g) { return park::smooth_ground(g, tool.tool, *spot, reach, kSmoothRate * kSmoothPass, l.target, seed, [&](save::Tile t) { return shapeable(scene, t); }); });
                if (!trial)
                    continue;
                const auto cost = static_cast<std::int32_t>(std::lround(trial->second * kShapeCost));
                if (trial->second <= 0 || !affordable(scene, cost))
                    continue;
                pay(scene, cost, { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) });
                shaped = std::move(trial->first);
            }
            if (shaped)
                take_ground(device, scene, assets, std::move(*shaped));
        }

        void paint(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            const auto tile = over ? std::nullopt : ground_tile(scene, at, size);
            if (!tile || tile == l.tile || (l.cliff ? *l.cliff >= l.cliffs.size() : l.surface >= l.surfaces.size()))
                return;
            l.tile = tile;
            const std::uint8_t surface = l.cliff ? l.cliffs[*l.cliff].index : l.surfaces[l.surface].index;
            std::optional<park::TileRect> painted;
            for (const save::Tile t : park::brush_tiles(*tile, l.brush, true, scene.grid.width, scene.grid.height))
            {
                save::TerrainCell& cell = scene.grid.at(t.col, t.row);
                std::uint8_t& face = l.cliff ? cell.cliff : cell.surface;
                if (face == surface || !park::owned_outright(cell) || !affordable(scene, kPaintCost))
                    continue;
                face = surface;
                pay(scene, kPaintCost, *tile); // From the brush's middle, so a dab floats up as one sum
                const park::TileRect one { t.col, t.row, t.col + 1, t.row + 1 };
                painted = !painted ? one : park::TileRect { std::min(painted->col, one.col), std::min(painted->row, one.row), std::max(painted->end_col, one.end_col), std::max(painted->end_row, one.end_row) };
            }
            if (painted)
                rebuild_ground(device, scene, assets, painted);
        }

        void say(ParkScene& scene, std::string_view key)
        {
            simulation::post(scene.park.messages, simulation::ParkMessage { .text_key = std::string(key) });
        }

        /// Fills the lake holding the tile under `top` again at its level, on the ground as it now is, over the tiles
        /// it held and those of `more`; never beyond, so it cannot run out through a gap the ground left.
        void refit_lake(ParkScene& scene, const save::WorldPoint3& top, std::span<const save::CarvedTile> more)
        {
            const save::TerrainGrid& g = scene.grid;
            const save::Tile tile { static_cast<int>(std::floor((top.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((top.z - g.origin_z) / g.tile_size_z)) };
            for (save::WaterBody& b : scene.footpaths.water.bodies)
            {
                if (!park::holds(b, tile))
                    continue;
                const auto within = [&](save::Tile t) { return park::holds(b, t) || std::ranges::any_of(more, [&](const save::CarvedTile& c) { return c.tile.col == t.col && c.tile.row == t.row; }); };
                if (auto refilled = park::fill_water(g, tile, b.level, within))
                    b = std::move(*refilled);
                return;
            }
        }

        /// A click with the water tool: takes the lake clicked away, or pours one up to the height clicked.
        void pour(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (l.tile || over || size.x <= 0 || size.y <= 0)
                return; // Once a press
            const save::TerrainGrid& g = scene.grid;
            const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            const auto distance = park::ground_hit(ray, g);
            if (!distance)
                return;
            // The water starts on the tile the pointer was over just before meeting the ground: the one in front of
            // a cliff wall clicked, else the one clicked.
            const glm::vec3 front = ray.origin + ray.direction * (*distance - kInFront);
            const save::Tile tile { static_cast<int>(std::floor((front.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((front.z - g.origin_z) / g.tile_size_z)) };
            if (!g.contains(tile.col, tile.row))
                return;
            l.tile = tile;
            const float height = ray.origin.y + ray.direction.y * *distance;
            const auto& paths = scene.footpaths.paths;
            const auto allowed = [&](save::Tile t) { return park::owned_outright(g.at(t)) && std::ranges::none_of(paths, [&](const save::PathTile& p) { return p.col == t.col && p.row == t.row && !p.elevation; }); };
            const auto& bodies = scene.footpaths.water.bodies;
            const auto lake = std::ranges::find_if(bodies, [&](const save::WaterBody& b) { return park::holds(b, *l.tile); });
            if (lake != bodies.end() && height < lake->level)
            {
                // The ground clicked is under the lake: the click was on its surface.
                if (!allowed(*l.tile))
                    return say(scene, kWaterRefused);
                // Its waterfalls go with it, putting back the ground they cut.
                save::TerrainGrid ground = g;
                park::remove_lake(scene.footpaths.water, scene.footpaths.waterfalls, ground, *l.tile);
                show_park_particles(scene.particles, park::waterfall_spray(scene.footpaths.waterfalls), &ParkParticles::sprays);
                start_stroke(scene);
                take_ground(device, scene, assets, std::move(ground), false);
                return end_stroke(scene);
            }
            // shortcut: waterfalls of a lake raised this way keep their old heights, ask before covering them when
            // falls get a yes/no question
            else if (auto poured = park::pour_water(scene.footpaths.water, g, *l.tile, height, allowed))
                scene.footpaths.water = std::move(*poured);
            else
                return say(scene, kWaterRefused);
            rebuild_ground(device, scene, assets);
        }

        /// A click with the waterfall tool: picks the first lake, or makes the waterfall to the second.
        void pick_lake(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (l.tile)
                return; // Once a press
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            if (!spot)
                return;
            const save::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            l.tile = tile;
            const auto& bodies = scene.footpaths.water.bodies;
            const auto lake = std::ranges::find_if(bodies, [&](const save::WaterBody& b) { return park::holds(b, tile); });
            if (lake == bodies.end())
                return;
            const auto index = static_cast<std::size_t>(lake - bodies.begin());
            if (!l.upper_lake)
            {
                l.upper_lake = index;
                l.upper_tile = tile;
                return;
            }
            const auto fall = park::make_waterfall(scene.grid, bodies, *std::exchange(l.upper_lake, std::nullopt), index, l.upper_tile);
            if (!fall)
                return say(scene, fall.error());
            // The spillway is cut where nothing holds the ground; the higher lake spreads into it.
            save::TerrainGrid ground = scene.grid;
            park::cut_spillway(ground, *fall);
            start_stroke(scene);
            for (const save::CarvedTile& c : fall->carved)
            {
                if (park::refuses(l.holds, c.tile, c.corners, ground.at(c.tile.col, c.tile.row).corners))
                    return say(scene, park::kWaterfallLandUnsuitable);
            }
            take_ground(device, scene, assets, std::move(ground), false);
            end_stroke(scene);
            refit_lake(scene, fall->top, fall->carved);
            scene.footpaths.waterfalls.push_back(*fall);
            show_park_particles(scene.particles, park::waterfall_spray(scene.footpaths.waterfalls), &ParkParticles::sprays);
            rebuild_ground(device, scene, assets);
        }

        /// The fence round the park's land built again, and the staff kept to that land, after it changed.
        /// shortcut: the old panels stay hidden among the objects; drop them when objects can be taken out.
        void land_owned_changed(ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            simulation::keep_staff_in(scene.sim, scene.grid);
            wait_for_build(scene);
            for (save::PlacedObject& o : scene.objects)
                if (std::ranges::contains(scene.fence_panels, o.id))
                    o.hidden = true;
            scene.fence_panels.clear();
            const save::Park& park = scene.park_model.park;
            park::BoundaryFence fence = park::boundary_fence(park.fence_symbol, park.fence_overlay, scene.grid, scene.footpaths.paths, last_id(scene));
            if (!fence.pieces.empty() && load_object_model(scene, scene.placing.models, assets, fence.pieces.front()))
            {
                const auto& colours = scene.models.at(park::model_key(fence.pieces.front())).default_colours;
                for (save::PlacedObject& panel : fence.pieces)
                {
                    panel.colours = colours;
                    scene.fence_panels.push_back(panel.id);
                    scene.objects.push_back(std::move(panel));
                }
                scene.leans.merge(fence.leans);
            }
            objects_changed(scene);
        }

        /// A press with the buying tool: buys what is on sale under the brush, once for each tile the pointer moves to.
        void buy(ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            if (!spot)
                return;
            const save::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            if (l.tile == tile || !scene.grid.contains(tile.col, tile.row))
                return;
            l.tile = tile;
            const save::Finance& prices = scene.park_model.finance;
            const std::int32_t price = l.buying == park::LandBuy::Land ? prices.land_buy_cost : prices.rights_buy_cost;
            const park::LandBought bought = park::buy_land(scene.grid, park::brush_tiles(tile, l.brush, false, scene.grid.width, scene.grid.height), l.buying, price, scene.park.cash);
            if (bought.short_of_cash)
                say(scene, kNoMoney);
            if (bought.tiles == 0)
                return;
            if (scene.park.cash)
                *scene.park.cash -= bought.spent;
            float_money(scene, tile_point(scene.grid, tile), -bought.spent);
            land_owned_changed(scene, assets);
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

        std::optional<ui::LandPanel&> panel_of(const ParkScene& scene)
        {
            return scene.desktop->root().find_as<ui::LandPanel>(kPanelName);
        }

        /// The panel with the tabs of the tool out: shaping's, or painting's with the surfaces read.
        void open_panel(ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            const LandTools& l = scene.land;
            const ui::StringTable& strings = *assets.strings;
            auto panel = panel_of(scene);
            if (!panel)
                panel = scene.desktop->root().add<ui::LandPanel>(std::string(kPanelName), ui::game_skins::land_panel_style(assets.palette), strings);
            std::vector<ui::LandTab> tabs;
            ui::LandPick chosen;
            if (l.tool == LandTools::Tool::Buy)
            {
                ui::LandTab buying { "ParkInfoIcon_BuyLand", std::u16string(strings.get("Park_BuyLandHelp")), 2, { } };
                buying.tools.push_back({ "ParkInfoIcon_BuyLand", std::u16string(strings.get("Park_BuyLandHelp")), true });
                buying.tools.push_back({ "ParkInfoIcon_BuyConstructionRites", std::u16string(strings.get("Park_BuyConstructionRitesHelp")), true });
                tabs.push_back(std::move(buying));
                chosen = { 0, l.buying == park::LandBuy::Rights ? 1U : 0U };
            }
            else if (l.tool == LandTools::Tool::Paint)
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
                tabs.push_back(tab_of(strings, "Land_Vertex", 2, { "Land_Vertex_Neighbour", "Land_Vertex_Neighbour2", "Land_Vertex_Freeform", "Land_Vertex_Freeform2", "Land_Vertex_Quantised1", "Land_Vertex_Quantised2" }, kTileShapings.size() * 2));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Smooth", 3, { "Land_Brush_Height_Smooth_Average", "Land_Brush_Height_Smooth_FlatMove", "Land_Brush_Height_Smooth_Flat", "Land_Brush_Height_Smooth_Flat1", "Land_Brush_Height_Smooth_Round", "Land_Brush_Height_Smooth_Square" }, kSmoothTools.size()));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Raise", 4, { "Land_Brush_Height_Raise_Ridge", "Land_Brush_Height_Raise_Mountain", "Land_Brush_Height_Raise_Mesa", "Land_Brush_Height_Raise_Hill" }, kRaiseShapes.size()));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Lower", 5, { "Land_Brush_Height_Lower_Canyon", "Land_Brush_Height_Lower_Ditch", "Land_Brush_Height_Lower_Crater" }, kLowerShapes.size()));
                chosen = { kTileTab, l.tile_tool };
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
        if (tool != LandTools::Tool::Water && tool != LandTools::Tool::Waterfall)
            open_panel(scene, assets);
    }

    void buy_land_tool(ParkScene& scene, park::LandBuy what, const ui::InterfaceAssets& assets)
    {
        stop_land_tools(scene);
        scene.land.tool = LandTools::Tool::Buy;
        scene.land.buying = what;
        open_panel(scene, assets);
    }

    void stop_land_tools(ParkScene& scene)
    {
        LandTools& l = scene.land;
        if (auto panel = panel_of(scene))
            panel->hide();
        l.tool = LandTools::Tool::None;
        end_stroke(scene);
        l.tile.reset();
        l.holds = { };
        l.before = { };
        l.stroke = { };
        l.upper_lake.reset();
    }

    bool land_tool(const ParkScene& scene)
    {
        return scene.land.tool != LandTools::Tool::None;
    }

    bool delete_waterfall(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size)
    {
        if (scene.land.tool != LandTools::Tool::Waterfall)
            return false;
        const auto spot = ground_spot(scene, at, size);
        if (!spot)
            return false;
        const save::TerrainGrid& g = scene.grid;
        auto& falls = scene.footpaths.waterfalls;
        const auto fall = park::waterfall_at(falls, g.origin_x + spot->x * g.tile_size_x, g.origin_z + spot->y * g.tile_size_z);
        if (!fall)
            return false;
        const save::Waterfall& f = falls[*fall];
        const auto col = static_cast<int>(std::floor(((f.lip_left.x + f.lip_right.x) / 2 - g.origin_x) / g.tile_size_x - f.flow.x / 2));
        const auto row = static_cast<int>(std::floor(((f.lip_left.z + f.lip_right.z) / 2 - g.origin_z) / g.tile_size_z - f.flow.z / 2));
        // The ground its spillway was cut from goes back, so must be the park's.
        const auto owned = [&](save::Tile t) { return !g.contains(t.col, t.row) || park::owned_outright(g.at(t.col, t.row)); };
        if (!owned({ col, row }) || !std::ranges::all_of(f.carved, [&](const save::CarvedTile& c) { return owned(c.tile); }))
        {
            say(scene, park::kWaterfallDeleteNotOwned);
            return true;
        }
        save::TerrainGrid ground = g;
        park::restore_spillway(ground, f);
        const save::WorldPoint3 top = f.top;
        falls.erase(falls.begin() + static_cast<std::ptrdiff_t>(*fall));
        show_park_particles(scene.particles, park::waterfall_spray(falls), &ParkParticles::sprays);
        start_stroke(scene);
        take_ground(device, scene, assets, std::move(ground), false);
        end_stroke(scene);
        refit_lake(scene, top, { });
        rebuild_ground(device, scene, assets);
        return true;
    }

    void update_land_tools(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, float elapsed)
    {
        LandTools& l = scene.land;
        if (l.rumble)
            l.rumble = std::max(kRumbleQuiet, *l.rumble - elapsed / kRumbleFade);
        if (auto panel = panel_of(scene); panel && (l.tool == LandTools::Tool::Shape || l.tool == LandTools::Tool::Paint || l.tool == LandTools::Tool::Buy))
        {
            if (panel->take_closed())
            {
                stop_land_tools(scene); // Closed by the player
                return;
            }
            l.brush = panel->brush();
            if (const auto pick = panel->take_pick(); pick && l.tool == LandTools::Tool::Buy)
            {
                l.tile.reset();
                l.buying = pick->tool == 1 ? park::LandBuy::Rights : park::LandBuy::Land;
            }
            else if (pick && l.tool == LandTools::Tool::Paint)
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
                if (pick->tab == kTileTab && pick->tool < kTileShapings.size() * 2)
                    l.tile_tool = pick->tool;
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
            end_stroke(scene);
            l.tile.reset();
        }
        else if (l.tool == LandTools::Tool::Shape && l.smoothing)
            smooth(device, scene, assets, at, size, over, elapsed);
        else if (l.tool == LandTools::Tool::Shape && l.shape)
            brush_shape(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Shape)
            shape(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Paint)
            paint(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Waterfall)
            pick_lake(device, scene, assets, at, size, over);
        else if (l.tool == LandTools::Tool::Buy)
            buy(scene, assets, at, size, over);
        else
            pour(device, scene, assets, at, size, over);
        mark(device, scene, at, size, over);
    }

} // namespace rct3::scene
