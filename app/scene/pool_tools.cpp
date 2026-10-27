// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/pool_tools.hpp"

#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/object_pick.hpp"
#include "park/pools.hpp"
#include "park/tile_shaping.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::scene
{

    namespace
    {
        constexpr glm::vec4 kMarkColour { 0.3f, 0.75f, 1.0f, 0.9f };
        constexpr glm::vec4 kRefusedColour { 1.0f, 0.35f, 0.3f, 0.9f };

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
            // shortcut: the surface snaps to the ground; add the editor's surface height buttons when pools
            // can be raised on supports.
            const auto begun = scene.pools.at(a.col, a.row);
            const std::int32_t surface = begun ? begun->surface : static_cast<std::int32_t>(std::lround(scene.grid.at(a.col, a.row).max_height()));
            const auto water = std::ranges::find(park::kPoolWater, item.symbol);
            const park::PoolKind kind = water != park::kPoolWater.end() ? park::PoolKind::Water : park::PoolKind::Tile;
            // Paved squares take the normal depth, the one the pool type prices them at.
            const std::int32_t depth = water != park::kPoolWater.end() ? static_cast<std::int32_t>(water - park::kPoolWater.begin()) + 1 : 2;
            std::vector<park::PoolSquare> squares;
            const auto area = marked(a, b);
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    squares.push_back({ .col = col, .row = row, .kind = kind, .surface = surface, .depth = depth });
            const std::int64_t cost = park::lay_pool(map, prices, squares);
            return { std::move(map), cost };
        }

        /// The square the pointer is on: the top of a pool square where one is in the way, as the player sees
        /// it, else the ground; the ground under the pools is dug down out of sight.
        std::optional<save::Tile> pointed_square(const ParkScene& scene, ui::Point at, ui::Point size)
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
            const save::Tile tile { static_cast<int>(std::floor((hit.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((hit.z - g.origin_z) / g.tile_size_z)) };
            return g.contains(tile) ? std::optional(tile) : std::nullopt;
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
    } // namespace

    bool start_pooling(ParkScene& scene, const park::BuildItem& item)
    {
        if (item.kind != park::BuildKind::PoolSquare || item.pool_prices.size() != 18)
            return false;
        scene.pooling = PoolTool { .item = item, .drawn = scene.pooling.drawn };
        return true;
    }

    void stop_pooling(ParkScene& scene)
    {
        scene.pooling = PoolTool { .drawn = scene.pooling.drawn }; // The marks go on the next frame
    }

    void update_pooling(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over)
    {
        PoolTool& t = scene.pooling;
        const auto tile = t.item && !over ? pointed_square(scene, at, size) : std::nullopt;
        if (t.item && down && (t.corner || tile))
        {
            if (!t.corner)
                t.corner = tile;
            if (tile)
                t.other = tile;
        }
        else if (t.corner && t.other)
        {
            // Let go: the squares are laid or taken up.
            const auto [map, cost] = marked_pools(scene, *t.corner, *t.other);
            if (allowed(scene, marked(*t.corner, *t.other), cost))
            {
                // The ground under squares taken up is left flat at their surface.
                for (const park::PoolSquare& s : scene.pools.squares)
                    if (!map.at(s.col, s.row))
                        scene.grid.at(s.col, s.row).corners.fill(static_cast<float>(s.surface));
                scene.pools = map;
                if (scene.park.cash)
                    *scene.park.cash -= cost;
                if (cost != 0)
                    float_money(scene, tile_point(scene.grid, *t.other), -cost);
                pools_changed(device, scene, assets);
            }
            t.corner.reset();
            t.other.reset();
        }
        std::optional<std::array<std::int32_t, 4>> marks;
        bool ok = true;
        if (t.corner && t.other)
        {
            marks = marked(*t.corner, *t.other);
            ok = allowed(scene, *marks, marked_pools(scene, *t.corner, *t.other).second);
        }
        else if (tile)
            marks = marked(*tile, *tile);
        if (marks != t.drawn)
        {
            park::TerrainMesh mesh;
            if (marks)
                park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], park::TilePart::Whole, ok ? kMarkColour : kRefusedColour);
            set_view_lines(device, scene.pool_marks, mesh);
            t.drawn = marks;
        }
    }

    void pools_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        wait_for_build(scene);
        for (save::PlacedObject& o : scene.objects)
            if (std::ranges::contains(scene.pool_pieces, o.id))
                o.hidden = true;
        scene.pool_pieces.clear();
        // The ground under every square is dug down to its floor.
        for (const park::PoolSquare& s : scene.pools.squares)
            if (scene.grid.contains(s.col, s.row))
                scene.grid.at(s.col, s.row).corners.fill(static_cast<float>(s.surface - s.depth));
        for (save::PlacedObject& piece : park::pool_objects(scene.pools, park::kPoolOverlay, last_id(scene)))
        {
            if (!load_object_model(scene, scene.placing.models, assets, piece))
                continue;
            piece.colours = scene.models.at(park::model_key(piece)).default_colours;
            scene.pool_pieces.push_back(piece.id);
            scene.objects.push_back(std::move(piece));
        }
        objects_changed(scene);
        std::vector<save::PoolWater>& water = scene.footpaths.pools;
        water.resize(scene.saved_pool_water);
        std::ranges::copy(park::pool_water(scene.pools), std::back_inserter(water));
        rebuild_ground(device, scene, assets);
    }

} // namespace rct3::scene
