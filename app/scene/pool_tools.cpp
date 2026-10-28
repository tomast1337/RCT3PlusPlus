// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/pool_tools.hpp"

#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/object_pick.hpp"
#include "park/pools.hpp"
#include "park/tile_shaping.hpp"
#include "profiling/profiler.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"
#include "simulation/park_messages.hpp"
#include "ui/extra_skins.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/pool_panel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

namespace rct3::scene
{

    namespace
    {
        constexpr glm::vec4 kMarkColour { 0.3f, 0.75f, 1.0f, 0.9f };
        constexpr glm::vec4 kRefusedColour { 1.0f, 0.35f, 0.3f, 0.9f };
        constexpr std::string_view kPanelName = "pool_panel";
        constexpr std::string_view kNoMoney = "PlaceObject_NotEnoughMoney";
        constexpr float kPixelsPerBoardUnit = 10; ///< Of a drag up, to raise a high diving board a unit
        constexpr std::int32_t kHighestBoard = 30;
        constexpr std::array<std::array<std::int32_t, 2>, 4> kFacing { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } }; // By turn, as park/pools.hpp turns

        std::optional<ui::PoolPanel&> panel_of(const ParkScene& scene)
        {
            return scene.desktop->root().find_as<ui::PoolPanel>(kPanelName);
        }

        /// The extras and the changing rooms the pool panel offers, in its order: the pool type's extras, its
        /// spa pool last, and every changing room. None while the build items are still being read.
        std::pair<std::vector<const park::BuildItem*>, std::vector<const park::BuildItem*>> offered(const ParkScene& scene)
        {
            std::pair<std::vector<const park::BuildItem*>, std::vector<const park::BuildItem*>> out;
            const auto& items = scene.build.items;
            if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return out;
            const park::BuildItem* spa = nullptr;
            const park::BuildItem* dive = nullptr;
            const park::BuildItem* stairs = nullptr;
            for (const park::BuildItem& i : items.get())
            {
                if (i.list != park::BuildList::Pools)
                    continue;
                // shortcut: the default pool type only; offer the others once the panel can choose a type.
                if (i.kind == park::BuildKind::PoolRoom)
                    out.second.push_back(&i);
                else if (i.overlay == park::kPoolOverlay && i.kind == park::BuildKind::PoolExtra)
                    out.first.push_back(&i);
                else if (i.overlay == park::kPoolOverlay && i.symbol == park::kPoolJacuzzi)
                    spa = &i;
                else if (i.overlay == park::kPoolOverlay && i.symbol == park::kPoolHighDive)
                    dive = &i;
                else if (i.overlay == park::kPoolOverlay && i.symbol == park::kPoolStairs)
                    stairs = &i;
            }
            for (const park::BuildItem* last : { dive, spa, stairs })
                if (last != nullptr)
                    out.first.push_back(last);
            return out;
        }

        /// Follows the pool panel: what the player chose there goes out as the tool.
        void follow_panel(ParkScene& scene)
        {
            auto panel = panel_of(scene);
            if (!panel)
                return;
            if (panel->take_closed())
                return stop_pooling(scene);
            if (!panel->showing())
                return;
            const ui::PoolEditor& e = panel->editor();
            scene.pooling.height = e.snap ? std::nullopt : std::optional(e.height);
            scene.pooling.edge = static_cast<park::PoolEdge>(std::clamp(e.edge, 0, 3));
            if (e.grid != scene.pooling.grid)
                scene.show_grid = scene.pooling.grid = e.grid;
            if (!panel->take_change())
                return;
            const auto [extras, rooms] = offered(scene);
            const park::BuildItem* item = nullptr;
            if (const auto p = panel->placing())
            {
                const auto& from = p->first == ui::PoolPage::Extras ? extras : rooms;
                item = p->second < from.size() ? from[p->second] : nullptr;
            }
            else if (const auto& items = scene.build.items; items.valid() && items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const std::string_view symbol = e.mode == ui::PoolMode::Water ? park::kPoolWater[static_cast<std::size_t>(std::clamp(e.depth, 1, 3) - 1)] : (e.mode == ui::PoolMode::Tiles ? park::kPoolTiles : park::kPoolDelete);
                const auto found = std::ranges::find_if(items.get(), [&](const park::BuildItem& i) { return i.list == park::BuildList::Pools && i.overlay == park::kPoolOverlay && i.symbol == symbol; });
                item = found != items.get().end() ? &*found : nullptr;
            }
            if (item != nullptr)
                (void)start_pooling(scene, *item);
        }

