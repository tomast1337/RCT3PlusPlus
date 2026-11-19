// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path/drag.hpp"

#include "core/types.hpp"
#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/object/pick.hpp"
#include "park/path/decks.hpp"
#include "park/queue_fences.hpp"
#include "park/ride/placement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace rct3::park::path
{

    std::vector<save::model::Tile> brush_line(save::model::Tile from, save::model::Tile to)
    {
        std::vector<save::model::Tile> line { from };
        save::model::Tile at = from;
        const i32 cols = std::abs(to.col - from.col);
        const i32 rows = std::abs(to.row - from.row);
        // Steps along whichever way is further behind the straight line, one side at a time.
        while (at != to)
        {
            const i32 done_cols = std::abs(at.col - from.col);
            const i32 done_rows = std::abs(at.row - from.row);
            const bool along_cols = at.row == to.row || (at.col != to.col && (done_cols + 0.5f) * static_cast<f32>(rows) <= (done_rows + 0.5f) * static_cast<f32>(cols));
            if (along_cols)
                at.col += to.col > at.col ? 1 : -1;
            else
                at.row += to.row > at.row ? 1 : -1;
            line.push_back(at);
        }
        return line;
    }

    std::vector<save::model::Tile> brush_square(save::model::Tile centre, i32 size)
    {
        std::vector<save::model::Tile> square;
        const i32 low = -(size - 1) / 2;
        for (i32 row = low; row < low + size; ++row)
        {
            for (i32 col = low; col < low + size; ++col)
                square.push_back({ centre.col + col, centre.row + row });
        }
        return square;
    }

    DragPlan plan_drag(std::span<const save::model::Tile> area, const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, std::optional<const SolidTiles&> solids)
    {
        DragPlan plan;
        for (const save::model::Tile spot : area)
        {
            if (ground_path_at(paths, spot))
            {
                ++plan.kept;
                continue;
            }
            FootpathBuilder one;
            start_at(one, spot);
            const auto piece = next_piece(one, grid);
            if (piece && piece->ground && lay_problem(one, grid, paths, solids) == LayProblem::None)
                plan.lay.push_back(spot);
            else
                ++plan.refused;
        }
        return plan;
    }

    Side nearest_side(f32 across_col, f32 across_row) noexcept
    {
        // Distances to the edges, by side.
        const std::array<f32, 4> to_edge { 1 - across_row, 1 - across_col, across_row, across_col };
        return Side::from(static_cast<i32>(std::ranges::min_element(to_edge) - to_edge.begin()));
    }

    std::optional<PathEdge> path_under(const Ray& ray, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, bool through_ground)
    {
        const auto ground = object::ground_hit(ray, grid);
        std::optional<PathEdge> best;
        f32 nearest = ground && !through_ground ? *ground + 0.01f : kFar;
        glm::vec3 best_hit { 0.0f };
        for (usize i = 0; i < paths.size(); ++i)
        {
            const save::model::PathTile& p = paths[i];
            if (!grid.contains(p.col, p.row))
                continue;
            f32 t = 0;
            if (!p.elevation || ground::is_ground_queue(p))
            {
                if (!ground)
                    continue;
                t = *ground;
            }
            else
            {
                const save::model::PathElevation& e = *p.elevation;
                const f32 rise = ground::path_rise(e.slope);
                const f32 middle = static_cast<f32>(e.quantised_height) + (slope_falls(e.slope) ? -rise : rise) / 2;
                if (std::abs(ray.direction.y) < 1e-6f)
                    continue;
                t = (middle - ray.origin.y) / ray.direction.y;
            }
            if (t <= 0 || t > nearest)
                continue;
            const glm::vec3 hit = ray.origin + ray.direction * t;
            if (static_cast<i32>(std::floor((hit.x - grid.origin_x) / grid.tile_size_x)) != p.col || static_cast<i32>(std::floor((hit.z - grid.origin_z) / grid.tile_size_z)) != p.row)
                continue;
            nearest = t;
            best = PathEdge { i, std::nullopt };
            best_hit = hit;
        }
        if (!best)
            return std::nullopt;
        const save::model::PathTile& p = paths[best->path_index];
        const Side side = nearest_side((best_hit.x - grid.world_x(p.col)) / grid.tile_size_x, (best_hit.z - grid.world_z(p.row)) / grid.tile_size_z);
        if (edge_height(p, grid, side))
            best->side = side;
        return best;
    }

    bool start_from_edge(FootpathBuilder& builder, const save::model::PathTile& path, const save::model::TerrainGrid& grid, Side side)
    {
        const auto height = edge_height(path, grid, side);
        if (!height)
            return false;
        const save::model::Tile spot { path.col, path.row };
        start_at(builder, spot);
        builder.heading = side;
        builder.trail.push_back({ spot, 0, static_cast<i32>(std::lround(*height)) });
        return true;
    }

    std::optional<usize> door_under(const Ray& ray, std::span<const save::model::PlacementSpot> doors, const save::model::TerrainGrid& grid, bool through_ground)
    {
        if (std::abs(ray.direction.y) < 1e-6f)
            return std::nullopt;
        const auto ground = through_ground ? std::nullopt : object::ground_hit(ray, grid);
        f32 nearest = ground ? *ground + 0.01f : kFar;
        std::optional<usize> best;
        for (usize i = 0; i < doors.size(); ++i)
        {
            const save::model::PlacementSpot& d = doors[i];
            const f32 t = (static_cast<f32>(d.height) - ray.origin.y) / ray.direction.y;
            if (t <= 0 || t > nearest)
                continue;
            const glm::vec3 hit = ray.origin + ray.direction * t;
            const f32 across_col = (hit.x - grid.world_x(d.col)) / grid.tile_size_x;
            const f32 across_row = (hit.z - grid.world_z(d.row)) / grid.tile_size_z;
            if (across_col < 0 || across_col >= 1 || across_row < 0 || across_row >= 1)
                continue;
            nearest = t;
            best = i;
        }
        return best;
    }

    void start_from_door(FootpathBuilder& builder, const save::model::PlacementSpot& door)
    {
        const save::model::Tile spot { door.col, door.row };
        start_at(builder, spot);
        builder.heading = ride::door_back(door);
        builder.trail.push_back({ spot, 0, door.height });
    }

} // namespace rct3::park::path
