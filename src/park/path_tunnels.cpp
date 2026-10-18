// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_tunnels.hpp"

#include "park/ground_paths.hpp"
#include "park/queue_fences.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <utility>

namespace rct3::park
{

    std::vector<GroundOpening> tunnel_hollows(std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        std::vector<GroundOpening> hollows;
        for (const save::PathTile& p : paths)
        {
            if (!p.elevation || !p.elevation->underground || !grid.contains(p.col, p.row))
                continue;
            const float rise = path_rise(p.elevation->slope);
            const float low = static_cast<float>(p.elevation->quantised_height) - (slope_falls(p.elevation->slope) ? rise : 0);
            const GroundOpening hollow { p.col, p.row, low - kTunnelFloor, low + rise + kTunnelHeight };
            if (grid.at(p.col, p.row).min_height() >= hollow.high)
                hollows.push_back(hollow);
        }
        return hollows;
    }

    TerrainMesh tunnel_lining(std::span<const GroundOpening> hollows, const save::TerrainGrid& grid)
    {
        TerrainMesh mesh;
        std::multimap<std::pair<int, int>, const GroundOpening*> by_tile;
        for (const GroundOpening& h : hollows)
            by_tile.emplace(std::pair { h.col, h.row }, &h);
        std::map<std::pair<GroundPaint, std::uint8_t>, std::vector<GroundVertex>> by_paint;
        const glm::vec2 span { grid.tile_size_x, grid.tile_size_z };
        const auto quad = [&](std::uint8_t cliff, const std::array<glm::vec3, 4>& q, glm::vec3 normal, bool along_x) {
            const GroundPaint paint = normal.y != 0 ? GroundPaint::TunnelCeiling : GroundPaint::TunnelWall;
            auto& out = by_paint[{ paint, cliff }];
            for (const std::size_t i : { 0, 1, 2, 0, 2, 3 })
            {
                const glm::vec3 p = q[i];
                const glm::vec2 uv = normal.y != 0 ? glm::vec2 { p.x, p.z } / span : glm::vec2 { along_x ? p.x / span.x : p.z / span.y, -p.y / kTunnelHeight };
                out.push_back({ p, glm::vec4(1.0f), uv, normal });
            }
        };
        for (const GroundOpening& h : hollows)
        {
            const std::uint8_t cliff = grid.at(h.col, h.row).cliff;
            const float x0 = grid.world_x(h.col);
            const float x1 = grid.world_x(h.col + 1);
            const float z0 = grid.world_z(h.row);
            const float z1 = grid.world_z(h.row + 1);
            // Ceiling, facing down.
            quad(cliff, { glm::vec3 { x0, h.high, z0 }, { x1, h.high, z0 }, { x1, h.high, z1 }, { x0, h.high, z1 } }, { 0, -1, 0 }, true);
            // Sides as the builder's headings: towards higher rows, higher columns, lower rows, lower columns.
            constexpr std::array<std::array<int, 2>, 4> kStep { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
            // The neighbour's corners on the shared edge, in the terrain's corner order.
            constexpr std::array<std::array<std::size_t, 2>, 4> kFacing { { { 0, 1 }, { 0, 2 }, { 2, 3 }, { 1, 3 } } };
            for (std::size_t side = 0; side < 4; ++side)
            {
                const int col = h.col + kStep[side][0];
                const int row = h.row + kStep[side][1];
                const auto [first, last] = by_tile.equal_range({ col, row });
                if (std::any_of(first, last, [&](const auto& other) { return other.second->low < h.high && h.low < other.second->high; }))
                    continue;
                // Open where the ground beyond is lower than the hollow's top: that is the tunnel's mouth.
                if (grid.contains(col, row))
                {
                    const auto& corners = grid.at(col, row).corners;
                    if (std::min(corners[kFacing[side][0]], corners[kFacing[side][1]]) < h.high)
                        continue;
                }
                const bool along_x = side % 2 == 0;
                const float x = side == 1 ? x1 : x0;
                const float z = side == 0 ? z1 : z0;
                const glm::vec3 inward { -static_cast<float>(kStep[side][0]), 0, -static_cast<float>(kStep[side][1]) };
                const glm::vec3 a = along_x ? glm::vec3 { x0, 0, z } : glm::vec3 { x, 0, z0 };
                const glm::vec3 b = along_x ? glm::vec3 { x1, 0, z } : glm::vec3 { x, 0, z1 };
                std::array<glm::vec3, 4> wall { glm::vec3 { a.x, h.low, a.z }, { b.x, h.low, b.z }, { b.x, h.high, b.z }, { a.x, h.high, a.z } };
                // Wound to face into the hollow.
                if (glm::dot(glm::cross(wall[1] - wall[0], wall[2] - wall[0]), inward) < 0)
                    std::swap(wall[1], wall[3]);
                quad(cliff, wall, inward, along_x);
            }
        }
        for (auto& [paint, vertices] : by_paint)
        {
            mesh.batches.push_back({ paint.first, paint.second, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(vertices.size()) });
            for (GroundVertex& v : vertices)
            {
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.low = mesh.vertices.empty() ? v.position : glm::min(mesh.low, v.position);
                mesh.high = mesh.vertices.empty() ? v.position : glm::max(mesh.high, v.position);
                mesh.vertices.push_back(v);
            }
        }
        return mesh;
    }

} // namespace rct3::park
