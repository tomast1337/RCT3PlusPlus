// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/pool_tools.hpp"

#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/object/pick.hpp"
#include "park/object/placement.hpp"
#include "park/pools/pools.hpp"
#include "park/tile_shaping.hpp"
#include "profiling/profiler.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"
#include "simulation/park/messages.hpp"
#include "ui/extra_skins.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/pool_panel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ranges>
#include <utility>

namespace rct3::scene
{

    namespace
    {
        constexpr glm::vec4 kMarkColour { 0.3f, 0.75f, 1.0f, 0.9f };
        constexpr glm::vec4 kRefusedColour { 1.0f, 0.35f, 0.3f, 0.9f };
        constexpr std::string_view kPanelName = "pool_panel";
        constexpr float kPixelsPerBoardUnit = 10; ///< Of a drag up, to raise a high diving board a unit
        constexpr std::int32_t kHighestBoard = 30;
        constexpr float kArrowLift = 0.04f; ///< Of the arrow over the next pool path piece, so it shows over the piece, as on the path tool

        std::optional<ui::screens::PoolPanel&> panel_of(const park::ParkScene& scene)
        {
            return scene.desktop->root().find_as<ui::screens::PoolPanel>(kPanelName);
        }

        /// What the pool panel offers to pick, each in its order.
        struct Offered
        {
            std::vector<const rct3::park::BuildItem*> extras; ///< The pool type's extras, its high diving board and spa pool last
            std::vector<const rct3::park::BuildItem*> rooms;  ///< Every changing room
            std::vector<const rct3::park::BuildItem*> stairs; ///< On the paths page: the ramps, its spiral staircase last
        };

        /// What the pool panel offers; nothing while the build items are still being read. An extra that changes
        /// the level at its side is a ramp, offered with the ways up rather than the extras, as in the game.
        Offered offered(const park::ParkScene& scene)
        {
            Offered out;
            const auto& items = scene.build.items;
            if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return out;
            const rct3::park::BuildItem* spa = nullptr;
            const rct3::park::BuildItem* dive = nullptr;
            const rct3::park::BuildItem* stairs = nullptr;
            for (const rct3::park::BuildItem& i : items.get())
            {
                if (i.list != rct3::park::BuildList::Pools)
                    continue;
                // shortcut: the default pool type only; offer the others once the panel can choose a type.
                if (i.kind == rct3::park::BuildKind::PoolRoom)
                    out.rooms.push_back(&i);
                else if (i.overlay == rct3::park::kPoolOverlay && i.kind == rct3::park::BuildKind::PoolExtra)
                    (rct3::park::pools::pool_extra_fit(i.pool_extra).step > 0 ? out.stairs : out.extras).push_back(&i);
                else if (i.overlay == rct3::park::kPoolOverlay && i.symbol == rct3::park::kPoolJacuzzi)
                    spa = &i;
                else if (i.overlay == rct3::park::kPoolOverlay && i.symbol == rct3::park::kPoolHighDive)
                    dive = &i;
                else if (i.overlay == rct3::park::kPoolOverlay && i.symbol == rct3::park::kPoolStairs)
                    stairs = &i;
            }
            for (const rct3::park::BuildItem* last : { dive, spa })
                if (last != nullptr)
                    out.extras.push_back(last);
            if (stairs != nullptr)
                out.stairs.push_back(stairs);
            return out;
        }

        /// Follows the pool panel: what the player chose there goes out as the tool.
        void follow_panel(park::ParkScene& scene)
        {
            auto panel = panel_of(scene);
            if (!panel)
                return;
            if (panel->take_closed())
                return stop_pooling(scene);
            if (!panel->showing())
                return;
            const ui::screens::PoolEditor& e = panel->editor();
            scene.pooling.height = e.snap ? std::nullopt : std::optional(e.height);
            scene.pooling.edge = static_cast<rct3::park::pools::PoolEdge>(std::clamp(e.edge, 0, 3));
            if (e.grid != scene.pooling.grid)
                scene.show_grid = scene.pooling.grid = e.grid;
            if (!panel->take_change())
                return;
            const Offered offer = offered(scene);
            const rct3::park::BuildItem* item = nullptr;
            if (const auto p = panel->placing())
            {
                const auto& from = p->first == ui::screens::PoolPage::Extras ? offer.extras : p->first == ui::screens::PoolPage::Paths ? offer.stairs : offer.rooms;
                item = p->second < from.size() ? from[p->second] : nullptr;
            }
            else if (const auto& items = scene.build.items; items.valid() && items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                // The paths page's tool lays the piece of the slope chosen.
                const rct3::park::PathSlope slope = scene.pooling.trail.slope;
                const std::string_view path = slope == rct3::park::PathSlope::Flat ? rct3::park::kPoolPath : slope == rct3::park::PathSlope::Up || slope == rct3::park::PathSlope::Down ? rct3::park::kPoolPathSlope : rct3::park::kPoolPathStairs;
                constexpr std::array<std::string_view, 3> kTools { "", rct3::park::kPoolTiles, rct3::park::kPoolDelete };
                const std::string_view symbol = e.mode == ui::screens::PoolMode::Water ? rct3::park::kPoolWater[static_cast<std::size_t>(std::clamp(e.depth, 1, 3) - 1)] : e.mode == ui::screens::PoolMode::Path ? path : kTools[std::to_underlying(e.mode)];
                const auto found = std::ranges::find_if(items.get(), [&](const rct3::park::BuildItem& i) { return i.list == rct3::park::BuildList::Pools && i.overlay == rct3::park::kPoolOverlay && i.symbol == symbol; });
                item = found != items.get().end() ? &*found : nullptr;
            }
            if (item != nullptr)
                (void)start_pooling(scene, *item);
        }

        /// The area marked, as its least and greatest column and row.
        std::array<std::int32_t, 4> marked(save::model::Tile a, save::model::Tile b)
        {
            return { std::min(a.col, b.col), std::min(a.row, b.row), std::max(a.col, b.col), std::max(a.row, b.row) };
        }

