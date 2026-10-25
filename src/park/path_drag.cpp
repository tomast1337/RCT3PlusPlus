// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_drag.hpp"

#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/object_pick.hpp"
#include "park/path_decks.hpp"
#include "park/queue_fences.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rct3::park
{

    std::vector<save::Tile> brush_line(save::Tile from, save::Tile to)
    {
        std::vector<save::Tile> line { from };
        save::Tile at = from;
        const int cols = std::abs(to.col - from.col);
        const int rows = std::abs(to.row - from.row);
        // Steps along whichever way is further behind the straight line, one side at a time.
        while (at != to)
        {
            const int done_cols = std::abs(at.col - from.col);
            const int done_rows = std::abs(at.row - from.row);
            const bool along_cols = at.row == to.row || (at.col != to.col && (done_cols + 0.5f) * static_cast<float>(rows) <= (done_rows + 0.5f) * static_cast<float>(cols));
            if (along_cols)
                at.col += to.col > at.col ? 1 : -1;
            else
                at.row += to.row > at.row ? 1 : -1;
            line.push_back(at);
        }
        return line;
    }

    std::vector<save::Tile> brush_square(save::Tile centre, int size)
    {
        std::vector<save::Tile> square;
        const int low = -(size - 1) / 2;
        for (int row = low; row < low + size; ++row)
        {
            for (int col = low; col < low + size; ++col)
                square.push_back({ centre.col + col, centre.row + row });
        }
        return square;
    }

    DragPlan plan_drag(std::span<const save::Tile> area, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::optional<const SolidTiles&> solids)
    {
        DragPlan plan;
        for (const save::Tile spot : area)
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

    std::uint8_t nearest_side(float across_col, float across_row) noexcept
    {
        // Distances to the edges towards higher rows, higher columns, lower rows and lower columns.
        const std::array<float, 4> to_edge { 1 - across_row, 1 - across_col, across_row, across_col };
        return static_cast<std::uint8_t>(std::ranges::min_element(to_edge) - to_edge.begin());
    }

    std::optional<PathEdge> path_under(const Ray& ray, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, bool through_ground)
    {
        const auto ground = ground_hit(ray, grid);
        std::optional<PathEdge> best;
        float nearest = ground && !through_ground ? *ground + 0.01f : kFar;
        glm::vec3 best_hit { 0.0f };
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            const save::PathTile& p = paths[i];
            if (!grid.contains(p.col, p.row))
                continue;
            float t = 0;
            if (!p.elevation || is_ground_queue(p))
            {
                if (!ground)
                    continue;
                t = *ground;
            }
            else
            {
                const save::PathElevation& e = *p.elevation;
                const float rise = path_rise(e.slope);
                const float middle = static_cast<float>(e.quantised_height) + (slope_falls(e.slope) ? -rise : rise) / 2;
                if (std::abs(ray.direction.y) < 1e-6f)
                    continue;
                t = (middle - ray.origin.y) / ray.direction.y;
            }
            if (t <= 0 || t > nearest)
                continue;
            const glm::vec3 hit = ray.origin + ray.direction * t;
            if (static_cast<int>(std::floor((hit.x - grid.origin_x) / grid.tile_size_x)) != p.col || static_cast<int>(std::floor((hit.z - grid.origin_z) / grid.tile_size_z)) != p.row)
                continue;
            nearest = t;
            best = PathEdge { i, std::nullopt };
            best_hit = hit;
        }
        if (!best)
            return std::nullopt;
        const save::PathTile& p = paths[best->path];
        const std::uint8_t side = nearest_side((best_hit.x - grid.world_x(p.col)) / grid.tile_size_x, (best_hit.z - grid.world_z(p.row)) / grid.tile_size_z);
        if (edge_height(p, grid, side))
            best->side = side;
        return best;
    }

    bool start_from_edge(FootpathBuilder& builder, const save::PathTile& path, const save::TerrainGrid& grid, std::uint8_t side)
    {
        const auto height = edge_height(path, grid, side);
        if (!height)
            return false;
        const save::Tile spot { path.col, path.row };
        start_at(builder, spot);
        builder.heading = side;
        builder.trail.push_back({ spot, 0, static_cast<std::int32_t>(std::lround(*height)) });
        return true;
    }

    std::optional<DoorEdge> door_under(const Ray& ray, std::span<const save::PlacementSpot> doors, const save::TerrainGrid& grid, bool through_ground)
    {
        if (std::abs(ray.direction.y) < 1e-6f)
            return std::nullopt;
        const auto ground = through_ground ? std::nullopt : ground_hit(ray, grid);
        float nearest = ground ? *ground + 0.01f : kFar;
        std::optional<DoorEdge> best;
        for (std::size_t i = 0; i < doors.size(); ++i)
        {
            const save::PlacementSpot& d = doors[i];
            const float t = (static_cast<float>(d.height) - ray.origin.y) / ray.direction.y;
            if (t <= 0 || t > nearest)
                continue;
            const glm::vec3 hit = ray.origin + ray.direction * t;
            const float across_col = (hit.x - grid.world_x(d.col)) / grid.tile_size_x;
            const float across_row = (hit.z - grid.world_z(d.row)) / grid.tile_size_z;
            if (across_col < 0 || across_col >= 1 || across_row < 0 || across_row >= 1)
                continue;
            nearest = t;
            best = DoorEdge { i, nearest_side(across_col, across_row) };
        }
        return best;
    }

    void start_from_door(FootpathBuilder& builder, const save::PlacementSpot& door, std::uint8_t side)
    {
        const save::Tile spot { door.col, door.row };
        start_at(builder, spot);
        builder.heading = side;
        builder.trail.push_back({ spot, 0, door.height });
    }

} // namespace rct3::park
