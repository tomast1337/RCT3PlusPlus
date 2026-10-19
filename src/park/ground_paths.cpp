// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_paths.hpp"

#include "park/ground_queues.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace rct3::park
{

    PathCell path_cell(PathLook look) noexcept
    {
        switch (look)
        {
            case PathLook::EdgeXJoined:
                return { 0, 0 };
            case PathLook::CornerOnly:
                return { 0, 1 };
            case PathLook::Open:
                return { 0, 2 };
            case PathLook::EdgeZJoined:
                return { 0, 3 };
            case PathLook::EdgeX:
                return { 1, 0 };
            case PathLook::Closed:
                return { 1, 1 };
            case PathLook::Notched:
                return { 1, 2 };
            case PathLook::EdgeZ:
                return { 1, 3 };
        }
        return { 0, 2 };
    }

    PathLook quarter_look(bool along_x, bool along_z, bool across) noexcept
    {
        if (along_x && along_z)
            return across ? PathLook::Open : PathLook::Notched;
        if (along_z)
            return across ? PathLook::EdgeXJoined : PathLook::EdgeX;
        if (along_x)
            return across ? PathLook::EdgeZJoined : PathLook::EdgeZ;
        return PathLook::Closed;
    }

    float path_rise(std::uint8_t slope) noexcept
    {
        switch (slope)
        {
            case 1:
            case 2:
                return 2; // Steep
            case 4:
            case 5:
                return 1; // Gentle
            default:
                return 0;
        }
    }

    GroundPathMap ground_path_map(std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        GroundPathMap map;
        map.width = grid.width;
        map.height = grid.height;
        map.tiles.assign(static_cast<std::size_t>(std::max(grid.width, 0)) * static_cast<std::size_t>(std::max(grid.height, 0)), kNoPath);
        for (const save::PathTile& p : paths)
        {
            if (!grid.contains(p.col, p.row))
                continue;
            const std::size_t at = static_cast<std::size_t>(p.row) * static_cast<std::size_t>(map.width) + p.col;
            if (is_ground_queue(p))
            {
                auto& sides = map.queues[at];
                for (const std::uint8_t d : { p.queue->start_direction, p.queue->end_direction })
                {
                    if (const auto side = side_of(d))
                        sides = static_cast<std::uint8_t>(sides | 1u << static_cast<unsigned>(*side));
                }
                continue;
            }
            if (p.elevation)
            {
                // Which end the stored height is at is not needed: either end may meet the ground.
                const auto stored = static_cast<float>(p.elevation->quantised_height);
                const float rise = path_rise(p.elevation->slope);
                auto& ends = map.raised[at];
                for (const float h : { stored - rise, stored, stored + rise })
                    ends.push_back(h);
                continue;
            }
            if (p.kind != save::PathKind::Ground || p.surface_name.empty())
                continue;
            auto style = std::ranges::find(map.styles, p.surface_name);
            if (style == map.styles.end())
            {
                if (map.styles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
                    continue;
                map.styles.push_back(p.surface_name);
                style = std::prev(map.styles.end());
            }
            map.tiles[at] = static_cast<std::int16_t>(style - map.styles.begin());
        }
        return map;
    }

    std::vector<RideFloor> ride_floors(std::span<const save::Ride> rides, const std::function<std::optional<std::array<std::int32_t, 2>>(std::uint64_t object)>& squares_of, const save::TerrainGrid& grid)
    {
        std::vector<RideFloor> floors;
        for (const save::Ride& ride : rides)
        {
            if (ride.kind != save::RideKind::Flat || !ride.base.spot)
                continue;
            const save::PlacementSpot& spot = *ride.base.spot;
            const auto squares = squares_of(spot.object);
            if (!squares)
                continue;
            save::PlacedObject placed;
            placed.col = spot.col;
            placed.row = spot.row;
            placed.rotation = spot.direction;
            const std::array span = spot.direction % 2 == 0 ? *squares : std::array { (*squares)[1], (*squares)[0] };
            const glm::vec2 middle = footprint_middle(placed, *squares);
            RideFloor floor;
            floor.col0 = static_cast<int>(std::lround(middle.x - static_cast<float>(span[0]) / 2));
            floor.row0 = static_cast<int>(std::lround(middle.y - static_cast<float>(span[1]) / 2));
            floor.col1 = floor.col0 + span[0] - 1;
            floor.row1 = floor.row0 + span[1] - 1;
            // A ride raised off the ground or sunk into it stands on its own platform, not on paving.
            bool level = true;
            for (int r = floor.row0; r <= floor.row1 && level; ++r)
            {
                for (int c = floor.col0; c <= floor.col1 && level; ++c)
                    level = grid.contains(c, r) && std::ranges::all_of(grid.at(c, r).corners, [&](float h) { return h == static_cast<float>(spot.height); });
            }
            if (!level)
                continue;
            if (!ride.stations.empty() && ride.stations.front().exit_spot)
            {
                // A door faces the ride; what it lets out onto is behind it.
                const save::PlacementSpot& exit = *ride.stations.front().exit_spot;
                const glm::vec2 back = turned_x(exit.direction + 2);
                floor.beyond_exit = std::array { exit.col + static_cast<int>(std::lround(back.x)), exit.row + static_cast<int>(std::lround(back.y)) };
            }
            floors.push_back(floor);
        }
        return floors;
    }

    void pave_ride_floors(GroundPathMap& map, std::span<const RideFloor> floors)
    {
        const auto style_index = [&](std::string_view name) -> std::optional<std::int16_t> {
            auto style = std::ranges::find(map.styles, name);
            if (style == map.styles.end())
            {
                if (map.styles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
                    return std::nullopt;
                map.styles.emplace_back(name);
                style = std::prev(map.styles.end());
            }
            return static_cast<std::int16_t>(style - map.styles.begin());
        };
        for (const RideFloor& floor : floors)
        {
            std::optional<std::int16_t> style;
            if (floor.beyond_exit && has_ground_path(map, (*floor.beyond_exit)[0], (*floor.beyond_exit)[1]))
                style = map.tiles[static_cast<std::size_t>((*floor.beyond_exit)[1]) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>((*floor.beyond_exit)[0])];
            else
                style = style_index(kRideFloorStyle);
            if (!style)
                continue;
            for (int r = std::max(floor.row0, 0); r <= std::min(floor.row1, map.height - 1); ++r)
            {
                for (int c = std::max(floor.col0, 0); c <= std::min(floor.col1, map.width - 1); ++c)
                {
                    std::int16_t& tile = map.tiles[static_cast<std::size_t>(r) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(c)];
                    if (tile == kNoPath)
                        tile = *style;
                }
            }
        }
    }

    bool has_ground_path(const GroundPathMap& map, int col, int row) noexcept
    {
        if (col < 0 || row < 0 || col >= map.width || row >= map.height)
            return false;
        return map.tiles[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col)] != kNoPath;
    }

    bool path_continues(const GroundPathMap& map, const save::TerrainGrid& grid, int col, int row, int dc, int dr)
    {
        if (has_ground_path(map, col + dc, row + dr))
            return true;
        if (!grid.contains(col, row) || !grid.contains(col + dc, row + dr))
            return false;
        const std::size_t there = static_cast<std::size_t>(row + dr) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col + dc);
        if (const auto queue = map.queues.find(there); queue != map.queues.end())
        {
            // The neighbour's side facing back towards this tile.
            const TileSide facing = dc > 0 ? TileSide::Left : dc < 0 ? TileSide::Right : dr > 0 ? TileSide::Back : TileSide::Front;
            if ((queue->second & 1u << static_cast<unsigned>(facing)) != 0)
                return true;
        }
        const auto found = map.raised.find(there);
        if (found == map.raised.end())
            return false;
        // This tile's two corners on the shared edge.
        const auto& corners = grid.at(col, row).corners;
        const std::size_t a = dc > 0 ? 1 : dr > 0 ? 2 : 0;
        const std::size_t b = dc != 0 ? a + 2 : a + 1;
        const float edge = (corners[a] + corners[b]) / 2;
        return std::ranges::any_of(found->second, [&](float h) { return std::abs(h - edge) <= kPathMeets; });
    }

    namespace
    {

        /// A point of a tile, (u, v) from 0 to 1 across it along x and z, on the tile's two ground
        /// triangles: (0, 1, 2) where u + v <= 1, (3, 2, 1) beyond.
        template<typename T> T on_tile(const std::array<T, 4>& at, float u, float v)
        {
            if (u + v <= 1)
                return at[0] + (at[1] - at[0]) * u + (at[2] - at[0]) * v;
            return at[3] + (at[2] - at[3]) * (1 - u) + (at[1] - at[3]) * (1 - v);
        }

        /// The texture coordinates of a span of a cell, (u, v) from 0 to 1 across it. Kept half a texel off
        /// the cell's border so the next cell never bleeds in.
        glm::vec2 cell_uv(std::uint8_t cell, float u, float v)
        {
            constexpr float kInset = 1.0f / 256;
            const glm::vec2 origin { (cell & 1) != 0 ? 0.5f : 0.0f, (cell & 2) != 0 ? 0.5f : 0.0f };
            return origin + glm::vec2(kInset + u * (0.5f - 2 * kInset), kInset + v * (0.5f - 2 * kInset));
        }

        using Key = std::pair<GroundPaint, std::uint8_t>;

        struct Builder
        {
            std::map<Key, std::vector<GroundVertex>> by_paint;

            /// The part of a tile from (u0, v0) to (u1, v1) painted with `look`, split along the same
            /// diagonal as the ground so it lies in the ground's triangles.
            void span(std::uint8_t style, PathLook look, const std::array<glm::vec3, 4>& corners, const std::array<glm::vec3, 4>& normals, float u0, float v0, float u1, float v1)
            {
                const PathCell cell = path_cell(look);
                auto& out = by_paint[{ cell.sheet == 0 ? GroundPaint::PathA : GroundPaint::PathB, style }];
                std::array<GroundVertex, 4> q;
                for (int k = 0; k < 4; ++k)
                {
                    const float u = (k & 1) != 0 ? u1 : u0;
                    const float v = (k & 2) != 0 ? v1 : v0;
                    q[static_cast<std::size_t>(k)] = { on_tile(corners, u, v) + glm::vec3(0.0f, kPathLift, 0.0f), glm::vec4(1.0f), cell_uv(cell.cell, u, v), on_tile(normals, u, v) };
                }
                for (const int k : { 0, 1, 2, 3, 2, 1 })
                    out.push_back(q[static_cast<std::size_t>(k)]);
            }
        };

    } // namespace

    TerrainMesh ground_path_mesh(const GroundPathMap& map, const save::TerrainGrid& grid)
    {
        TerrainMesh mesh;
        if (map.width != grid.width || map.height != grid.height || map.tiles.size() != grid.cells.size())
            return mesh;

        Builder b;
        bool any = false;
        for (int row = 0; row < grid.height; ++row)
        {
            for (int col = 0; col < grid.width; ++col)
            {
                const std::int16_t style = map.tiles[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(col)];
                if (style == kNoPath)
                    continue;
                std::array<PathLook, 4> looks { };
                for (int k = 0; k < 4; ++k)
                {
                    const int dc = (k & 1) != 0 ? 1 : -1;
                    const int dr = (k & 2) != 0 ? 1 : -1;
                    looks[static_cast<std::size_t>(k)] = quarter_look(path_continues(map, grid, col, row, dc, 0), path_continues(map, grid, col, row, 0, dr), has_ground_path(map, col + dc, row + dr));
                }

                const save::TerrainCell& cell = grid.at(col, row);
                std::array<glm::vec3, 4> corners;
                std::array<glm::vec3, 4> normals;
                for (int c = 0; c < 4; ++c)
                {
                    const auto cu = static_cast<std::size_t>(c);
                    corners[cu] = { grid.world_x(col + c % 2), cell.corners[cu], grid.world_z(row + c / 2) };
                    normals[cu] = glm::normalize(ground_normal(grid, col, row, c));
                }
                const auto s = static_cast<std::uint8_t>(style);
                if (std::ranges::all_of(looks, [&](PathLook l) { return l == looks[0]; }))
                {
                    b.span(s, looks[0], corners, normals, 0, 0, 1, 1);
                }
                else
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        const float u = (k & 1) != 0 ? 0.5f : 0.0f;
                        const float v = (k & 2) != 0 ? 0.5f : 0.0f;
                        b.span(s, looks[static_cast<std::size_t>(k)], corners, normals, u, v, u + 0.5f, v + 0.5f);
                    }
                }
                const glm::vec3 low { corners[0].x, cell.min_height(), corners[0].z };
                const glm::vec3 high { corners[3].x, cell.max_height() + kPathLift, corners[3].z };
                mesh.low = any ? glm::min(mesh.low, low) : low;
                mesh.high = any ? glm::max(mesh.high, high) : high;
                any = true;
            }
        }

        for (auto& [key, vertices] : b.by_paint)
        {
            GroundBatch batch { key.first, key.second, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(vertices.size()) };
            for (GroundVertex& v : vertices)
            {
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }
            mesh.batches.push_back(batch);
        }
        return mesh;
    }

} // namespace rct3::park