        /// The pools with the area marked laid or taken up, and what that costs; below zero, money back.
        std::pair<rct3::park::pools::PoolMap, std::int64_t> marked_pools(const park::ParkScene& scene, save::model::Tile a, save::model::Tile b)
        {
            const rct3::park::BuildItem& item = *scene.pooling.item;
            rct3::park::pools::PoolMap map = scene.pools;
            const auto prices = std::span<const std::int32_t, 18>(item.pool_prices.data(), 18);
            // Where squares are stacked, the one on top where the drag began is taken up, and those level with it.
            const auto begun = scene.pools.at(a.col, a.row);
            if (item.symbol == rct3::park::kPoolDelete)
            {
                const std::int64_t cost = rct3::park::pools::take_up_pool(map, prices, a, b, begun ? std::optional(begun->surface) : std::nullopt);
                return { std::move(map), cost };
            }
            // Raised or lowered with Shift, the surface is where Shift took it. Snapped to the ground, it is the
            // ground's where the drag began, or the surface of the pool begun there; else the height the pool panel
            // sets. Raised, a pool stands over the ground, which stays where it is (pools_changed).
            const std::int32_t surface = scene.pooling.lifted.value_or(scene.pooling.height.value_or(begun ? begun->surface : static_cast<std::int32_t>(std::lround(scene.grid.at(a.col, a.row).max_height()))));
            const auto water = std::ranges::find(rct3::park::kPoolWater, item.symbol);
            const rct3::park::pools::PoolKind kind = water != rct3::park::kPoolWater.end() ? rct3::park::pools::PoolKind::Water
                : item.symbol == rct3::park::kPoolJacuzzi                                  ? rct3::park::pools::PoolKind::Jacuzzi
                : item.symbol == rct3::park::kPoolHighDive                                 ? rct3::park::pools::PoolKind::HighDive
                : item.symbol == rct3::park::kPoolStairs                                   ? rct3::park::pools::PoolKind::Stairs
                : item.symbol == rct3::park::kPoolPath                                     ? rct3::park::pools::PoolKind::Path
                : item.symbol == rct3::park::kPoolPathSlope                                ? rct3::park::pools::PoolKind::PathSlope
                : item.symbol == rct3::park::kPoolPathStairs                               ? rct3::park::pools::PoolKind::PathStairs
                                                                                           : rct3::park::pools::PoolKind::Tile;
            const bool climbs = kind == rct3::park::pools::PoolKind::HighDive || kind == rct3::park::pools::PoolKind::Stairs;
            const bool turned = climbs || kind == rct3::park::pools::PoolKind::PathSlope || kind == rct3::park::pools::PoolKind::PathStairs;
            // Paved squares take the normal depth, the one the pool type prices them at.
            // Paths go no deeper than their surface.
            const std::int32_t depth = water != rct3::park::kPoolWater.end() ? static_cast<std::int32_t>(water - rct3::park::kPoolWater.begin()) + 1 : rct3::park::pools::is_pool_path(kind) ? 0 : 2;
            std::vector<rct3::park::pools::PoolSquare> squares;
            const auto square = [&](std::int32_t col, std::int32_t row) { squares.push_back({ .col = col, .row = row, .kind = kind, .surface = surface, .depth = depth, .board = climbs ? scene.pooling.board : 0, .side = turned ? scene.pooling.side : rct3::park::Side { }, .edge = scene.pooling.edge }); };
            const auto area = marked(a, b);
            if (kind == rct3::park::pools::PoolKind::Path)
            {
                // Along the row where the drag began, then down the column where it is.
                // shortcut: the marks and the check of the land owned cover the drag's whole rectangle; mark the L
                // alone once it misleads.
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    square(col, a.row);
                for (std::int32_t row = area[1]; row <= area[3]; ++row)
                    if (row != a.row)
                        square(b.col, row);
            }
            else
                for (std::int32_t row = area[1]; row <= area[3]; ++row)
                    for (std::int32_t col = area[0]; col <= area[2]; ++col)
                        square(col, row);
            const std::int64_t cost = rct3::park::pools::lay_pool(map, prices, squares);
            return { std::move(map), cost };
        }

        /// The station platform squares of the pool rides, those ending in a pool or back at a station, whose decks a
        /// pool path trail may start from.
        std::vector<rct3::park::pools::SlidePlatform> slide_platforms(const park::ParkScene& scene)
        {
            std::vector<rct3::park::pools::SlidePlatform> out;
            for (std::size_t i = 0; i < scene.rides.rides.size() && i < scene.courses.size(); ++i)
                if (const save::model::Ride& ride = scene.rides.rides[i]; !ride.base.demolished && ride.track && i < scene.track_rules.size() && scene.track_rules[i].pool_ride)
                    for (const save::model::TrackPiece& p : ride.track->pieces)
                        if (p.platform)
                            out.push_back({ .col = p.platform->tile_x, .row = p.platform->tile_z, .height = p.platform->height, .turn = rct3::park::ModelTurn::from(p.platform->direction) });
            return out;
        }

        /// Where the pointer is on the grid, in tiles with fractions: on the top of a pool square or of one of
        /// `decks` where one is in the way, as the player sees it, else on the ground; the ground under the pools
        /// is dug down out of sight.
        std::optional<glm::vec2> pointed_spot(const park::ParkScene& scene, ui::Point at, ui::Point size, std::span<const rct3::park::pools::SlidePlatform> decks)
        {
            if (size.x <= 0 || size.y <= 0)
                return std::nullopt;
            const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            std::optional<float> nearest = rct3::park::object::ground_hit(ray, scene.grid);
            const save::model::TerrainGrid& g = scene.grid;
            const auto top = [&](std::int32_t col, std::int32_t row, std::int32_t height) {
                const float along = (static_cast<float>(height) - ray.origin.y) / ray.direction.y;
                const glm::vec3 hit = ray.origin + ray.direction * along;
                const bool on = std::floor((hit.x - g.origin_x) / g.tile_size_x) == static_cast<float>(col) && std::floor((hit.z - g.origin_z) / g.tile_size_z) == static_cast<float>(row);
                if (along > 0 && on && (!nearest || along < *nearest))
                    nearest = along;
            };
            for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
                top(s.col, s.row, s.surface);
            for (const rct3::park::pools::SlidePlatform& d : decks)
                top(d.col, d.row, d.height);
            if (!nearest)
                return std::nullopt;
            const glm::vec3 hit = ray.origin + ray.direction * *nearest;
            const glm::vec2 spot { (hit.x - g.origin_x) / g.tile_size_x, (hit.z - g.origin_z) / g.tile_size_z };
            return g.contains(static_cast<int>(std::floor(spot.x)), static_cast<int>(std::floor(spot.y))) ? std::optional(spot) : std::nullopt;
        }