        /// The area marked, as its least and greatest column and row.
        std::array<std::int32_t, 4> marked(save::Tile a, save::Tile b)
        {
            return { std::min(a.col, b.col), std::min(a.row, b.row), std::max(a.col, b.col), std::max(a.row, b.row) };
        }

        /// The pools with the area marked laid or taken up, and what that costs; below zero, money back.
        std::pair<park::PoolMap, std::int64_t> marked_pools(const ParkScene& scene, save::Tile a, save::Tile b)
        {
            const park::BuildItem& item = *scene.pooling.item;
            park::PoolMap map = scene.pools;
            const auto prices = std::span<const std::int32_t, 18>(item.pool_prices.data(), 18);
            if (item.symbol == park::kPoolDelete)
            {
                const std::int64_t cost = park::take_up_pool(map, prices, a, b);
                return { std::move(map), cost };
            }
            // Snapped to the ground, the surface is the ground's where the drag began, or the surface of the pool
            // begun there; else the height the pool panel sets. Raised, a pool stands on the land raised to its
            // floor (pools_changed).
            const auto begun = scene.pools.at(a.col, a.row);
            const std::int32_t surface = scene.pooling.height.value_or(begun ? begun->surface : static_cast<std::int32_t>(std::lround(scene.grid.at(a.col, a.row).max_height())));
            const auto water = std::ranges::find(park::kPoolWater, item.symbol);
            const park::PoolKind kind = water != park::kPoolWater.end() ? park::PoolKind::Water : item.symbol == park::kPoolJacuzzi ? park::PoolKind::Jacuzzi : item.symbol == park::kPoolHighDive ? park::PoolKind::HighDive : item.symbol == park::kPoolStairs ? park::PoolKind::Stairs : park::PoolKind::Tile;
            const bool climbs = kind == park::PoolKind::HighDive || kind == park::PoolKind::Stairs;
            // Paved squares take the normal depth, the one the pool type prices them at.
            const std::int32_t depth = water != park::kPoolWater.end() ? static_cast<std::int32_t>(water - park::kPoolWater.begin()) + 1 : 2;
            std::vector<park::PoolSquare> squares;
            const auto area = marked(a, b);
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    squares.push_back({ .col = col, .row = row, .kind = kind, .surface = surface, .depth = depth, .board = climbs ? scene.pooling.board : 0, .turn = climbs ? scene.pooling.turns : 0, .edge = scene.pooling.edge });
            const std::int64_t cost = park::lay_pool(map, prices, squares);
            return { std::move(map), cost };
        }

        /// Where the pointer is on the grid, in tiles with fractions: on the top of a pool square where one is
        /// in the way, as the player sees it, else on the ground; the ground under the pools is dug down out of
        /// sight.
        std::optional<glm::vec2> pointed_spot(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            if (size.x <= 0 || size.y <= 0)
                return std::nullopt;
            const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
            std::optional<float> nearest = park::ground_hit(ray, scene.grid);
            const save::TerrainGrid& g = scene.grid;
            for (const park::PoolSquare& s : scene.pools.squares)
            {
                const float along = (static_cast<float>(s.surface) - ray.origin.y) / ray.direction.y;
                const glm::vec3 hit = ray.origin + ray.direction * along;
                const bool on = std::floor((hit.x - g.origin_x) / g.tile_size_x) == static_cast<float>(s.col) && std::floor((hit.z - g.origin_z) / g.tile_size_z) == static_cast<float>(s.row);
                if (along > 0 && on && (!nearest || along < *nearest))
                    nearest = along;
            }
            if (!nearest)
                return std::nullopt;
            const glm::vec3 hit = ray.origin + ray.direction * *nearest;
            const glm::vec2 spot { (hit.x - g.origin_x) / g.tile_size_x, (hit.z - g.origin_z) / g.tile_size_z };
            return g.contains(static_cast<int>(std::floor(spot.x)), static_cast<int>(std::floor(spot.y))) ? std::optional(spot) : std::nullopt;
        }

