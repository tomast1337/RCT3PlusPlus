// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/land_tools.hpp"

#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/ground/brush.hpp"
#include "park/object/pick.hpp"
#include "park/ride/placement.hpp"
#include "park/view_lines.hpp"
#include "park/water/fill.hpp"
#include "park/waterfall_making.hpp"
#include "park/waterfall_mesh.hpp"
#include "scene/objects.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/ground.hpp"
#include "scene/park/questions.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/view.hpp"
#include "scene/ride_placing.hpp"
#include "simulation/import_park.hpp"
#include "simulation/park/messages.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/build/land.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace rct3::scene
{

    namespace
    {
        // Prices, in hundredths: painting one tile (moving the ground is rct3::park::ground::kShapeCost).
        constexpr float kTravelSample = 11.3f;          ///< Pixels the pointer moves before the way it goes over the ground is read again
        constexpr units::Seconds kLongestStep { 0.1f }; ///< A spray counts a frame as this at most, so a hitch does not jolt the ground
        constexpr float kSteepestDrag = 0.5f;           ///< Chosen by eye: a tile tool reads a view steeper than this (the part of the pointer's ray along the ground) as this steep
        constexpr float kRumbleQuiet = 0.25f;           ///< Chosen by eye: the rumble of a shaping stroke while the ground stays still
        constexpr units::Seconds kRumbleFade { 0.6f };  ///< Chosen by eye: it takes this long to fall back to that once the ground stops moving

        constexpr std::string_view kPanelName = "land_panel";
        constexpr std::string_view kWaterRefused = "Rct3pp_WaterRefused";
        constexpr float kInFront = 0.01f; ///< Back along the pointer's ray from the ground it meets, in world units

        /// A tab's tools by their pictures, `usable` the first of them that work so far.
        ui::screens::build::LandTab tab_of(const ui::StringTable& strings, std::string_view icon, int slot, std::initializer_list<std::string_view> tools, std::size_t usable)
        {
            ui::screens::build::LandTab t { std::string(icon), std::u16string(strings.get(std::format("{}Help", icon))), slot, { } };
            for (const std::string_view tool : tools)
                t.tools.push_back({ std::string(tool), std::u16string(strings.get(std::format("{}Help", tool))), t.tools.size() < usable });
            return t;
        }

        /// Pays `cost` for work on `tile` as landscaping. A stroke shows one sum, where it first paid, that grows
        /// while the stroke goes and floats away once it stops. False when the park cannot afford it, and nothing
        /// is paid.
        bool pay(park::ParkScene& scene, std::int32_t cost, save::model::Tile tile)
        {
            if (!simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, units::Money { cost }, simulation::park::LedgerLine::Landscaping, false))
                return false;
            LandTools& l = scene.land;
            if (!l.cost_at)
                l.cost_at = park::tile_point(scene.grid, tile);
            const auto sum = std::ranges::find_if(scene.money, [&](const park::FloatingMoney& m) { return m.at == *l.cost_at && m.amount < 0; });
            if (sum == scene.money.end())
                park::float_money(scene, *l.cost_at, -cost);
            else if (cost != 0)
            {
                sum->amount -= cost;
                sum->age = 0;
            }
            return true;
        }

        /// The raising and lowering brushes' tab, by its place in the panel.
        constexpr std::size_t kModeTab = 3;
        constexpr std::size_t kBrushTab = 2;
        constexpr std::size_t kSmoothTab = 1;
        constexpr std::size_t kCliffTab = 1; ///< Of painting
        constexpr std::size_t kTileTab = 0;
        /// The tile tools' pairs, in their tab's order.
        constexpr std::array kTileShapings { rct3::park::TileShaping::Snap, rct3::park::TileShaping::Free, rct3::park::TileShaping::Whole };
        constexpr glm::vec4 kMarkColour { 1.0f, 1.0f, 1.0f, 0.85f };
        constexpr float kRingPiece = 1.0f;  ///< Length of the pieces the brush's ring is drawn in, in world units
        constexpr float kRingLift = 0.2f;   ///< Of the ring over the ground, enough not to flicker against it from afar
        constexpr float kRingWidth = 0.25f; ///< Of its line

        /// The smoothing tab's tools, in its order, and where a levelling one levels to.
        enum class Level : std::uint8_t
        {
            None,
            Pointer, ///< The height under the pointer, as it moves
            Press,   ///< The height where the stroke began
        };
        struct SmoothTool
        {
            rct3::park::ground::GroundSmoothing tool;
            Level level = Level::None;
            bool whole = false; ///< Levels to the whole unit of that height: a pad
        };
        constexpr std::array kSmoothTools {
            SmoothTool { rct3::park::ground::GroundSmoothing::Average }, SmoothTool { rct3::park::ground::GroundSmoothing::Level, Level::Pointer }, SmoothTool { rct3::park::ground::GroundSmoothing::Level, Level::Press }, SmoothTool { rct3::park::ground::GroundSmoothing::Level, Level::Press, true }, SmoothTool { rct3::park::ground::GroundSmoothing::JoinEdges }, SmoothTool { rct3::park::ground::GroundSmoothing::TileEdges },
        };
        /// The brushes' tab, in its order: those that raise when dragged up, then those that lower when dragged down.
        constexpr std::array kShapeBrushes { rct3::park::ground::GroundShape::Ridge, rct3::park::ground::GroundShape::Mountain, rct3::park::ground::GroundShape::Mesa, rct3::park::ground::GroundShape::Hill, rct3::park::ground::GroundShape::Canyon, rct3::park::ground::GroundShape::Trough, rct3::park::ground::GroundShape::Crater };

        std::uint32_t tile_key(save::model::Tile t)
        {
            return static_cast<std::uint32_t>(t.row) << 16 | static_cast<std::uint16_t>(t.col);
        }

        /// Whether a tool may move the ground of `tile` at all this stroke: owned outright and not refused yet.
        bool shapeable(const park::ParkScene& scene, save::model::Tile tile)
        {
            return rct3::park::owned_outright(scene.grid.at(tile)) && !scene.land.refused.contains(tile_key(tile));
        }

        /// Gathers what stands on the ground as a stroke starts; the ground under a stroke cannot reach it.
        void start_stroke(park::ParkScene& scene)
        {
            LandTools& l = scene.land;
            l.holds = rct3::park::ground::ground_holds(scene.objects, scene.models, scene.footpaths.paths, scene.grid);
            l.refused = l.holds.held;
            l.start = LandTools::Start { .grid = scene.grid, .water = scene.footpaths.water, .waterfalls = scene.footpaths.waterfalls };
            if (l.tool == LandTools::Tool::Shape)
                l.rumble = kRumbleQuiet;
        }

        /// Ends a stroke: seats the scenery standing on the ground it moved on its new height.
        void end_stroke(park::ParkScene& scene)
        {
            LandTools& l = scene.land;
            l.rumble.reset();
            l.cost_at.reset();
            l.start.reset();
            if (!l.stirred)
                return;
            if (scene.object_build.valid())
                (void)scene.object_build.get(); // It reads the items being moved
            if (rct3::park::ground::seat_followers(scene.objects, l.holds, scene.models, scene.grid, (*l.stirred)[0], (*l.stirred)[1]))
            {
                scene.levels = { };
                scene.since_rebuild = park::kObjectRebuildInterval;
            }
            l.stirred.reset();
        }

        /// Shapes a copy of the ground `from` with `shape` (which returns what it moved) again until what stands on it lets
        /// every tile it moved go, holding those it refuses for the rest of the stroke. Nothing when a tile refused
        /// still moved.
        template<typename Shape> std::optional<std::pair<save::model::TerrainGrid, float>> try_shape(park::ParkScene& scene, const save::model::TerrainGrid& from, Shape&& shape)
        {
            LandTools& l = scene.land;
            for (;;)
            {
                save::model::TerrainGrid trial = from;
                const float moved = std::max(0.0f, shape(trial) - rct3::park::ground::limit_ground(trial, from)); // What a limit stopped is not paid for
                bool again = false;
                for (int r = 0; r < trial.height; ++r)
                {
                    for (int c = 0; c < trial.width; ++c)
                    {
                        const auto& was = from.at(c, r).corners;
                        const auto& now = trial.at(c, r).corners;
                        if (was == now || !rct3::park::ground::refuses(l.holds, { c, r }, was, now))
                            continue;
                        if (!l.refused.insert(tile_key({ c, r })).second)
                            return std::nullopt;
                        again = true;
                        // A brush's points round it stay where they are too.
                        if (l.before.width == trial.width && !l.before.held.empty())
                        {
                            for (int dr = 0; dr < 2; ++dr)
                                for (int dc = 0; dc < 2; ++dc)
                                    l.before.held[static_cast<std::size_t>(r + dr) * static_cast<std::size_t>(trial.width + 1) + static_cast<std::size_t>(c + dc)] = true;
                        }
                    }
                }
                if (!again)
                    return std::pair { std::move(trial), moved };
            }
        }

        /// Takes on the shaped ground `trial`, the scenery on it following.
        void take_ground(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, save::model::TerrainGrid trial, bool drain = true)
        {
            LandTools& l = scene.land;
            // The meshes built beside the frame read the ground.
            if (scene.object_build.valid())
            {
                if (auto built = scene.object_build.get())
                    park::show_objects(device, scene, std::move(*built));
            }
            if (scene.people_build.valid())
                scene.people_build.wait();
            const save::model::TerrainGrid was = std::exchange(scene.grid, std::move(trial));
            std::optional<std::array<save::model::Tile, 2>> step;
            for (int r = 0; r < was.height; ++r)
                for (int c = 0; c < was.width; ++c)
                {
                    if (scene.grid.at(c, r).corners == was.at(c, r).corners)
                        continue;
                    if (!step)
                        step = std::array { save::model::Tile { c, r }, save::model::Tile { c, r } };
                    (*step)[0] = { std::min((*step)[0].col, c), std::min((*step)[0].row, r) };
                    (*step)[1] = { std::max((*step)[1].col, c), std::max((*step)[1].row, r) };
                }
            // The lakes settle on the new ground, from how the stroke found them; waterfalls beside it go.
            bool drained = false;
            if (drain && l.start)
            {
                save::model::WaterLayer water = l.start->water;
                std::vector<save::model::Waterfall> falls = l.start->waterfalls;
                rct3::park::water::settle_water(water, falls, l.start->grid, scene.grid);
                const auto same = [](const save::model::WaterBody& a, const save::model::WaterBody& b) { return a.level == b.level && std::ranges::equal(a.tris, b.tris, [](const save::model::WaterTri& x, const save::model::WaterTri& y) { return x.col == y.col && x.row == y.row && x.triangle == y.triangle && x.mask == y.mask; }); };
                drained = falls.size() != scene.footpaths.waterfalls.size() || !std::ranges::equal(water.bodies, scene.footpaths.water.bodies, same);
                scene.footpaths.water = std::move(water);
                scene.footpaths.waterfalls = std::move(falls);
            }
            if (drained)
                park::show_park_particles(scene.particles, rct3::park::waterfall_spray(scene.footpaths.waterfalls), &park::ParkParticles::sprays);
            // A lake that changed may reach far from the step; a moved corner shades the tiles beside those round it.
            std::optional<rct3::park::TileRect> touched;
            if (step && !drained)
                touched = rct3::park::TileRect { (*step)[0].col, (*step)[0].row, (*step)[1].col + 1, (*step)[1].row + 1 }.grown(1);
            park::rebuild_ground(device, scene, assets, touched);
            if (!step)
                return;
            simulation::follow_ground(scene.sim, scene.footpaths.paths, was, scene.grid, (*step)[0], (*step)[1]);
            if (l.rumble)
                l.rumble = 1.0f;
            if (!l.stirred)
                l.stirred = step;
            (*l.stirred)[0] = { std::min((*l.stirred)[0].col, (*step)[0].col), std::min((*l.stirred)[0].row, (*step)[0].row) };
            (*l.stirred)[1] = { std::max((*l.stirred)[1].col, (*step)[1].col), std::max((*l.stirred)[1].row, (*step)[1].row) };
            // Items standing at the ground's height are drawn there once built again; the stroke's end seats them all.
            constexpr int kWidest = 4; // Tiles an item reaches from the one it stands on
            const auto near = [&](std::size_t i) {
                const save::model::PlacedObject& o = scene.objects[i];
                return o.col + kWidest >= (*step)[0].col && o.col - kWidest <= (*step)[1].col && o.row + kWidest >= (*step)[0].row && o.row - kWidest <= (*step)[1].row;
            };
            if (std::ranges::any_of(l.holds.followers, near))
            {
                scene.levels = { };
                scene.since_rebuild = park::kObjectRebuildInterval;
            }
        }

        /// The tiles a tile tool works on with the pointer at `spot`, from the first to the last, and the part.
        struct TileTarget
        {
            save::model::Tile first;
            save::model::Tile last;
            rct3::park::TilePart part = rct3::park::TilePart::Whole;
        };
        std::optional<TileTarget> tile_target(const park::ParkScene& scene, std::optional<glm::vec2> spot)
        {
            if (!spot)
                return std::nullopt;
            const save::model::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            if (!scene.grid.contains(tile.col, tile.row))
                return std::nullopt;
            // The first of each pair works on one tile, the second on the brush's square.
            if (scene.land.tile_tool % 2 == 0)
                return TileTarget { tile, tile, rct3::park::part_at({ spot->x - static_cast<float>(tile.col), spot->y - static_cast<float>(tile.row) }) };
            const auto tiles = rct3::park::ground::brush_tiles(tile, rct3::park::ground::brush_across(scene.land.brush, scene.grid.tile_size_x), scene.grid.width, scene.grid.height);
            TileTarget t { tile, tile };
            for (const save::model::Tile s : tiles)
            {
                t.first = { std::min(t.first.col, s.col), std::min(t.first.row, s.row) };
                t.last = { std::max(t.last.col, s.col), std::max(t.last.row, s.row) };
            }
            return t;
        }

        void shape(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (!l.tile)
            {
                const auto target = over ? std::nullopt : tile_target(scene, ground_spot(scene, at, size));
                if (!target)
                    return;
                // The stroke's tiles go up and down with the pointer from the point of the ground it pressed on.
                const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
                const auto distance = rct3::park::object::ground_hit(ray, scene.grid);
                if (!distance)
                    return;
                l.press_at = ray.origin + ray.direction * *distance;
                l.press_height = rct3::park::upright_height(ray, l.press_at, kSteepestDrag).value_or(l.press_at.y);
                l.press_camera = scene.camera;
                l.tile = target->first;
                l.amount = 0;
                l.paid_for = 0;
                start_stroke(scene);
                l.stroke = rct3::park::begin_tile_stroke(scene.grid, target->first.col, target->first.row, target->last.col, target->last.row, target->part);
            }
            // Read through the view as it was at the press: the view rises with the ground it looks at.
            const auto height = size.y > 0 ? rct3::park::upright_height(rct3::park::view_ray(l.press_camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y }), l.press_at, kSteepestDrag) : std::nullopt;
            if (!height || *height - l.press_height == l.amount)
                return;
            const float amount = *height - l.press_height;
            auto trial = try_shape(scene, scene.grid, [&](save::model::TerrainGrid& g) { return rct3::park::shape_tiles(g, l.stroke, kTileShapings[l.tile_tool / 2], amount, [&](save::model::Tile t) { return shapeable(scene, t); }); });
            if (!trial)
                return;
            const float moved = trial->second;
            const auto cost = static_cast<std::int32_t>(std::lround(std::max(moved - l.paid_for, 0.0f) * rct3::park::ground::kShapeCost));
            if (!pay(scene, cost, *l.tile))
                return;
            l.paid_for = std::max(l.paid_for, moved);
            l.amount = amount;
            take_ground(device, scene, assets, std::move(trial->first));
        }

        /// Makes the marks on the ground again when what the tool out works on changed.
        void mark(render::Device& device, park::ParkScene& scene, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            rct3::park::TerrainMesh mesh;
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            const auto whole = [&](save::model::Tile t) { rct3::park::tile_marks(mesh, scene.grid, t.col, t.row, t.col, t.row, rct3::park::TilePart::Whole, kMarkColour); };
            const bool tiles = l.tool == LandTools::Tool::Shape && !l.smoothing && !l.shape;
            if (tiles && l.tile)
                rct3::park::tile_marks(mesh, scene.grid, l.stroke.col0, l.stroke.row0, l.stroke.col1, l.stroke.row1, l.stroke.part, kMarkColour);
            else if (tiles)
            {
                if (const auto t = tile_target(scene, spot))
                    rct3::park::tile_marks(mesh, scene.grid, t->first.col, t->first.row, t->last.col, t->last.row, t->part, kMarkColour);
            }
            else if (l.tool == LandTools::Tool::Shape || l.tool == LandTools::Tool::Paint)
            {
                // The brush: a ring on the ground, as wide as it reaches. A dragged stroke stays where it began;
                // spraying and painting follow the pointer.
                const std::optional<glm::vec2> middle = l.tool == LandTools::Tool::Shape && !l.spray && l.tile ? std::optional(l.middle) : spot;
                const float reach = static_cast<float>(l.brush) / scene.grid.tile_size_x;
                if (middle && l.tool == LandTools::Tool::Paint)
                {
                    for (const save::model::Tile t : rct3::park::ground::round_brush_tiles(*middle, reach, scene.grid.width, scene.grid.height))
                        whole(t);
                }
                if (middle)
                {
                    // Short pieces, each end on the ground and well over it, so slopes between do not swallow them.
                    const auto radius = static_cast<float>(l.brush);
                    const int pieces = std::clamp(static_cast<int>(glm::two_pi<float>() * radius / kRingPiece), 24, 256);
                    const auto on_ring = [&](int i) {
                        const float turn = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(pieces);
                        const float x = scene.grid.world_x(0) + middle->x * scene.grid.tile_size_x + std::cos(turn) * radius;
                        const float z = scene.grid.world_z(0) + middle->y * scene.grid.tile_size_z + std::sin(turn) * radius;
                        return scene.grid.height_at(x, z).transform([&](float y) { return glm::vec3 { x, y + kRingLift, z }; });
                    };
                    for (int i = 0; i < pieces; ++i)
                    {
                        const auto a = on_ring(i);
                        const auto b = on_ring(i + 1);
                        if (a && b)
                            rct3::park::ground_strip(mesh, *a, *b, kMarkColour, kRingWidth);
                    }
                }
            }
            else if (l.tool != LandTools::Tool::None)
            {
                std::optional<save::model::Tile> middle = l.tool == LandTools::Tool::Water ? l.tile : std::nullopt;
                if (!middle && l.tool == LandTools::Tool::Waterfall && l.upper_lake && !spot)
                    middle = l.upper_tile;
                if (!middle && spot)
                    middle = save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                if (middle && (l.tool == LandTools::Tool::Water || l.tool == LandTools::Tool::Waterfall) && scene.grid.contains(middle->col, middle->row))
                    whole(*middle);
                else if (middle)
                {
                    for (const save::model::Tile t : rct3::park::ground::brush_tiles(*middle, rct3::park::ground::brush_across(l.brush, scene.grid.tile_size_x), scene.grid.width, scene.grid.height))
                        whole(t);
                }
            }
            std::vector<glm::vec3> marked;
            for (const rct3::park::GroundVertex& v : mesh.vertices)
                marked.push_back(v.position);
            if (marked == l.marked && (scene.land_marks.count > 0) == !marked.empty())
                return;
            l.marked = std::move(marked);
            park::set_view_lines(device, scene.land_marks, mesh);
        }

        void brush_shape(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, units::Seconds elapsed)
        {
            LandTools& l = scene.land;
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            const float reach = static_cast<float>(l.brush) / scene.grid.tile_size_x;
            if (!l.tile)
            {
                if (!spot || !scene.grid.contains(static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y))))
                    return;
                l.tile = save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                l.middle = *spot;
                l.press_y = at.y;
                l.amount = 0;
                l.paid_for = 0;
                // A ridge or a canyon runs the way the pointer was going over the ground as it pressed; across the
                // view when it had not moved. The camera looks along (sin yaw, cos yaw) over the ground.
                l.along = l.travel.value_or(glm::vec2 { std::cos(scene.camera.yaw), -std::sin(scene.camera.yaw) });
                ++l.passes; // The stroke's own roughness
                start_stroke(scene);
                l.before = rct3::park::ground::ground_before(scene.grid, [&](save::model::Tile t) { return shapeable(scene, t); });
            }
            if (l.spray)
            {
                // The shape, a little higher (or deeper) every moment, on the ground as it is under the pointer now.
                if (!spot)
                    return;
                const int width = scene.grid.width;
                for (int r = std::max(static_cast<int>(std::floor(spot->y - reach)) - 1, 0); r <= std::min(static_cast<int>(std::ceil(spot->y + reach)), scene.grid.height - 1); ++r)
                    for (int c = std::max(static_cast<int>(std::floor(spot->x - reach)) - 1, 0); c <= std::min(static_cast<int>(std::ceil(spot->x + reach)), width - 1); ++c)
                        l.before.corners[static_cast<std::size_t>(r) * static_cast<std::size_t>(width) + static_cast<std::size_t>(c)] = scene.grid.at(c, r).corners;
                const float amount = static_cast<float>(l.spray_speed) * rct3::park::ground::kSprayRate * std::min(elapsed, kLongestStep).value;
                auto trial = try_shape(scene, scene.grid, [&](save::model::TerrainGrid& g) { return rct3::park::ground::shape_ground(g, l.before, *l.shape, *spot, reach, l.along, amount, l.passes); });
                if (!trial)
                    return;
                // Paid in whole hundredths; the rest is carried to the next moment.
                const float owed = (l.amount + trial->second - l.paid_for) * rct3::park::ground::kShapeCost;
                const auto cost = static_cast<std::int32_t>(std::floor(owed));
                if (!pay(scene, cost, { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }))
                    return;
                l.amount += trial->second;
                l.paid_for += static_cast<float>(cost) / rct3::park::ground::kShapeCost;
                l.middle = *spot;
                take_ground(device, scene, assets, std::move(trial->first));
                return;
            }
            // A lowering brush digs as the pointer goes down; the other way, each brush makes its shape upside down.
            const bool lowers = rct3::park::ground::shape_share(*l.shape, 0) < 0;
            float amount = rct3::park::ground::drag_amount(lowers ? at.y - l.press_y : l.press_y - at.y);
            if (amount == l.amount)
                return;
            const auto shaped = [&](save::model::TerrainGrid& g) { return rct3::park::ground::shape_ground(g, l.before, *l.shape, l.middle, reach, l.along, amount, l.passes); };
            auto trial = try_shape(scene, scene.grid, shaped);
            // Short of cash, the stroke goes as far as the cash pays for: what it moves grows evenly with it.
            if (const float can = l.paid_for + static_cast<float>(std::max<std::int64_t>(scene.park.cash.value_or(units::Money { }).value - 1, 0)) / rct3::park::ground::kShapeCost; trial && scene.park.cash && trial->second > can)
            {
                simulation::park::cannot_pay(scene.park);
                amount *= can / trial->second;
                trial = try_shape(scene, scene.grid, shaped);
            }
            if (!trial)
                return;
            const float moved = trial->second;
            const auto cost = static_cast<std::int32_t>(std::lround(std::max(moved - l.paid_for, 0.0f) * rct3::park::ground::kShapeCost));
            if (!pay(scene, cost, *l.tile))
                return;
            l.paid_for = std::max(l.paid_for, moved);
            l.amount = amount;
            take_ground(device, scene, assets, std::move(trial->first));
        }

        void smooth(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, units::Seconds elapsed)
        {
            LandTools& l = scene.land;
            const SmoothTool tool = kSmoothTools[*l.smoothing];
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            const float reach = static_cast<float>(l.brush) / scene.grid.tile_size_x;
            if (!l.tile)
            {
                if (!spot || !scene.grid.contains(static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y))))
                    return;
                l.tile = save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                start_stroke(scene);
                l.middle = *spot;
                l.press_y = at.y;
                l.amount = 0;
                l.paid_for = 0;
                // Levelling works to the height of the ground pressed on.
                const save::model::TerrainGrid& g = scene.grid;
                l.target = g.height_at(g.origin_x + spot->x * g.tile_size_x, g.origin_z + spot->y * g.tile_size_z).value_or(0);
                if (tool.whole)
                    l.target = rct3::park::ground::whole_level(l.target);
            }
            const auto free = [&](save::model::Tile t) { return shapeable(scene, t); };
            if (!l.spray)
            {
                // Dragged: the ground the stroke found, worked further the further the pointer is from its press.
                const float share = std::max(std::abs(at.y - l.press_y) - rct3::park::ground::kDragDeadZone, 0.0f) * rct3::park::ground::kSmoothDragRate;
                if (share == l.amount || !l.start)
                    return;
                auto trial = try_shape(scene, l.start->grid, [&](save::model::TerrainGrid& g) { return rct3::park::ground::smooth_ground(g, tool.tool, l.middle, reach, share, std::numeric_limits<float>::infinity(), l.target, 1, free); });
                if (!trial)
                    return;
                const auto cost = static_cast<std::int32_t>(std::lround(std::max(trial->second - l.paid_for, 0.0f) * rct3::park::ground::kShapeCost));
                if (!pay(scene, cost, *l.tile))
                    return;
                l.paid_for = std::max(l.paid_for, trial->second);
                l.amount = share;
                take_ground(device, scene, assets, std::move(trial->first));
                return;
            }
            // Sprayed: all the way, at an even speed, under the pointer as it is now.
            if (!spot)
                return;
            if (tool.level == Level::Pointer)
                l.target = rct3::park::ground::mean_height(scene.grid, *spot, 1);
            const float most = static_cast<float>(l.spray_speed) * rct3::park::ground::kSprayRate * rct3::park::ground::kSmoothSprayShare * std::min(elapsed, kLongestStep).value;
            const std::uint64_t seed = ++l.passes;
            auto trial = try_shape(scene, scene.grid, [&](save::model::TerrainGrid& g) { return rct3::park::ground::smooth_ground(g, tool.tool, *spot, reach, 1, most, l.target, seed, free); });
            if (!trial || trial->second <= 0)
                return;
            const float owed = (l.amount + trial->second - l.paid_for) * rct3::park::ground::kShapeCost;
            const auto cost = static_cast<std::int32_t>(std::floor(owed));
            if (!pay(scene, cost, { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }))
                return;
            l.amount += trial->second;
            l.paid_for += static_cast<float>(cost) / rct3::park::ground::kShapeCost;
            take_ground(device, scene, assets, std::move(trial->first));
        }

        void paint(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            if (!spot || (l.tile && *spot == l.middle) || (l.cliff ? *l.cliff >= l.cliffs.size() : l.surface >= l.surfaces.size()))
                return;
            l.tile = save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            l.middle = *spot;
            const std::uint8_t surface = l.cliff ? l.cliffs[*l.cliff].index : l.surfaces[l.surface].index;
            std::optional<rct3::park::TileRect> painted;
            for (const save::model::Tile t : rct3::park::ground::round_brush_tiles(*spot, static_cast<float>(l.brush) / scene.grid.tile_size_x, scene.grid.width, scene.grid.height))
            {
                save::model::TerrainCell& cell = scene.grid.at(t.col, t.row);
                std::uint8_t& face = l.cliff ? cell.cliff : cell.surface;
                if (face == surface || !rct3::park::owned_outright(cell))
                    continue;
                face = surface;
                const rct3::park::TileRect one { t.col, t.row, t.col + 1, t.row + 1 };
                painted = !painted ? one : rct3::park::TileRect { std::min(painted->col, one.col), std::min(painted->row, one.row), std::max(painted->end_col, one.end_col), std::max(painted->end_row, one.end_row) };
            }
            if (painted)
                park::rebuild_ground(device, scene, assets, painted);
        }

        void say(park::ParkScene& scene, std::string_view key)
        {
            simulation::park::post(scene.park.messages, simulation::park::ParkMessage { .text_key = std::string(key) });
        }

        /// Fills the lake holding the tile under `top` again at its level, on the ground as it now is, over the tiles
        /// it held and those of `more`; never beyond, so it cannot run out through a gap the ground left.
        void refit_lake(park::ParkScene& scene, const save::model::WorldPoint3& top, std::span<const save::model::CarvedTile> more)
        {
            const save::model::TerrainGrid& g = scene.grid;
            const save::model::Tile tile { static_cast<int>(std::floor((top.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((top.z - g.origin_z) / g.tile_size_z)) };
            for (save::model::WaterBody& b : scene.footpaths.water.bodies)
            {
                if (!rct3::park::water::holds(b, tile))
                    continue;
                const auto within = [&](save::model::Tile t) { return rct3::park::water::holds(b, t) || std::ranges::any_of(more, [&](const save::model::CarvedTile& c) { return c.tile.col == t.col && c.tile.row == t.row; }); };
                if (auto refilled = rct3::park::water::fill_water(g, tile, b.level, within))
                    b = std::move(*refilled);
                return;
            }
        }

        /// A click with the water tool: takes the lake clicked away, or pours one up to the height clicked.
        void pour(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (l.tile || over || size.x <= 0 || size.y <= 0)
                return; // Once a press
            const save::model::TerrainGrid& g = scene.grid;
            const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            const auto distance = rct3::park::object::ground_hit(ray, g);
            if (!distance)
                return;
            // The water starts on the tile the pointer was over just before meeting the ground: the one in front of
            // a cliff wall clicked, else the one clicked.
            const glm::vec3 front = ray.origin + ray.direction * (*distance - kInFront);
            const save::model::Tile tile { static_cast<int>(std::floor((front.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((front.z - g.origin_z) / g.tile_size_z)) };
            if (!g.contains(tile.col, tile.row))
                return;
            l.tile = tile;
            const float height = ray.origin.y + ray.direction.y * *distance;
            const auto& paths = scene.footpaths.paths;
            const auto allowed = [&](save::model::Tile t) { return rct3::park::owned_outright(g.at(t)) && std::ranges::none_of(paths, [&](const save::model::PathTile& p) { return p.col == t.col && p.row == t.row && !p.elevation; }); };
            const auto& bodies = scene.footpaths.water.bodies;
            const auto lake = std::ranges::find_if(bodies, [&](const save::model::WaterBody& b) { return rct3::park::water::holds(b, *l.tile); });
            if (lake != bodies.end() && height < lake->level)
            {
                // The ground clicked is under the lake: the click was on its surface.
                if (!allowed(*l.tile))
                    return say(scene, kWaterRefused);
                // A lake with waterfalls goes only once the player said so.
                const auto on_lake = [&](const save::model::WorldPoint3& p) { return rct3::park::water::holds(*lake, { static_cast<int>(std::floor((p.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((p.z - g.origin_z) / g.tile_size_z)) }); };
                if (std::ranges::any_of(scene.footpaths.waterfalls, [&](const save::model::Waterfall& f) { return on_lake(f.top) || on_lake(f.bottom); }))
                    return park::ask_remove_lake(scene, *assets.strings, assets.palette, *l.tile);
                return remove_lake(device, scene, assets, *l.tile);
            }
            else if (auto poured = rct3::park::water::pour_water(scene.footpaths.water, g, *l.tile, height, allowed))
            {
                // Water that would take in a lake with waterfalls is not poured: the player is asked whether those
                // falls should go, and pours again once they have.
                const auto& falls = scene.footpaths.waterfalls;
                if (const auto flooded = rct3::park::water::flooded_falls(scene.footpaths.water, *poured, falls, g); !flooded.empty())
                {
                    std::vector<save::model::WorldPoint3> tops;
                    for (const std::size_t i : flooded)
                        tops.push_back(falls[i].top);
                    return park::ask_flood_falls(scene, *assets.strings, assets.palette, std::move(tops));
                }
                scene.footpaths.water = std::move(*poured);
            }
            else
                return say(scene, kWaterRefused);
            park::rebuild_ground(device, scene, assets);
        }

        /// A click with the waterfall tool: picks the first lake, or makes the waterfall to the second.
        void pick_lake(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            if (l.tile)
                return; // Once a press
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            if (!spot)
                return;
            const save::model::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            l.tile = tile;
            const auto& bodies = scene.footpaths.water.bodies;
            const auto lake = std::ranges::find_if(bodies, [&](const save::model::WaterBody& b) { return rct3::park::water::holds(b, tile); });
            if (lake == bodies.end())
                return;
            const auto index = static_cast<std::size_t>(lake - bodies.begin());
            if (!l.upper_lake)
            {
                l.upper_lake = index;
                l.upper_tile = tile;
                return;
            }
            const auto fall = rct3::park::make_waterfall(scene.grid, bodies, *std::exchange(l.upper_lake, std::nullopt), index, l.upper_tile, tile);
            if (!fall)
                return say(scene, fall.error());
            // The spillway is cut where nothing holds the ground; the higher lake spreads into it.
            save::model::TerrainGrid ground = scene.grid;
            rct3::park::cut_spillway(ground, *fall);
            start_stroke(scene);
            for (const save::model::CarvedTile& c : fall->carved)
            {
                if (rct3::park::ground::refuses(l.holds, c.tile, c.corners, ground.at(c.tile.col, c.tile.row).corners))
                    return say(scene, rct3::park::kWaterfallLandUnsuitable);
            }
            take_ground(device, scene, assets, std::move(ground), false);
            end_stroke(scene);
            refit_lake(scene, fall->top, fall->carved);
            scene.footpaths.waterfalls.push_back(*fall);
            park::show_park_particles(scene.particles, rct3::park::waterfall_spray(scene.footpaths.waterfalls), &park::ParkParticles::sprays);
            park::rebuild_ground(device, scene, assets);
        }

        /// The fence round the park's land built again, and the staff kept to that land, after it changed.
        /// shortcut: the old panels stay hidden among the objects; drop them when objects can be taken out.
        void land_owned_changed(park::ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            simulation::keep_staff_in(scene.sim, scene.grid);
            wait_for_build(scene);
            for (save::model::PlacedObject& o : scene.objects)
                if (std::ranges::contains(scene.fence_panels, o.id))
                    o.hidden = true;
            scene.fence_panels.clear();
            const save::model::Park& park = scene.park_model.park;
            rct3::park::BoundaryFence fence = rct3::park::boundary_fence(park.fence_symbol, park.fence_overlay, scene.grid, scene.footpaths.paths, last_id(scene));
            if (!fence.pieces.empty() && load_object_model(scene, scene.placing.models, assets, fence.pieces.front()))
            {
                const auto& colours = scene.models.at(rct3::park::object::model_key(fence.pieces.front())).default_colours;
                for (save::model::PlacedObject& panel : fence.pieces)
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
        void buy(park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over)
        {
            LandTools& l = scene.land;
            const auto spot = over ? std::nullopt : ground_spot(scene, at, size);
            if (!spot)
                return;
            const save::model::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            if (l.tile == tile || !scene.grid.contains(tile.col, tile.row))
                return;
            l.tile = tile;
            const save::model::Finance& prices = scene.park_model.finance;
            const units::Money price { l.buying == rct3::park::LandBuy::Land ? prices.land_buy_cost : prices.rights_buy_cost };
            const rct3::park::LandBought bought = rct3::park::buy_land(scene.grid, rct3::park::ground::brush_tiles(tile, rct3::park::ground::brush_across(l.brush, scene.grid.tile_size_x), scene.grid.width, scene.grid.height), l.buying, price, scene.park.cash);
            if (bought.short_of_cash)
                simulation::park::cannot_pay(scene.park);
            if (bought.tiles == 0)
                return;
            // buy_land took no more than the cash could pay for.
            simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, bought.spent, simulation::park::LedgerLine::LandPurchase, false);
            park::float_money(scene, park::tile_point(scene.grid, tile), -bought.spent.value);
            land_owned_changed(scene, assets);
        }

        void read_surfaces(LandTools& l, const ui::InterfaceAssets& assets)
        {
            if (std::exchange(l.surfaces_read, true) || !assets.install)
                return;
            for (const std::string_view name : rct3::park::ground::kGroundFiles)
            {
                const auto found = assets.install->find(name);
                if (!found)
                    continue;
                for (const auto& [paint, into] : { std::pair { rct3::park::GroundPaint::Surface, &l.surfaces }, std::pair { rct3::park::GroundPaint::Cliff, &l.cliffs } })
                {
                    auto more = rct3::park::ground::ground_surfaces(*found, paint);
                    if (!more)
                    {
                        logging::logger("park").warning("{}: {}", name, more.error());
                        break;
                    }
                    into->insert(into->end(), std::make_move_iterator(more->begin()), std::make_move_iterator(more->end()));
                }
            }
        }

        std::optional<ui::screens::build::LandPanel&> panel_of(const park::ParkScene& scene)
        {
            return scene.desktop->root().find_as<ui::screens::build::LandPanel>(kPanelName);
        }

        /// The panel with the tabs of the tool out: shaping's, or painting's with the surfaces read.
        void open_panel(park::ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            const LandTools& l = scene.land;
            const ui::StringTable& strings = *assets.strings;
            auto panel = panel_of(scene);
            if (!panel)
                panel = scene.desktop->root().add<ui::screens::build::LandPanel>(std::string(kPanelName), ui::game_skins::land_panel_style(assets.palette), strings);
            std::vector<ui::screens::build::LandTab> tabs;
            ui::screens::build::LandPick chosen;
            if (l.tool == LandTools::Tool::Buy)
            {
                ui::screens::build::LandTab buying { "ParkInfoIcon_BuyLand", std::u16string(strings.get("Park_BuyLandHelp")), 2, { } };
                buying.tools.push_back({ "ParkInfoIcon_BuyLand", std::u16string(strings.get("Park_BuyLandHelp")), true });
                buying.tools.push_back({ "ParkInfoIcon_BuyConstructionRites", std::u16string(strings.get("Park_BuyConstructionRitesHelp")), true });
                tabs.push_back(std::move(buying));
                chosen = { 0, l.buying == rct3::park::LandBuy::Rights ? 1U : 0U };
            }
            else if (l.tool == LandTools::Tool::Paint)
            {
                ui::screens::build::LandTab base { "Land_Brush_Paint_Base", std::u16string(strings.get("Land_Brush_Paint_BaseHelp")), 4, { } };
                for (const rct3::park::ground::GroundSurface& s : l.surfaces)
                    base.tools.push_back({ s.icon, std::u16string(strings.get(s.help)), true });
                tabs.push_back(std::move(base));
                ui::screens::build::LandTab cliff { "Land_Brush_Paint_Cliff", std::u16string(strings.get("Land_Brush_Paint_CliffHelp")), 5, { } };
                for (const rct3::park::ground::GroundSurface& s : l.cliffs)
                    cliff.tools.push_back({ s.icon, std::u16string(strings.get(s.help)), true });
                tabs.push_back(std::move(cliff));
                chosen = l.cliff ? ui::screens::build::LandPick { kCliffTab, *l.cliff } : ui::screens::build::LandPick { 0, l.surface };
            }
            else
            {
                tabs.push_back(tab_of(strings, "Land_Vertex", 2, { "Land_Vertex_Neighbour", "Land_Vertex_Neighbour2", "Land_Vertex_Freeform", "Land_Vertex_Freeform2", "Land_Vertex_Quantised1", "Land_Vertex_Quantised2" }, kTileShapings.size() * 2));
                tabs.push_back(tab_of(strings, "Land_Brush_Height_Smooth", 3, { "Land_Brush_Height_Smooth_Average", "Land_Brush_Height_Smooth_FlatMove", "Land_Brush_Height_Smooth_Flat", "Land_Brush_Height_Smooth_Flat1", "Land_Brush_Height_Smooth_Round", "Land_Brush_Height_Smooth_Square" }, kSmoothTools.size()));
                ui::screens::build::LandTab brushes = tab_of(strings, "Land_Brush_Height_Raise", 4, { "Land_Brush_Height_Raise_Ridge", "Land_Brush_Height_Raise_Mountain", "Land_Brush_Height_Raise_Mesa", "Land_Brush_Height_Raise_Hill", "Land_Brush_Height_Lower_Canyon", "Land_Brush_Height_Lower_Ditch", "Land_Brush_Height_Lower_Crater" }, kShapeBrushes.size());
                brushes.help = std::u16string(strings.get("Rct3pp_LandShapeHelp"));
                tabs.push_back(std::move(brushes));
                ui::screens::build::LandTab mode = tab_of(strings, "Land_Brush_Height_Mode", 1, { "Land_Brush_Height_Mode_Spray", "Land_Brush_Height_Mode_Drag" }, 2);
                mode.setting = true;
                mode.on = l.spray ? 0 : 1;
                mode.number = "Land_Brush_Height_Mode_SpraySpeed";
                mode.number_help = std::u16string(strings.get("Land_Brush_Height_Mode_SpraySpeedHelp"));
                mode.number_value = l.spray_speed;
                mode.number_most = rct3::park::ground::kFastestSpray;
                tabs.push_back(std::move(mode));
                chosen = { kTileTab, l.tile_tool };
                if (l.smoothing)
                    chosen = { kSmoothTab, *l.smoothing };
                else if (l.shape)
                    chosen = { kBrushTab, static_cast<std::size_t>(std::ranges::find(kShapeBrushes, *l.shape) - kShapeBrushes.begin()) };
            }
            panel->show(std::move(tabs), chosen, l.brush, rct3::park::ground::kLargestBrush);
        }
    } // namespace

    void toggle_land_tool(park::ParkScene& scene, LandTools::Tool tool, const ui::InterfaceAssets& assets)
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

    void buy_land_tool(park::ParkScene& scene, rct3::park::LandBuy what, const ui::InterfaceAssets& assets)
    {
        stop_land_tools(scene);
        scene.land.tool = LandTools::Tool::Buy;
        scene.land.buying = what;
        open_panel(scene, assets);
    }

    void stop_land_tools(park::ParkScene& scene)
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

    bool land_tool(const park::ParkScene& scene)
    {
        return scene.land.tool != LandTools::Tool::None;
    }

    bool delete_waterfall(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size)
    {
        if (scene.land.tool != LandTools::Tool::Waterfall)
            return false;
        const auto spot = ground_spot(scene, at, size);
        if (!spot)
            return false;
        const save::model::TerrainGrid& g = scene.grid;
        auto& falls = scene.footpaths.waterfalls;
        const auto fall = rct3::park::waterfall_at(falls, g.origin_x + spot->x * g.tile_size_x, g.origin_z + spot->y * g.tile_size_z);
        if (!fall)
            return false;
        const save::model::Waterfall& f = falls[*fall];
        const auto col = static_cast<int>(std::floor(((f.lip_left.x + f.lip_right.x) / 2 - g.origin_x) / g.tile_size_x - f.flow.x / 2));
        const auto row = static_cast<int>(std::floor(((f.lip_left.z + f.lip_right.z) / 2 - g.origin_z) / g.tile_size_z - f.flow.z / 2));
        // The ground its spillway was cut from goes back, so must be the park's.
        const auto owned = [&](save::model::Tile t) { return !g.contains(t.col, t.row) || rct3::park::owned_outright(g.at(t.col, t.row)); };
        if (!owned({ col, row }) || !std::ranges::all_of(f.carved, [&](const save::model::CarvedTile& c) { return owned(c.tile); }))
        {
            say(scene, rct3::park::kWaterfallDeleteNotOwned);
            return true;
        }
        save::model::TerrainGrid ground = g;
        rct3::park::restore_spillway(ground, f);
        const save::model::WorldPoint3 top = f.top;
        falls.erase(falls.begin() + static_cast<std::ptrdiff_t>(*fall));
        park::show_park_particles(scene.particles, rct3::park::waterfall_spray(falls), &park::ParkParticles::sprays);
        start_stroke(scene);
        take_ground(device, scene, assets, std::move(ground), false);
        end_stroke(scene);
        refit_lake(scene, top, { });
        park::rebuild_ground(device, scene, assets);
        return true;
    }

    void remove_lake(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Tile tile)
    {
        save::model::TerrainGrid ground = scene.grid;
        if (!rct3::park::water::remove_lake(scene.footpaths.water, scene.footpaths.waterfalls, ground, tile))
            return;
        park::show_park_particles(scene.particles, rct3::park::waterfall_spray(scene.footpaths.waterfalls), &park::ParkParticles::sprays);
        start_stroke(scene);
        take_ground(device, scene, assets, std::move(ground), false);
        end_stroke(scene);
    }

    void remove_falls(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const save::model::WorldPoint3> tops)
    {
        auto& falls = scene.footpaths.waterfalls;
        save::model::TerrainGrid ground = scene.grid;
        std::vector<save::model::WorldPoint3> gone;
        std::erase_if(falls, [&](const save::model::Waterfall& f) {
            if (std::ranges::none_of(tops, [&](const save::model::WorldPoint3& p) { return p.x == f.top.x && p.y == f.top.y && p.z == f.top.z; }))
                return false;
            rct3::park::restore_spillway(ground, f);
            gone.push_back(f.top);
            return true;
        });
        if (gone.empty())
            return;
        park::show_park_particles(scene.particles, rct3::park::waterfall_spray(falls), &park::ParkParticles::sprays);
        start_stroke(scene);
        take_ground(device, scene, assets, std::move(ground), false);
        end_stroke(scene);
        // Each lake draws back out of the spillway it had spread into.
        for (const save::model::WorldPoint3& top : gone)
            refit_lake(scene, top, { });
        park::rebuild_ground(device, scene, assets);
    }

    void update_land_tools(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, units::Seconds elapsed)
    {
        LandTools& l = scene.land;
        if (l.rumble)
            l.rumble = std::max(kRumbleQuiet, *l.rumble - elapsed.value / kRumbleFade.value);
        if (auto panel = panel_of(scene); panel && (l.tool == LandTools::Tool::Shape || l.tool == LandTools::Tool::Paint || l.tool == LandTools::Tool::Buy))
        {
            if (panel->take_closed())
            {
                stop_land_tools(scene); // Closed by the player
                return;
            }
            l.brush = panel->brush();
            if (l.tool == LandTools::Tool::Shape)
                l.spray_speed = panel->number(kModeTab);
            if (const auto pick = panel->take_pick(); pick && l.tool == LandTools::Tool::Buy)
            {
                l.tile.reset();
                l.buying = pick->tool == 1 ? rct3::park::LandBuy::Rights : rct3::park::LandBuy::Land;
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
            else if (pick && l.tool == LandTools::Tool::Shape && pick->tab == kModeTab)
                l.spray = pick->tool == 0;
            else if (pick && l.tool == LandTools::Tool::Shape)
            {
                l.tile.reset();
                l.smoothing.reset();
                if (pick->tab == kSmoothTab && pick->tool < kSmoothTools.size())
                    l.smoothing = pick->tool;
                if (pick->tab == kTileTab && pick->tool < kTileShapings.size() * 2)
                    l.tile_tool = pick->tool;
                if (pick->tab == kBrushTab && pick->tool < kShapeBrushes.size())
                    l.shape = kShapeBrushes[pick->tool];
                else
                    l.shape.reset();
            }
        }
        if (l.tool == LandTools::Tool::None || !down)
        {
            end_stroke(scene);
            l.tile.reset();
            // Which way the pointer goes over the ground, read again each time it has gone a little way.
            if (l.tool != LandTools::Tool::Shape || !l.shape || over)
                l.travel_from.reset();
            else if (!l.travel_from || glm::distance(glm::vec2 { at.x, at.y }, l.travel_from->first) > kTravelSample)
            {
                if (const auto spot = ground_spot(scene, at, size))
                {
                    if (l.travel_from && *spot != l.travel_from->second)
                        l.travel = glm::normalize(*spot - l.travel_from->second);
                    l.travel_from = std::pair { glm::vec2 { at.x, at.y }, *spot };
                }
            }
        }
        else if (l.tool == LandTools::Tool::Shape && l.smoothing)
            smooth(device, scene, assets, at, size, over, elapsed);
        else if (l.tool == LandTools::Tool::Shape && l.shape)
            brush_shape(device, scene, assets, at, size, over, elapsed);
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