        /// Draws the marks over the area, at the pools' surface where squares are, and remembers them.
        void draw_marks(render::Device& device, park::ParkScene& scene, std::optional<std::array<std::int32_t, 4>> marks, rct3::park::TilePart part, bool ok)
        {
            const std::int32_t look = std::to_underlying(part) * 2 + (ok ? 1 : 0);
            if (marks == scene.pooling.drawn && look == scene.pooling.drawn_look)
                return;
            rct3::park::TerrainMesh mesh;
            if (marks)
            {
                // The ground under the squares is dug out of sight: the marks are drawn as on their surface.
                std::vector<std::pair<save::model::Tile, std::array<float, 4>>> dug;
                for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
                    if (s.col >= (*marks)[0] && s.col <= (*marks)[2] && s.row >= (*marks)[1] && s.row <= (*marks)[3])
                    {
                        auto& corners = scene.grid.at(s.col, s.row).corners;
                        dug.emplace_back(save::model::Tile { s.col, s.row }, corners);
                        corners.fill(static_cast<float>(s.surface));
                    }
                rct3::park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], part, ok ? kMarkColour : kRefusedColour);
                // Backwards: a tile with squares stacked is in it more than once.
                for (const auto& [tile, corners] : std::views::reverse(dug))
                    scene.grid.at(tile.col, tile.row).corners = corners;
            }
            park::set_view_lines(device, scene.pool_marks, mesh);
            scene.pooling.drawn = marks;
            scene.pooling.drawn_look = look;
        }

        /// The object `symbol` of `overlay` alone, slowly turning, for the panel's disc; none for an empty symbol.
        void turn_preview(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, std::string_view symbol, std::string_view overlay)
        {
            park::PoolTool& t = scene.pooling;
            if (symbol.empty())
            {
                t.preview_of.clear();
                t.preview = { };
                return;
            }
            save::model::PlacedObject alone { .symbol = std::string(symbol), .overlay = std::string(overlay), .force_absolute_height = true };
            if (t.preview_of != symbol && load_object_model(scene, scene.placing.models, assets, alone))
            {
                t.preview_of = symbol;
                t.preview = rct3::park::object::object_mesh(std::span(&alone, 1), scene.models, scene.grid);
                ghost_textures(device, scene, t.preview);
            }
            if (t.preview_of == symbol)
                t.preview_camera = rct3::park::framed(rct3::park::object::object_corners(alone, scene.models, scene.grid, scene.fitted), kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
        }

        /// Draws `ghost` see-through, tinted as refused unless `ok`; none takes the ghost away.
        void show_ghost(render::Device& device, park::ParkScene& scene, const std::optional<save::model::PlacedObject>& ghost, bool ok)
        {
            park::PoolTool& t = scene.pooling;
            const bool same = ghost.has_value() == t.ghost_of.has_value() && (!ghost || (ghost->symbol == t.ghost_of->symbol && ghost->col == t.ghost_of->col && ghost->row == t.ghost_of->row && ghost->base_height == t.ghost_of->base_height && ghost->rotation == t.ghost_of->rotation));
            if (same && ok == t.ghost_ok)
                return;
            t.ghost_of = ghost;
            t.ghost_ok = ok;
            t.ghost = ghost ? rct3::park::object::object_mesh(std::span(&*ghost, 1), scene.models, scene.grid) : rct3::park::object::ObjectMesh { };
            for (rct3::park::GroundVertex& v : t.ghost.vertices)
                v.colour *= ok ? kGhostTint : kRefusedTint;
            ghost_textures(device, scene, t.ghost);
        }

        /// Puts the extra or changing room out with the pool tool where the pointer is, when it fits and can be
        /// paid for. A changing room goes on the land the park owns.
        void update_extra(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<glm::vec2> spot, bool down)
        {
            park::PoolTool& t = scene.pooling;
            const rct3::park::BuildItem& item = *t.item;
            const auto tile = spot ? std::optional(save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }) : std::nullopt;
            const rct3::park::pools::PoolExtraFit fit = rct3::park::pools::pool_extra_fit(item.pool_extra);
            const bool room = item.kind == rct3::park::BuildKind::PoolRoom;
            const auto fits_at = [&](rct3::park::Side side) { return tile && (room ? rct3::park::pools::pool_room_fits(scene.pools, tile->col, tile->row, side) : rct3::park::pools::pool_extra_fits(scene.pools, tile->col, tile->row, fit, side)); };
            // It takes the side nearest the pointer that it fits against, as a bench on a path does; the turn keys
            // turn it only where it fits every way or none.
            const auto nearest = tile ? rct3::park::object::nearest_side(*spot - glm::vec2 { tile->col, tile->row }, fits_at) : std::nullopt;
            const rct3::park::Side side = nearest && !std::ranges::all_of(rct3::park::kSides, fits_at) ? *nearest : t.side;
            const bool fits = fits_at(side) && (!room || rct3::park::owned_outright(scene.grid.at(tile->col, tile->row)));
            const bool ok = fits && (!scene.park.cash || *scene.park.cash >= item.price);
            if (down && tile)
                t.corner = tile;
            else if (t.corner && !down)
            {
                // Let go: the extra or changing room goes in where the pointer is now. The panel and its tool stay
                // out, as in the original.
                t.corner.reset();
                if (ok && room)
                {
                    scene.pools.rooms.push_back({ .symbol = item.symbol, .overlay = item.overlay, .col = tile->col, .row = tile->row, .side = side });
                    ++scene.built;
                    pools_changed(device, scene, assets);
                }
                else if (ok && simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, item.price, simulation::park::LedgerLine::RideConstruction, false))
                {
                    save::model::PlacedObject model { .symbol = item.symbol, .overlay = item.overlay };
                    const bool loaded = load_object_model(scene, scene.placing.models, assets, model);
                    scene.pools.extras.push_back(
                        {
                            .symbol = item.symbol,
                            .overlay = item.overlay,
                            .col = tile->col,
                            .row = tile->row,
                            .side = side,
                            .refund = loaded ? scene.models.at(rct3::park::object::model_key(model)).removal_cost : 0,
                            .fit = fit,
                        });
                    ++scene.built;
                    if (item.price != 0)
                        park::float_money(scene, park::tile_point(scene.grid, *tile), -item.price);
                    pools_changed(device, scene, assets);
                }
                else if (fits)
                    simulation::park::cannot_pay(scene.park);
            }
            draw_marks(device, scene, tile ? std::optional(marked(*tile, *tile)) : std::nullopt, rct3::park::TilePart::Whole, ok);
            turn_preview(device, scene, assets, item.symbol, item.overlay);
            // The ghost: the extra or room as it would stand there, on its square's surface, else on the ground.
            std::optional<save::model::PlacedObject> ghost;
            if (tile)
            {
                rct3::park::pools::PoolMap alone { .squares = scene.pools.squares };
                if (room)
                    alone.rooms.push_back({ .symbol = item.symbol, .overlay = item.overlay, .col = tile->col, .row = tile->row, .side = side });
                else
                    alone.extras.push_back({ .symbol = item.symbol, .overlay = item.overlay, .col = tile->col, .row = tile->row, .side = side, .fit = fit });
                ghost = rct3::park::pools::pool_objects(alone, item.overlay, 0).back();
                const bool on_square = room ? fits : scene.pools.at(tile->col, tile->row).has_value();
                if (!on_square)
                    ghost->base_height = static_cast<std::int32_t>(std::lround(scene.grid.at(tile->col, tile->row).max_height()));
                if (!load_object_model(scene, scene.placing.models, assets, *ghost))
                    ghost.reset();
            }
            show_ghost(device, scene, ghost, ok);
        }

        /// Whether the area may be worked on: all of it land the park owns outright, and paid for.
        /// shortcut: what stands on the land is not checked; refuse paths and objects once pools can clash.
        bool allowed(const park::ParkScene& scene, const std::array<std::int32_t, 4>& area, std::int64_t cost)
        {
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    if (!scene.grid.contains(col, row) || !rct3::park::owned_outright(scene.grid.at(col, row)))
                        return false;
            return !scene.park.cash || cost <= 0 || *scene.park.cash >= cost;
        }

        /// Takes the work shown out of the pools again, and puts back the ground it dug. True when there was any.
        bool put_back(park::ParkScene& scene)
        {
            park::PoolTool& t = scene.pooling;
            if (!t.before)
                return false;
            scene.pools = std::move(*t.before);
            t.before.reset();
            for (const auto& [tile, corners] : t.dug)
                scene.grid.at(tile.col, tile.row).corners = corners;
            t.dug.clear();
            return true;
        }

    } // namespace

    namespace
    {
        /// Whether `item` is the paths page's tool.
        bool laying_paths(const rct3::park::BuildItem& item)
        {
            return item.symbol == rct3::park::kPoolPath || item.symbol == rct3::park::kPoolPathSlope || item.symbol == rct3::park::kPoolPathStairs;
        }

        /// Acts on the paths page's control pressed: turns and slopes change the trail, and laying or taking a
        /// piece away does it at the trail's end, paid for or refunded, as the path panel's controls do. A piece
        /// the park cannot pay for, or off the land it owns, is refused. Then tells the page what can be used.
        void follow_path_controls(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            auto panel = panel_of(scene);
            park::PoolTool& t = scene.pooling;
            if (!panel || !panel->showing())
                return;
            const auto control = panel->take_path_control();
            const rct3::park::PathStep step = control ? rct3::park::press(t.trail, static_cast<rct3::park::PathControl>(*control)) : rct3::park::PathStep::None;
            if (step != rct3::park::PathStep::None && t.item && t.item->pool_prices.size() == 18)
            {
                const auto prices = std::span<const std::int32_t, 18>(t.item->pool_prices.data(), 18);
                const bool redraw = put_back(scene);
                t.working.reset();
                std::optional<save::model::Tile> done;
                std::int64_t cost = 0;
                if (const auto piece = rct3::park::pools::next_pool_path(t.trail); step == rct3::park::PathStep::Lay && piece)
                {
                    const save::model::Tile spot { piece->square.col, piece->square.row };
                    rct3::park::pools::PoolMap map = scene.pools;
                    cost = rct3::park::pools::lay_pool(map, prices, std::span(&piece->square, 1));
                    if (allowed(scene, marked(spot, spot), cost))
                    {
                        t.trail_ground.resize(t.trail.trail.size());
                        t.trail_ground.push_back(scene.grid.at(spot.col, spot.row).corners);
                        scene.pools = std::move(map);
                        t.trail.trail.push_back({ .spot = spot, .laid = 1, .height = piece->end });
                        t.trail.on_slope = piece->square.kind != rct3::park::pools::PoolKind::Path;
                        done = spot;
                    }
                    else if (allowed(scene, marked(spot, spot), 0))
                        simulation::park::cannot_pay(scene.park);
                }
                else if (step == rct3::park::PathStep::Remove && !t.trail.trail.empty() && t.trail.trail.back().laid != 0)
                {
                    // The trail steps back; the piece goes when it is still a pool path, the ground put back as it was
                    // before the trail laid it, else left at its surface.
                    const save::model::Tile spot = t.trail.trail.back().spot;
                    t.trail.trail.pop_back();
                    const auto ground = t.trail_ground.size() > t.trail.trail.size() ? t.trail_ground[t.trail.trail.size()] : std::nullopt;
                    t.trail_ground.resize(t.trail.trail.size());
                    if (const auto square = scene.pools.at(spot.col, spot.row); square && rct3::park::pools::is_pool_path(square->kind))
                    {
                        auto& corners = scene.grid.at(spot.col, spot.row).corners;
                        if (ground)
                            corners = *ground;
                        else if (square->ground)
                            corners = *square->ground;
                        cost = rct3::park::pools::take_up_pool(scene.pools, prices, spot, spot, square->surface);
                        done = spot;
                    }
                    const auto back = t.trail.trail.empty() ? std::nullopt : scene.pools.at(t.trail.trail.back().spot.col, t.trail.trail.back().spot.row);
                    t.trail.on_slope = back && back->kind != rct3::park::pools::PoolKind::Path;
                }
                if (done)
                {
                    ++scene.built;
                    // Laying was allowed only when paid for; taking up pays back.
                    simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::RideConstruction, false);
                    if (cost != 0)
                        park::float_money(scene, park::tile_point(scene.grid, *done), -cost);
                    scene.supports.stale = true;
                }
                if (done || redraw)
                    pools_changed(device, scene, assets, done ? std::optional(rct3::park::TileRect { done->col, done->row, done->col + 1, done->row + 1 }.grown(1)) : std::nullopt);
                // Following, the camera glides to the trail's next square as the path panel's does.
                if (const auto next = rct3::park::next_spot(t.trail); done && next && panel->following())
                {
                    const std::int32_t height = t.trail.trail.empty() ? t.trail.start_height.value_or(0) : t.trail.trail.back().height;
                    scene.footpaths.camera_goal = glm::vec3 { scene.grid.world_x(next->col) + scene.grid.tile_size_x / 2, static_cast<float>(height), scene.grid.world_z(next->row) + scene.grid.tile_size_z / 2 };
                }
            }
            std::array<bool, ui::screens::kPoolPathControls> usable;
            usable.fill(true);
            const bool going = rct3::park::next_spot(t.trail).has_value();
            usable[std::to_underlying(rct3::park::PathControl::TurnLeft)] = going && rct3::park::can_turn(t.trail);
            usable[std::to_underlying(rct3::park::PathControl::TurnRight)] = going && rct3::park::can_turn(t.trail);
            usable[std::to_underlying(rct3::park::PathControl::Straight)] = going;
            usable[std::to_underlying(rct3::park::PathControl::Remove)] = !t.trail.trail.empty() && t.trail.trail.back().laid != 0;
            panel->set_path_controls(usable, std::to_underlying(rct3::park::slope_control(t.trail.slope)));
        }
    } // namespace

    void swap_pool_work(park::ParkScene& scene)
    {
        park::PoolTool& t = scene.pooling;
        if (!t.before)
            return;
        std::swap(scene.pools, *t.before);
        for (auto& [tile, corners] : t.dug)
            std::swap(scene.grid.at(tile.col, tile.row).corners, corners);
    }

    namespace
    {

        /// Lays `map`, the pools with the work done, in their place, keeping what to put back.
        void lay_work(park::ParkScene& scene, rct3::park::pools::PoolMap map)
        {
            park::PoolTool& t = scene.pooling;
            const auto area = marked(t.working->first, t.working->second);
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    t.dug.emplace_back(save::model::Tile { col, row }, scene.grid.at(col, row).corners);
            // The ground a square taken up had dug is put back.
            for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
                if (const auto now = map.at(s.col, s.row, s.surface); s.ground && (!now || now->surface != s.surface))
                    scene.grid.at(s.col, s.row).corners = *s.ground;
            t.before = std::exchange(scene.pools, std::move(map));
        }
    } // namespace

    bool start_pooling(park::ParkScene& scene, const rct3::park::BuildItem& item)
    {
        if ((item.kind != rct3::park::BuildKind::PoolSquare || item.pool_prices.size() != 18) && item.kind != rct3::park::BuildKind::PoolExtra && item.kind != rct3::park::BuildKind::PoolRoom)
            return false;
        const bool redraw = put_back(scene) || scene.pooling.redraw;
        scene.pooling = park::PoolTool { .item = item, .drawn = scene.pooling.drawn, .drawn_look = scene.pooling.drawn_look, .height = scene.pooling.height, .grid = scene.pooling.grid, .edge = scene.pooling.edge, .redraw = redraw, .side = scene.pooling.side, .preview_target = scene.pooling.preview_target, .preview_pixels = scene.pooling.preview_pixels, .trail = std::move(scene.pooling.trail), .trail_ground = std::move(scene.pooling.trail_ground) };
        return true;
    }

    void stop_pooling(park::ParkScene& scene)
    {
        if (auto panel = panel_of(scene))
            panel->hide();
        const bool redraw = put_back(scene) || scene.pooling.redraw;
        scene.pooling = park::PoolTool { .drawn = scene.pooling.drawn, .drawn_look = scene.pooling.drawn_look, .grid = scene.pooling.grid, .edge = scene.pooling.edge, .redraw = redraw, .side = scene.pooling.side, .preview_target = scene.pooling.preview_target, .preview_pixels = scene.pooling.preview_pixels }; // The marks go on the next frame
    }

    bool pool_panel_open(const park::ParkScene& scene)
    {
        const auto panel = panel_of(scene);
        return panel && panel->showing();
    }

    void toggle_pool_panel(render::Device& device, park::ParkScene& scene, ui::InterfaceAssets& assets, const ui::Currency& currency, std::optional<ui::screens::PoolPage> page)
    {
        auto panel = panel_of(scene);
        if (panel && panel->showing() && !page)
            return stop_pooling(scene);
        if (!panel)
        {
            // The extras' pictures live in an overlay of their own.
            constexpr std::array<std::string_view, 1> kIconFiles { "Pool/PoolIcons.common.ovl" };
            (void)ui::add_extra_skins(device, assets, kIconFiles);
            panel = scene.desktop->root().add<ui::screens::PoolPanel>(std::string(kPanelName), ui::game_skins::pool_panel_style(assets.palette), *assets.strings);
        }
        const Offered offer = offered(scene);
        // Each choice's help is its name, and its price where it has one.
        const auto choices = [&](const std::vector<const rct3::park::BuildItem*>& items) {
            std::vector<ui::screens::PoolChoice> out;
            for (const rct3::park::BuildItem* i : items)
                out.push_back({ i->icon, std::u16string(assets.strings->get(i->name_text)) + (i->price > 0 ? u" : " + ui::money_text(currency, i->price) : std::u16string { }) });
            return out;
        };
        panel->show(choices(offer.extras), choices(offer.rooms), choices(offer.stairs), page);
    }

    void update_pooling(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, bool shift, std::span<const ui::PlayCommand> commands)
    {
        follow_path_controls(device, scene, assets);
        follow_panel(scene);
        park::PoolTool& t = scene.pooling;
        // The pointer finds the park as it was before the work shown, which moves with it.
        swap_pool_work(scene);
        // The paths page's trails start from pool slides' platforms too.
        const std::vector<rct3::park::pools::SlidePlatform> decks = t.item && laying_paths(*t.item) ? slide_platforms(scene) : std::vector<rct3::park::pools::SlidePlatform> { };
        auto spot = t.item && !over ? pointed_spot(scene, at, size, decks) : std::nullopt;
        // On a slide's platform, a path starts beside it at its deck, off the side nearest the pointer that a
        // trail may start from, as the pinned controls do.
        std::optional<std::pair<save::model::Tile, std::int32_t>> deck_start;
        if (spot)
        {
            const save::model::Tile on { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
            const auto deck = scene.pools.at(on.col, on.row) ? decks.end() : std::ranges::find_if(decks, [&](const rct3::park::pools::SlidePlatform& d) { return d.col == on.col && d.row == on.row; });
            if (deck != decks.end())
                if (const auto side = rct3::park::object::nearest_side(*spot - glm::vec2 { on.col, on.row }, [&](rct3::park::Side side) { return rct3::park::pools::slide_path_start(scene.pools, *deck, side).has_value(); }))
                {
                    const auto [dc, dr] = side->step();
                    deck_start = { save::model::Tile { on.col + dc, on.row + dr }, *rct3::park::pools::slide_path_start(scene.pools, *deck, *side) };
                }
        }
        swap_pool_work(scene);
        // Held before a drag, Shift keeps the square under the pointer and raises or lowers its surface with the
        // pointer, as it lifts scenery; the drag then lays the squares at that height. Let go of before a drag, the
        // surface goes back.
        const bool squares = t.item && t.item->kind == rct3::park::BuildKind::PoolSquare;
        if (!shift && !t.corner)
        {
            t.lift.reset();
            t.lifted = deck_start ? std::optional(deck_start->second) : std::nullopt;
        }
        else if (shift && squares && !t.corner && size.x > 0 && size.y > 0)
        {
            if (!t.lift && spot)
            {
                const save::model::Tile tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) };
                const auto square = scene.pools.at(tile.col, tile.row);
                t.lift = park::PoolTool::Lift { .spot = *spot, .surface = t.height.value_or(square ? square->surface : static_cast<std::int32_t>(std::lround(scene.grid.at(tile.col, tile.row).max_height()))) };
            }
            if (t.lift)
            {
                const save::model::TerrainGrid& g = scene.grid;
                const glm::vec3 lift_at { g.origin_x + t.lift->spot.x * g.tile_size_x, static_cast<float>(t.lift->surface), g.origin_z + t.lift->spot.y * g.tile_size_z };
                const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
                if (const auto h = lifted_height(ray, lift_at, t.lift->surface))
                    t.lifted = std::clamp(*h, ui::screens::pool_panel::kHeightLowest, ui::screens::pool_panel::kHeightHighest);
                spot = t.lift->spot;
            }
        }
        for (const ui::PlayCommand c : commands)
        {
            if (c == ui::PlayCommand::TurnItemRight)
                t.side = t.side.right();
            else if (c == ui::PlayCommand::TurnItemLeft)
                t.side = t.side.left();
        }
        if (t.item && (t.item->kind == rct3::park::BuildKind::PoolExtra || t.item->kind == rct3::park::BuildKind::PoolRoom))
            return update_extra(device, scene, assets, spot, down);
        // The disc shows the piece the tool lays: the floor of a water square, the paving, or the extra laid as
        // a square; nothing while taking squares up.
        if (t.item)
        {
            const std::string_view s = t.item->symbol;
            const std::string_view piece = s == rct3::park::kPoolDelete ? "" : s == rct3::park::kPoolTiles ? "SolidTop" : s == rct3::park::kPoolJacuzzi ? "Jacuzzi" : s == rct3::park::kPoolHighDive ? "DBTopBoard" : s == rct3::park::kPoolStairs ? "StairsBase" : s == rct3::park::kPoolPath ? "PathStraight" : s == rct3::park::kPoolPathSlope ? "PathSlope" : s == rct3::park::kPoolPathStairs ? "PathStairs" : "Base";
            turn_preview(device, scene, assets, piece, rct3::park::kPoolOverlay);
        }
        const auto tile = spot ? std::optional(save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }) : std::nullopt;
        // Pinned, the paths page starts a trail where pressed: at the side of the pool square nearest the pointer
        // that a trail may start from. The square it would lay first is marked, else the trail's next.
        if (const auto panel = panel_of(scene); t.item && laying_paths(*t.item) && panel && panel->pinned())
        {
            if (const bool shown = put_back(scene); std::exchange(t.redraw, false) || shown)
                pools_changed(device, scene, assets);
            t.working.reset();
            const auto square = tile ? scene.pools.at(tile->col, tile->row) : std::nullopt;
            const auto deck = tile && !square ? std::ranges::find_if(decks, [&](const rct3::park::pools::SlidePlatform& d) { return d.col == tile->col && d.row == tile->row; }) : decks.end();
            const auto start_height = [&](rct3::park::Side side) { return square ? rct3::park::pools::pool_path_start(scene.pools, *square, side) : deck != decks.end() ? rct3::park::pools::slide_path_start(scene.pools, *deck, side) : std::nullopt; };
            const auto side = tile ? rct3::park::object::nearest_side(*spot - glm::vec2 { tile->col, tile->row }, [&](rct3::park::Side side) { return start_height(side).has_value(); }) : std::nullopt;
            // The trail as it would be, started where the pointer is, else the one laid.
            rct3::park::FootpathBuilder hovered = t.trail;
            if (side)
            {
                rct3::park::start_at(hovered, *tile);
                hovered.heading = *side;
                hovered.start_height = start_height(*side);
                hovered.trail.push_back({ .spot = *tile, .laid = 0, .height = *hovered.start_height });
                hovered.on_slope = square && (square->kind == rct3::park::pools::PoolKind::PathSlope || square->kind == rct3::park::pools::PoolKind::PathStairs);
            }
            if (down && !t.corner && side)
            {
                t.trail = hovered;
                t.trail_ground.assign(1, std::nullopt);
            }
            if (down)
                t.corner = tile;
            else
                t.corner.reset();
            // The next piece shown as the path tool shows it: the piece see-through, and the arrow over it the way
            // the trail goes on.
            const auto piece = rct3::park::pools::next_pool_path(hovered);
            std::optional<save::model::PlacedObject> ghost;
            t.arrow = { };
            if (piece)
            {
                const save::model::Tile at_spot { piece->square.col, piece->square.row };
                rct3::park::pools::PoolMap near;
                for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
                    if (std::abs(s.col - at_spot.col) <= 1 && std::abs(s.row - at_spot.row) <= 1 && (s.col != at_spot.col || s.row != at_spot.row))
                        near.squares.push_back(s);
                near.squares.push_back(piece->square);
                for (save::model::PlacedObject& o : rct3::park::pools::pool_objects(near, rct3::park::kPoolOverlay, 0))
                    if (o.col == at_spot.col && o.row == at_spot.row && o.base_height == piece->square.surface && o.symbol.starts_with("Path"))
                        ghost = std::move(o);
                if (ghost && !load_object_model(scene, scene.placing.models, assets, *ghost))
                    ghost.reset();
                const bool ok = t.item->pool_prices.size() == 18 && allowed(scene, marked(at_spot, at_spot), rct3::park::pools::pool_build_cost(std::span<const std::int32_t, 18>(t.item->pool_prices.data(), 18), piece->square));
                const std::int32_t from = hovered.trail.empty() ? hovered.start_height.value_or(0) : hovered.trail.back().height;
                t.arrow = rct3::park::heading_arrow({ .spot = at_spot, .start = from, .end = piece->end }, hovered.heading, scene.grid);
                for (rct3::park::GroundVertex& v : t.arrow.vertices)
                {
                    v.colour = ok ? glm::vec4(1.0f) : kRefusedTint;
                    v.position.y += kArrowLift;
                }
                show_ghost(device, scene, ghost, ok);
            }
            else
                show_ghost(device, scene, std::nullopt, true);
            draw_marks(device, scene, std::nullopt, rct3::park::TilePart::Whole, true);
            return;
        }
        // A high diving board faces the water beside it nearest the pointer, as an extra takes its side; the turn
        // keys turn it only where water lies on every side or none. Sides with no water behind come first, so one
        // laid on water at a pool's wall reaches straight out from the wall, not along it.
        if (t.item && t.item->symbol == rct3::park::kPoolHighDive && !t.corner && tile)
        {
            const auto water = [&](rct3::park::Side side) {
                const auto [dc, dr] = side.step();
                const auto square = scene.pools.at(tile->col + dc, tile->row + dr);
                return square && square->kind == rct3::park::pools::PoolKind::Water;
            };
            const bool backed = std::ranges::any_of(rct3::park::kSides, [&](rct3::park::Side side) { return water(side) && !water(side.opposite()); });
            const auto faces = [&](rct3::park::Side side) { return water(side) && (!backed || !water(side.opposite())); };
            if (const auto side = rct3::park::object::nearest_side(*spot - glm::vec2 { tile->col, tile->row }, faces); side && !std::ranges::all_of(rct3::park::kSides, faces))
                t.side = *side;
        }
        if (std::exchange(t.redraw, false))
            pools_changed(device, scene, assets);
        const bool dragging = t.item && down && (t.corner || tile);
        // A spa pool, a high diving board or a spiral staircase goes on the square pressed; dragging up or down
        // raises or lowers the board or the staircase's top.
        const bool climbs = t.item && (t.item->symbol == rct3::park::kPoolHighDive || t.item->symbol == rct3::park::kPoolStairs);
        const bool single = t.item && (t.item->symbol == rct3::park::kPoolJacuzzi || t.item->symbol == rct3::park::kPoolPathSlope || t.item->symbol == rct3::park::kPoolPathStairs || climbs);
        if (dragging)
        {
            if (!t.corner)
            {
                t.corner = deck_start ? deck_start->first : tile;
                t.press_y = at.y;
            }
            if (tile)
                t.other = single || deck_start ? t.corner : tile;
            if (climbs && t.press_y)
            {
                const std::int32_t lowest = t.item->symbol == rct3::park::kPoolStairs ? rct3::park::pools::kLowestStairs : rct3::park::pools::kBoardLevel;
                t.board = std::clamp(lowest + static_cast<std::int32_t>((*t.press_y - at.y) / kPixelsPerBoardUnit), lowest, kHighestBoard);
            }
        }
        // The work is the drag's, else the square under the pointer. As it changes it is laid into the pools to
        // be seen, when it may be done; letting go keeps it.
        const auto working = t.corner && t.other ? std::optional(std::pair { *t.corner, *t.other }) : (deck_start ? std::optional(std::pair { deck_start->first, deck_start->first }) : t.item && tile ? std::optional(std::pair { *tile, *tile }) : std::nullopt);
        // A change waits while the objects are being built away from the frame, so the frame never waits on
        // them; the latest work is shown once they are done, or at once when let go.
        // shortcut: each change still rebuilds the whole park's ground and objects (~200 ms a change in the
        // biggest parks); rebuild only the squares touched once the ground mesh is in chunks and objects can be
        // added to the built ones.
        const bool letting_go = !dragging && t.corner && t.other;
        if ((working != t.working || t.board != t.worked_board || t.side != t.worked_side || t.lifted != t.worked_lifted) && (letting_go || !scene.object_build.valid()))
        {
            bool changed = put_back(scene);
            // The work shown before and now, with the walls and railings round each.
            std::optional<rct3::park::TileRect> touched;
            for (const auto& work : { t.working, working })
            {
                if (!work)
                    continue;
                const auto [c0, r0, c1, r1] = marked(work->first, work->second);
                const rct3::park::TileRect area = rct3::park::TileRect { c0, r0, c1 + 1, r1 + 1 }.grown(1);
                touched = !touched ? area : rct3::park::TileRect { std::min(touched->col, area.col), std::min(touched->row, area.row), std::max(touched->end_col, area.end_col), std::max(touched->end_row, area.end_row) };
            }
            t.working = working;
            t.worked_board = t.board;
            t.worked_side = t.side;
            t.worked_lifted = t.lifted;
            if (working)
            {
                auto [map, cost] = marked_pools(scene, working->first, working->second);
                t.cost = cost;
                // A high diving board faces water under its boards' tip: beside it at its own level, or below it.
                const auto [dc, dr] = t.side.step();
                const auto faced = map.at(working->first.col + dc, working->first.row + dr);
                const auto tower = map.at(working->first.col, working->first.row);
                t.faces = t.item->symbol != rct3::park::kPoolHighDive || (faced && tower && faced->kind == rct3::park::pools::PoolKind::Water && faced->surface <= tower->surface);
                t.ok = t.faces && allowed(scene, marked(working->first, working->second), cost);
                if (t.ok)
                {
                    lay_work(scene, std::move(map));
                    changed = true;
                }
            }
            if (changed)
            {
                // Supports are made again only for work kept: nothing they hold up stands on pool squares.
                const bool stale = scene.supports.stale;
                pools_changed(device, scene, assets, touched);
                scene.supports.stale = stale;
            }
        }
        if (letting_go)
        {
            // Let go: the work is kept and paid for. Work on the park's own land that it cannot pay for is
            // turned down with a message.
            if (t.before && simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, t.cost, simulation::park::LedgerLine::RideConstruction, false))
            {
                ++scene.built;
                if (t.cost != 0)
                    park::float_money(scene, park::tile_point(scene.grid, *t.other), -t.cost);
                t.before.reset();
                t.dug.clear();
                scene.supports.stale = true;
                // The paths page's controls go on from the last piece laid: up a slope, or the way an L's last
                // leg went.
                const auto last = scene.pools.at(t.other->col, t.other->row);
                if (last && rct3::park::pools::is_pool_path(last->kind))
                {
                    const std::int32_t rise = last->kind == rct3::park::pools::PoolKind::PathSlope ? rct3::park::pools::kPathSlopeRise : last->kind == rct3::park::pools::PoolKind::PathStairs ? rct3::park::pools::kPathStairsRise : 0;
                    if (rise > 0)
                        t.trail.heading = last->side.opposite();
                    else if (t.other->row != t.corner->row)
                        t.trail.heading = t.other->row > t.corner->row ? rct3::park::Side::kNextRow : rct3::park::Side::kRowBefore;
                    else if (t.other->col != t.corner->col)
                        t.trail.heading = t.other->col > t.corner->col ? rct3::park::Side::kNextCol : rct3::park::Side::kColBefore;
                    rct3::park::start_at(t.trail, *t.other);
                    t.trail.start_height = last->surface;
                    t.trail.trail.push_back({ .spot = *t.other, .laid = 1, .height = last->surface + rise });
                    t.trail_ground.assign(1, std::nullopt);
                    t.trail.on_slope = rise > 0;
                }
            }
            else if (t.faces && allowed(scene, marked(*t.corner, *t.other), 0))
                simulation::park::cannot_pay(scene.park);
            t.corner.reset();
            t.other.reset();
            t.press_y.reset();
            t.lift.reset();
            t.lifted.reset();
        }
        if (t.item && laying_paths(*t.item))
        {
            show_ghost(device, scene, std::nullopt, true);
            t.arrow = { };
        }
        auto marks = working ? std::optional(marked(working->first, working->second)) : std::nullopt;
        // Else the square the paths page's controls lay next.
        // shortcut: marked flat; show the piece itself once the marks mislead.
        if (const auto next = rct3::park::next_spot(t.trail); !marks && next && t.item && laying_paths(*t.item))
            marks = marked(*next, *next);
        draw_marks(device, scene, marks, rct3::park::TilePart::Whole, !working || t.ok);
    }

    std::vector<rct3::park::pools::PoolOpening> pool_openings(const park::ParkScene& scene)
    {
        std::vector<rct3::park::pools::PoolOpening> out;
        for (std::size_t i = 0; i < scene.rides.rides.size() && i < scene.courses.size(); ++i)
            if (!scene.rides.rides[i].base.demolished && scene.courses[i])
                std::ranges::copy(scene.courses[i]->pool_openings, std::back_inserter(out));
        // A slide's station platform joins the pools at its deck on both sides off its track.
        for (const rct3::park::pools::SlidePlatform& p : slide_platforms(scene))
            for (const rct3::park::Side side : { p.turn.facing(), p.turn.facing().opposite() })
                out.push_back({ .col = p.col, .row = p.row, .height = p.height, .side = side });
        return out;
    }

    void pools_changed(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<rct3::park::TileRect> touched)
    {
        const profiling::Scope zone("PoolsChanged");
        wait_for_build(scene);
        // The work shown lays the pieces again at every move; those that stay as they were keep their objects.
        // The prop the info panel tells about is found again after, a changing room as the new piece of the
        // same room.
        const auto shown = scene.shown_prop && *scene.shown_prop < scene.objects.size() ? std::optional(scene.objects[*scene.shown_prop]) : std::nullopt;
        const bool shown_piece = shown && std::ranges::contains(scene.pool_pieces, shown->id);
        rct3::park::pools::dig_under_pools(scene.pools, scene.grid);
        scene.pool_openings = pool_openings(scene);
        std::vector<save::model::PlacedObject> pieces;
        for (save::model::PlacedObject& piece : rct3::park::pools::pool_objects(scene.pools, rct3::park::kPoolOverlay, last_id(scene), scene.pool_openings))
        {
            if (!load_object_model(scene, scene.placing.models, assets, piece))
                continue;
            piece.colours = scene.models.at(rct3::park::object::model_key(piece)).default_colours;
            pieces.push_back(std::move(piece));
        }
        replace_fixtures(scene, scene.pool_pieces, std::move(pieces));
        if (shown)
        {
            const auto same_piece = [&](const save::model::PlacedObject& o) { return shown_piece && o.col == shown->col && o.row == shown->row && o.symbol == shown->symbol && std::ranges::contains(scene.pool_pieces, o.id); };
            const auto found = std::ranges::find_if(scene.objects, [&](const save::model::PlacedObject& o) { return (!shown_piece && o.id == shown->id) || same_piece(o); });
            scene.shown_prop = found != scene.objects.end() ? std::optional(static_cast<std::size_t>(found - scene.objects.begin())) : std::nullopt;
        }
        // Pool rides' platforms show steps down the sides the paving now meets, or no longer does.
        for (std::size_t ride = 0; ride < scene.rides.rides.size() && ride < scene.track_rules.size(); ++ride)
            if (scene.track_rules[ride].pool_ride && !scene.rides.rides[ride].base.demolished)
                update_platforms(scene, ride, assets);
        objects_changed(scene);
        std::vector<save::model::PoolWater>& water = scene.footpaths.pools;
        water.resize(scene.saved_pool_water);
        std::ranges::copy(rct3::park::pools::pool_water(scene.pools), std::back_inserter(water));
        park::refresh_pavings(scene); // The changing rooms pave their floors
        park::rebuild_ground(device, scene, assets, touched);
    }

    void delete_pool_complex(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t room)
    {
        const auto& items = scene.build.items;
        if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        const auto type = std::ranges::find_if(items.get(), [](const rct3::park::BuildItem& i) { return i.overlay == rct3::park::kPoolOverlay && i.pool_prices.size() == 18; });
        if (type == items.get().end())
            return;
        const auto prices = std::span<const std::int32_t, 18>(type->pool_prices.data(), 18);
        const std::vector<save::model::Tile> complex = rct3::park::pools::pool_complex(scene.pools, room);
        if (complex.empty())
            return;
        // Each square goes as the delete tool takes it up, the ground it dug put back; the rooms opening on them go
        // with them.
        std::int64_t cost = 0;
        for (const save::model::Tile tile : complex)
        {
            for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
                if (s.col == tile.col && s.row == tile.row && s.ground)
                    scene.grid.at(tile.col, tile.row).corners = *s.ground;
            cost += rct3::park::pools::take_up_pool(scene.pools, prices, tile, tile);
        }
        simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::RideConstruction, false); // A refund, below zero
        if (cost != 0)
            park::float_money(scene, park::tile_point(scene.grid, complex.front()), -cost);
        pools_changed(device, scene, assets);
    }

    bool delete_pool_square(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size)
    {
        park::PoolTool& t = scene.pooling;
        if (!t.item)
            return false;
        // The work shown goes first, so the square pointed at is one built; it is shown again on the next frame.
        const bool shown = put_back(scene);
        t.working.reset();
        const auto spot = pointed_spot(scene, at, size, { });
        const auto tile = spot ? std::optional(save::model::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }) : std::nullopt;
        const auto top = tile ? scene.pools.at(tile->col, tile->row) : std::nullopt;
        const auto& items = scene.build.items;
        const bool ready = items.valid() && items.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        const auto type = ready ? std::ranges::find_if(items.get(), [](const rct3::park::BuildItem& i) { return i.overlay == rct3::park::kPoolOverlay && i.pool_prices.size() == 18; }) : decltype(items.get().end()) { };
        if (!top || !ready || type == items.get().end())
        {
            if (shown)
                pools_changed(device, scene, assets);
            return false;
        }
        // The top square there goes as the delete tool takes it up, with what stands on it, the ground it dug put back.
        const std::int32_t level = top->surface;
        rct3::park::pools::PoolMap map = scene.pools;
        const std::int64_t cost = rct3::park::pools::take_up_pool(map, std::span<const std::int32_t, 18>(type->pool_prices.data(), 18), *tile, *tile, level);
        for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
            if (const auto now = map.at(s.col, s.row, s.surface); s.ground && (!now || now->surface != s.surface))
                scene.grid.at(s.col, s.row).corners = *s.ground;
        scene.pools = std::move(map);
        ++scene.built;
        simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::RideConstruction, false); // A refund, below zero
        if (cost != 0)
            park::float_money(scene, park::tile_point(scene.grid, *tile), -cost);
        scene.supports.stale = true;
        pools_changed(device, scene, assets);
        return true;
    }

    void draw_pool_ghost(render::Device& device, const park::ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.pooling.item && !scene.pooling.ghost.vertices.empty())
            draw_ghost_mesh(device, scene, scene.pooling.ghost, transform, light);
        if (scene.pooling.item && !scene.pooling.arrow.vertices.empty() && scene.ground_textures.contains(rct3::park::ground::kPathArrowPicture))
            park::draw_build_arrow(device, scene, scene.pooling.arrow, rct3::park::ground::kPathArrowPicture, transform);
    }

} // namespace rct3::scene