        /// Draws the marks over the area, at the pools' surface where squares are, and remembers them.
        void draw_marks(render::Device& device, ParkScene& scene, std::optional<std::array<std::int32_t, 4>> marks, park::TilePart part, bool ok)
        {
            const std::int32_t look = std::to_underlying(part) * 2 + (ok ? 1 : 0);
            if (marks == scene.pooling.drawn && look == scene.pooling.drawn_look)
                return;
            park::TerrainMesh mesh;
            if (marks)
            {
                // The ground under the squares is dug out of sight: the marks are drawn as on their surface.
                std::vector<std::pair<save::Tile, std::array<float, 4>>> dug;
                for (const park::PoolSquare& s : scene.pools.squares)
                    if (s.col >= (*marks)[0] && s.col <= (*marks)[2] && s.row >= (*marks)[1] && s.row <= (*marks)[3])
                    {
                        auto& corners = scene.grid.at(s.col, s.row).corners;
                        dug.emplace_back(save::Tile { s.col, s.row }, corners);
                        corners.fill(static_cast<float>(s.surface));
                    }
                park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], part, ok ? kMarkColour : kRefusedColour);
                for (const auto& [tile, corners] : dug)
                    scene.grid.at(tile.col, tile.row).corners = corners;
            }
            set_view_lines(device, scene.pool_marks, mesh);
            scene.pooling.drawn = marks;
            scene.pooling.drawn_look = look;
        }

        /// The object `symbol` of `overlay` alone, slowly turning, for the panel's disc; none for an empty symbol.
        void turn_preview(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::string_view symbol, std::string_view overlay)
        {
            PoolTool& t = scene.pooling;
            if (symbol.empty())
            {
                t.preview_of.clear();
                t.preview = { };
                return;
            }
            save::PlacedObject alone { .symbol = std::string(symbol), .overlay = std::string(overlay), .force_absolute_height = true };
            if (t.preview_of != symbol && load_object_model(scene, scene.placing.models, assets, alone))
            {
                t.preview_of = symbol;
                t.preview = park::object_mesh(std::span(&alone, 1), scene.models, scene.grid);
                ghost_textures(device, scene, t.preview);
            }
            if (t.preview_of == symbol)
                t.preview_camera = park::framed(park::object_corners(alone, scene.models, scene.grid, scene.fitted), kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
        }

        /// Puts the extra or changing room out with the pool tool where the pointer is, when it fits and can be
        /// paid for. A changing room goes on the land the park owns.
        void update_extra(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<glm::vec2> spot, bool down)
        {
            PoolTool& t = scene.pooling;
            const park::BuildItem& item = *t.item;
            const auto tile = spot ? std::optional(save::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }) : std::nullopt;
            const std::int32_t turn = t.turns;
            const park::PoolExtraFit fit = park::pool_extra_fit(item.pool_extra);
            const bool room = item.kind == park::BuildKind::PoolRoom;
            const bool fits = tile && (room ? park::pool_room_fits(scene.pools, tile->col, tile->row, turn) && park::owned_outright(scene.grid.at(tile->col, tile->row)) : park::pool_extra_fits(scene.pools, tile->col, tile->row, fit, turn));
            const bool ok = fits && (!scene.park.cash || *scene.park.cash >= item.price);
            if (down && tile)
                t.corner = tile;
            else if (t.corner && !down)
            {
                // Let go: the extra goes in where the pointer is now. A changing room is placed once: the tool
                // and its ghost go.
                t.corner.reset();
                if (ok && room)
                {
                    scene.pools.rooms.push_back({ .symbol = item.symbol, .overlay = item.overlay, .col = tile->col, .row = tile->row, .turn = turn });
                    pools_changed(device, scene, assets);
                    // It opens on a paved square, so it is in a complex: its window opens in place of the panel.
                    const std::size_t placed = scene.pools.rooms.size() - 1;
                    stop_pooling(scene);
                    scene.pooling.room_placed = placed;
                    return;
                }
                if (ok)
                {
                    save::PlacedObject model { .symbol = item.symbol, .overlay = item.overlay };
                    const bool loaded = load_object_model(scene, scene.placing.models, assets, model);
                    scene.pools.extras.push_back(
                        {
                            .symbol = item.symbol,
                            .overlay = item.overlay,
                            .col = tile->col,
                            .row = tile->row,
                            .turn = turn,
                            .refund = loaded ? scene.models.at(park::model_key(model)).removal_cost : 0,
                            .fit = fit,
                        });
                    if (scene.park.cash)
                        *scene.park.cash -= item.price;
                    if (item.price != 0)
                        float_money(scene, tile_point(scene.grid, *tile), -item.price);
                    pools_changed(device, scene, assets);
                }
                else if (fits)
                    simulation::post(scene.park.messages, simulation::ParkMessage { .text_key = std::string(kNoMoney) });
            }
            draw_marks(device, scene, tile ? std::optional(marked(*tile, *tile)) : std::nullopt, park::TilePart::Whole, ok);
            turn_preview(device, scene, assets, item.symbol, item.overlay);
            // The ghost: the extra or room as it would stand there, on its square's surface, else on the ground.
            std::optional<save::PlacedObject> ghost;
            if (tile)
            {
                park::PoolMap alone { .squares = scene.pools.squares };
                if (room)
                    alone.rooms.push_back({ .symbol = item.symbol, .overlay = item.overlay, .col = tile->col, .row = tile->row, .turn = turn });
                else
                    alone.extras.push_back({ .symbol = item.symbol, .overlay = item.overlay, .col = tile->col, .row = tile->row, .turn = turn, .fit = fit });
                ghost = park::pool_objects(alone, item.overlay, 0).back();
                const bool on_square = room ? fits : scene.pools.at(tile->col, tile->row).has_value();
                if (!on_square)
                    ghost->base_height = static_cast<std::int32_t>(std::lround(scene.grid.at(tile->col, tile->row).max_height()));
                if (!load_object_model(scene, scene.placing.models, assets, *ghost))
                    ghost.reset();
            }
            const bool same = ghost.has_value() == t.ghost_of.has_value() && (!ghost || (ghost->col == t.ghost_of->col && ghost->row == t.ghost_of->row && ghost->base_height == t.ghost_of->base_height && ghost->rotation == t.ghost_of->rotation));
            if (same && ok == t.ghost_ok)
                return;
            t.ghost_of = ghost;
            t.ghost_ok = ok;
            t.ghost = ghost ? park::object_mesh(std::span(&*ghost, 1), scene.models, scene.grid) : park::ObjectMesh { };
            for (park::GroundVertex& v : t.ghost.vertices)
                v.colour *= ok ? kGhostTint : kRefusedTint;
            ghost_textures(device, scene, t.ghost);
        }

        /// Whether the area may be worked on: all of it land the park owns outright, and paid for.
        /// shortcut: what stands on the land is not checked; refuse paths and objects once pools can clash.
        bool allowed(const ParkScene& scene, const std::array<std::int32_t, 4>& area, std::int64_t cost)
        {
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    if (!scene.grid.contains(col, row) || !park::owned_outright(scene.grid.at(col, row)))
                        return false;
            return !scene.park.cash || cost <= 0 || *scene.park.cash >= cost;
        }

        /// Takes the work shown out of the pools again, and puts back the ground it dug. True when there was any.
        bool put_back(ParkScene& scene)
        {
            PoolTool& t = scene.pooling;
            if (!t.before)
                return false;
            scene.pools = std::move(*t.before);
            t.before.reset();
            for (const auto& [tile, corners] : t.dug)
                scene.grid.at(tile.col, tile.row).corners = corners;
            t.dug.clear();
            return true;
        }

        /// Swaps the work shown with what was there before; twice swaps it back.
        void swap_work(ParkScene& scene)
        {
            PoolTool& t = scene.pooling;
            if (!t.before)
                return;
            std::swap(scene.pools, *t.before);
            for (auto& [tile, corners] : t.dug)
                std::swap(scene.grid.at(tile.col, tile.row).corners, corners);
        }

        /// Lays `map`, the pools with the work done, in their place, keeping what to put back.
        void lay_work(ParkScene& scene, park::PoolMap map)
        {
            PoolTool& t = scene.pooling;
            const auto area = marked(t.working->first, t.working->second);
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    t.dug.emplace_back(save::Tile { col, row }, scene.grid.at(col, row).corners);
            // The ground under squares taken up is left flat at their surface.
            for (const park::PoolSquare& s : scene.pools.squares)
                if (!map.at(s.col, s.row))
                    scene.grid.at(s.col, s.row).corners.fill(static_cast<float>(s.surface));
            t.before = std::exchange(scene.pools, std::move(map));
        }
    } // namespace

    bool start_pooling(ParkScene& scene, const park::BuildItem& item)
    {
        if ((item.kind != park::BuildKind::PoolSquare || item.pool_prices.size() != 18) && item.kind != park::BuildKind::PoolExtra && item.kind != park::BuildKind::PoolRoom)
            return false;
        const bool redraw = put_back(scene) || scene.pooling.redraw;
        scene.pooling = PoolTool { .item = item, .drawn = scene.pooling.drawn, .drawn_look = scene.pooling.drawn_look, .height = scene.pooling.height, .grid = scene.pooling.grid, .edge = scene.pooling.edge, .redraw = redraw, .turns = scene.pooling.turns, .preview_target = scene.pooling.preview_target, .preview_pixels = scene.pooling.preview_pixels };
        return true;
    }

    void stop_pooling(ParkScene& scene)
    {
        if (auto panel = panel_of(scene))
            panel->hide();
        const bool redraw = put_back(scene) || scene.pooling.redraw;
        scene.pooling = PoolTool { .drawn = scene.pooling.drawn, .drawn_look = scene.pooling.drawn_look, .grid = scene.pooling.grid, .edge = scene.pooling.edge, .redraw = redraw, .turns = scene.pooling.turns, .preview_target = scene.pooling.preview_target, .preview_pixels = scene.pooling.preview_pixels }; // The marks go on the next frame
    }

    void toggle_pool_panel(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets)
    {
        auto panel = panel_of(scene);
        if (panel && panel->showing())
            return stop_pooling(scene);
        if (!panel)
        {
            // The extras' pictures live in an overlay of their own.
            constexpr std::array<std::string_view, 1> kIconFiles { "Pool/PoolIcons.common.ovl" };
            (void)ui::add_extra_skins(device, assets, kIconFiles);
            panel = scene.desktop->root().add<ui::PoolPanel>(std::string(kPanelName), ui::game_skins::pool_panel_style(assets.palette), *assets.strings);
        }
        const auto [extras, rooms] = offered(scene);
        const auto choices = [&](const std::vector<const park::BuildItem*>& items) {
            std::vector<ui::PoolChoice> out;
            for (const park::BuildItem* i : items)
                out.push_back({ i->icon, std::u16string(assets.strings->get(i->name_text)) });
            return out;
        };
        panel->show(choices(extras), choices(rooms));
    }

    void update_pooling(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, std::span<const ui::PlayCommand> commands)
    {
        follow_panel(scene);
        PoolTool& t = scene.pooling;
        t.room_placed.reset(); // Its window opened, and the click that placed it was not taken as a pick
        // The pointer finds the park as it was before the work shown, which moves with it.
        swap_work(scene);
        const auto spot = t.item && !over ? pointed_spot(scene, at, size) : std::nullopt;
        swap_work(scene);
        for (const ui::PlayCommand c : commands)
        {
            if (c == ui::PlayCommand::TurnItemRight)
                t.turns = (t.turns + 1) % 4;
            else if (c == ui::PlayCommand::TurnItemLeft)
                t.turns = (t.turns + 3) % 4;
        }
        if (t.item && (t.item->kind == park::BuildKind::PoolExtra || t.item->kind == park::BuildKind::PoolRoom))
            return update_extra(device, scene, assets, spot, down);
        // The disc shows the piece the tool lays: the floor of a water square, the paving, or the extra laid as
        // a square; nothing while taking squares up.
        if (t.item)
        {
            const std::string_view s = t.item->symbol;
            const std::string_view piece = s == park::kPoolDelete ? "" : s == park::kPoolTiles ? "SolidTop" : s == park::kPoolJacuzzi ? "Jacuzzi" : s == park::kPoolHighDive ? "DBTopBoard" : s == park::kPoolStairs ? "StairsBase" : "Base";
            turn_preview(device, scene, assets, piece, park::kPoolOverlay);
        }
        const auto tile = spot ? std::optional(save::Tile { static_cast<int>(std::floor(spot->x)), static_cast<int>(std::floor(spot->y)) }) : std::nullopt;
        if (std::exchange(t.redraw, false))
            pools_changed(device, scene, assets);
        const bool dragging = t.item && down && (t.corner || tile);
        // A spa pool, a high diving board or a spiral staircase goes on the square pressed; dragging up or down
        // raises or lowers the board or the staircase's top.
        const bool climbs = t.item && (t.item->symbol == park::kPoolHighDive || t.item->symbol == park::kPoolStairs);
        const bool single = t.item && (t.item->symbol == park::kPoolJacuzzi || climbs);
        if (dragging)
        {
            if (!t.corner)
            {
                t.corner = tile;
                t.press_y = at.y;
            }
            if (tile)
                t.other = single ? t.corner : tile;
            if (climbs && t.press_y)
            {
                const std::int32_t lowest = t.item->symbol == park::kPoolStairs ? park::kLowestStairs : park::kBoardLevel;
                t.board = std::clamp(lowest + static_cast<std::int32_t>((*t.press_y - at.y) / kPixelsPerBoardUnit), lowest, kHighestBoard);
            }
        }
        // The work is the drag's, else the square under the pointer. As it changes it is laid into the pools to
        // be seen, when it may be done; letting go keeps it.
        const auto working = t.corner && t.other ? std::optional(std::pair { *t.corner, *t.other }) : (t.item && tile ? std::optional(std::pair { *tile, *tile }) : std::nullopt);
        // A change waits while the objects are being built away from the frame, so the frame never waits on
        // them; the latest work is shown once they are done, or at once when let go.
        // shortcut: each change still rebuilds the whole park's ground and objects (~200 ms a change in the
        // biggest parks); rebuild only the squares touched once the ground mesh is in chunks and objects can be
        // added to the built ones.
        const bool letting_go = !dragging && t.corner && t.other;
        if ((working != t.working || t.board != t.worked_board || t.turns != t.worked_turns) && (letting_go || !scene.object_build.valid()))
        {
            bool changed = put_back(scene);
            // The work shown before and now, with the walls and railings round each.
            std::optional<park::TileRect> touched;
            for (const auto& work : { t.working, working })
            {
                if (!work)
                    continue;
                const auto [c0, r0, c1, r1] = marked(work->first, work->second);
                const park::TileRect area = park::TileRect { c0, r0, c1 + 1, r1 + 1 }.grown(1);
                touched = !touched ? area : park::TileRect { std::min(touched->col, area.col), std::min(touched->row, area.row), std::max(touched->end_col, area.end_col), std::max(touched->end_row, area.end_row) };
            }
            t.working = working;
            t.worked_board = t.board;
            t.worked_turns = t.turns;
            if (working)
            {
                auto [map, cost] = marked_pools(scene, working->first, working->second);
                t.cost = cost;
                // A high diving board faces water.
                const auto [dc, dr] = kFacing[static_cast<std::size_t>(t.turns & 3)];
                const auto faced = map.at(working->first.col + dc, working->first.row + dr);
                t.faces = t.item->symbol != park::kPoolHighDive || (faced && faced->kind == park::PoolKind::Water);
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
            if (t.before)
            {
                if (scene.park.cash)
                    *scene.park.cash -= t.cost;
                if (t.cost != 0)
                    float_money(scene, tile_point(scene.grid, *t.other), -t.cost);
                t.before.reset();
                t.dug.clear();
                scene.supports.stale = true;
            }
            else if (t.faces && allowed(scene, marked(*t.corner, *t.other), 0))
                simulation::post(scene.park.messages, simulation::ParkMessage { .text_key = std::string(kNoMoney) });
            t.corner.reset();
            t.other.reset();
            t.press_y.reset();
        }
        const auto marks = working ? std::optional(marked(working->first, working->second)) : std::nullopt;
        draw_marks(device, scene, marks, park::TilePart::Whole, !working || t.ok);
    }

    void pools_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<park::TileRect> touched)
    {
        const profiling::Scope zone("PoolsChanged");
        wait_for_build(scene);
        // The work shown lays the pieces again at every move; those that stay as they were keep their objects.
        // The prop the info panel tells about is found again after, a changing room as the new piece of the
        // same room.
        const auto shown = scene.shown_prop && *scene.shown_prop < scene.objects.size() ? std::optional(scene.objects[*scene.shown_prop]) : std::nullopt;
        const bool shown_piece = shown && std::ranges::contains(scene.pool_pieces, shown->id);
        // The ground under every square is dug down to its floor.
        for (const park::PoolSquare& s : scene.pools.squares)
            if (scene.grid.contains(s.col, s.row))
                scene.grid.at(s.col, s.row).corners.fill(static_cast<float>(s.surface - s.depth));
        std::vector<save::PlacedObject> pieces;
        for (save::PlacedObject& piece : park::pool_objects(scene.pools, park::kPoolOverlay, last_id(scene)))
        {
            if (!load_object_model(scene, scene.placing.models, assets, piece))
                continue;
            piece.colours = scene.models.at(park::model_key(piece)).default_colours;
            pieces.push_back(std::move(piece));
        }
        replace_fixtures(scene, scene.pool_pieces, std::move(pieces));
        if (shown)
        {
            const auto same_piece = [&](const save::PlacedObject& o) { return shown_piece && o.col == shown->col && o.row == shown->row && o.symbol == shown->symbol && std::ranges::contains(scene.pool_pieces, o.id); };
            const auto found = std::ranges::find_if(scene.objects, [&](const save::PlacedObject& o) { return (!shown_piece && o.id == shown->id) || same_piece(o); });
            scene.shown_prop = found != scene.objects.end() ? std::optional(static_cast<std::size_t>(found - scene.objects.begin())) : std::nullopt;
        }
        objects_changed(scene);
        std::vector<save::PoolWater>& water = scene.footpaths.pools;
        water.resize(scene.saved_pool_water);
        std::ranges::copy(park::pool_water(scene.pools), std::back_inserter(water));
        rebuild_ground(device, scene, assets, touched);
    }

    void delete_pool_complex(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t room)
    {
        const auto& items = scene.build.items;
        if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        const auto type = std::ranges::find_if(items.get(), [](const park::BuildItem& i) { return i.overlay == park::kPoolOverlay && i.pool_prices.size() == 18; });
        if (type == items.get().end())
            return;
        const auto prices = std::span<const std::int32_t, 18>(type->pool_prices.data(), 18);
        const std::vector<save::Tile> complex = park::pool_complex(scene.pools, room);
        if (complex.empty())
            return;
        // Each square goes as the delete tool takes it up, the ground left flat at its surface; the rooms
        // opening on them go with them.
        std::int64_t cost = 0;
        for (const save::Tile tile : complex)
            if (const auto square = scene.pools.at(tile.col, tile.row))
            {
                scene.grid.at(tile.col, tile.row).corners.fill(static_cast<float>(square->surface));
                cost += park::take_up_pool(scene.pools, prices, tile, tile);
            }
        if (scene.park.cash)
            *scene.park.cash -= cost;
        if (cost != 0)
            float_money(scene, tile_point(scene.grid, complex.front()), -cost);
        pools_changed(device, scene, assets);
    }

    void draw_pool_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.pooling.item && !scene.pooling.ghost.vertices.empty())
            draw_ghost_mesh(device, scene, scene.pooling.ghost, transform, light);
    }

} // namespace rct3::scene
