// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/surrounding_land.hpp"

#include "park/lighting.hpp"

#include <algorithm>
#include <array>
#include <map>

namespace rct3::park
{

    namespace
    {

        /// A grid point on the map's edge, walking round it: along +X at the first row, along +Z at the last
        /// column, back along -X at the last row, back along -Z at the first column.
        struct EdgePoint
        {
            int x = 0;
            int z = 0;
        };

        std::vector<EdgePoint> edge_points(const save::TerrainGrid& g)
        {
            std::vector<EdgePoint> out;
            out.reserve(static_cast<std::size_t>(2 * (g.width + g.height)));
            for (int x = 0; x < g.width; ++x)
                out.push_back({ x, 0 });
            for (int z = 0; z < g.height; ++z)
                out.push_back({ g.width, z });
            for (int x = g.width; x > 0; --x)
                out.push_back({ x, g.height });
            for (int z = g.height; z > 0; --z)
                out.push_back({ 0, z });
            return out;
        }

        /// The tile an edge point is a corner of, and which corner: the walk's points each have one, so
        /// the land meets that tile's ground exactly.
        struct EdgeCorner
        {
            int col = 0;
            int row = 0;
            int corner = 0;
        };

        EdgeCorner edge_corner(const save::TerrainGrid& g, EdgePoint p)
        {
            const int col = std::clamp(p.x, 0, g.width - 1);
            const int row = std::clamp(p.z, 0, g.height - 1);
            return { col, row, (p.x - col) + 2 * (p.z - row) };
        }

        float edge_height(const save::TerrainGrid& g, EdgePoint p)
        {
            const EdgeCorner c = edge_corner(g, p);
            return g.at(c.col, c.row).corners.at(static_cast<std::size_t>(c.corner));
        }

        /// Each value averaged with its neighbours round the ring.
        std::vector<float> smoothed(const std::vector<float>& h)
        {
            const std::size_t n = h.size();
            std::vector<float> out(n);
            for (std::size_t i = 0; i < n; ++i)
                out[i] = (h[(i + n - 1) % n] + 2 * h[i] + h[(i + 1) % n]) / 4;
            return out;
        }

        std::vector<float> smoothed(std::vector<float> h, int passes)
        {
            for (int pass = 0; pass < passes; ++pass)
                h = smoothed(h);
            return h;
        }

        float fade(float distance)
        {
            if (distance <= kSurroundFadeStart)
                return 1;
            return std::clamp(1 - (distance - kSurroundFadeStart) / (kSurroundReach - kSurroundFadeStart), 0.0f, 1.0f);
        }

        std::vector<float> ring_distances()
        {
            std::vector<float> out { 0 };
            for (float step = kSurroundFirstStep; out.back() < kSurroundReach; step *= kSurroundGrowth)
                out.push_back(std::min(out.back() + step, kSurroundReach));
            return out;
        }

        bool whole(const save::TerrainGrid& g)
        {
            return !g.empty() && g.cells.size() == static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height);
        }

    } // namespace

    float edge_level(const save::TerrainGrid& grid)
    {
        if (!whole(grid))
            return 0;
        const std::vector<EdgePoint> edge = edge_points(grid);
        float sum = 0;
        for (const EdgePoint p : edge)
            sum += edge_height(grid, p);
        return sum / static_cast<float>(edge.size());
    }

    LandRings land_rings(const save::TerrainGrid& grid)
    {
        LandRings out;
        if (!whole(grid))
            return out;
        const std::vector<EdgePoint> edge = edge_points(grid);
        out.points = edge.size();
        out.distances = ring_distances();
        const float level = edge_level(grid);
        std::vector<float> heights(out.points);
        std::ranges::transform(edge, heights.begin(), [&](EdgePoint p) { return edge_height(grid, p); });
        out.heights.reserve(out.points * out.distances.size());
        for (std::size_t k = 0; k < out.distances.size(); ++k)
        {
            if (k > 0)
            {
                heights = smoothed(std::move(heights), kSurroundSmoothing);
                for (float& h : heights)
                    h = level + (h - level) * kSurroundKeep;
            }
            out.heights.insert(out.heights.end(), heights.begin(), heights.end());
        }
        return out;
    }

    std::optional<float> land_height(const LandRings& rings, const save::TerrainGrid& grid, float x, float z)
    {
        if (rings.points == 0 || rings.distances.size() < 2 || !whole(grid))
            return std::nullopt;
        const float x0 = grid.world_x(0);
        const float z0 = grid.world_z(0);
        const float x1 = grid.world_x(grid.width);
        const float z1 = grid.world_z(grid.height);
        // Rings are the map's rectangle grown by the same amount on every side.
        const float dx = std::max({ x0 - x, x - x1, 0.0f });
        const float dz = std::max({ z0 - z, z - z1, 0.0f });
        const float d = std::max(dx, dz);
        if (d <= 0 || d >= rings.distances.back())
            return std::nullopt;

        // Where along the edge walk the point is, in grid steps (see edge_points).
        const auto w = static_cast<float>(grid.width);
        const auto h = static_cast<float>(grid.height);
        const float across = (std::clamp(x, x0 - d, x1 + d) - (x0 - d)) / (x1 - x0 + 2 * d) * w;
        const float down = (std::clamp(z, z0 - d, z1 + d) - (z0 - d)) / (z1 - z0 + 2 * d) * h;
        float along = 0;
        if (dz >= dx && z < z0)
            along = across;
        else if (dx >= dz && x > x1)
            along = w + down;
        else if (dz >= dx && z > z1)
            along = w + h + (w - across);
        else
            along = 2 * w + h + (h - down);

        const auto ring = static_cast<std::size_t>(std::ranges::upper_bound(rings.distances, d) - rings.distances.begin()) - 1;
        const float t = (d - rings.distances[ring]) / (rings.distances[ring + 1] - rings.distances[ring]);
        const auto n = rings.points;
        const auto i = static_cast<std::size_t>(std::floor(along)) % n;
        const float s = along - std::floor(along);
        const auto height = [&](std::size_t k, std::size_t p) { return rings.heights[k * n + p % n]; };
        const float inner = height(ring, i) + (height(ring, i + 1) - height(ring, i)) * s;
        const float outer = height(ring + 1, i) + (height(ring + 1, i + 1) - height(ring + 1, i)) * s;
        return inner + (outer - inner) * t;
    }

    TerrainMesh surrounding_land(const save::TerrainGrid& grid)
    {
        TerrainMesh mesh;
        if (!whole(grid))
            return mesh;

        const std::vector<EdgePoint> edge = edge_points(grid);
        const std::size_t n = edge.size();
        const std::vector<float> distances = ring_distances();
        const std::size_t rings = distances.size();
        const auto at = [&](std::size_t k, std::size_t i) { return k * n + i % n; };

        // The surfaces round the edge as the park's ground shows them, and the commonest one.
        std::map<std::uint8_t, std::vector<float>> shares;
        std::map<std::uint8_t, std::size_t> counts;
        for (const EdgePoint p : edge)
        {
            const EdgeCorner c = edge_corner(grid, p);
            const std::uint8_t surface = grid.at(c.col, c.row).surface;
            ++counts[surface];
            if (!shares.contains(surface))
                shares[surface].resize(n * rings);
        }
        const std::uint8_t commonest = std::ranges::max_element(counts, { }, [](const auto& c) { return c.second; })->first;
        for (auto& [surface, share] : shares)
        {
            for (std::size_t i = 0; i < n; ++i)
                share[i] = surface_share(grid, edge[i].x, edge[i].z, surface);
        }

        // Heights from the shape; surfaces ring by ring from the ring inside.
        const LandRings shape = land_rings(grid);
        for (std::size_t k = 1; k < rings; ++k)
        {
            for (auto& [surface, share] : shares)
            {
                const std::vector<float> inside = smoothed({ share.begin() + static_cast<std::ptrdiff_t>((k - 1) * n), share.begin() + static_cast<std::ptrdiff_t>(k * n) }, kSurroundSmoothing);
                const float settled = surface == commonest ? kSurroundSettle : 0;
                for (std::size_t i = 0; i < n; ++i)
                    share[at(k, i)] = inside[i] * (1 - kSurroundSettle) + settled;
            }
        }
        const float x0 = grid.world_x(0);
        const float z0 = grid.world_z(0);
        const float width = grid.world_x(grid.width) - x0;
        const float depth = grid.world_z(grid.height) - z0;
        std::vector<glm::vec3> points;
        points.reserve(n * rings);
        for (std::size_t k = 0; k < rings; ++k)
        {
            const float d = distances[k];
            for (std::size_t i = 0; i < n; ++i)
            {
                const float u = static_cast<float>(edge[i].x) / static_cast<float>(grid.width);
                const float v = static_cast<float>(edge[i].z) / static_cast<float>(grid.height);
                points.emplace_back(x0 - d + u * (width + 2 * d), shape.heights[at(k, i)], z0 - d + v * (depth + 2 * d));
            }
        }

        // Normals: the park ground's own at the edge, so the light carries on across it; elsewhere the faces
        // round each point.
        std::vector<glm::vec3> normals(points.size(), glm::vec3(0.0f));
        for (std::size_t k = 0; k + 1 < rings; ++k)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::array<std::size_t, 4> q { at(k, i), at(k, i + 1), at(k + 1, i), at(k + 1, i + 1) };
                const glm::vec3 face = glm::cross(points[q[2]] - points[q[0]], points[q[1]] - points[q[0]]);
                for (const std::size_t p : q)
                    normals[p] += face;
            }
        }
        for (std::size_t i = 0; i < n; ++i)
        {
            const EdgeCorner c = edge_corner(grid, edge[i]);
            normals[i] = ground_normal(grid, c.col, c.row, c.corner);
        }

        // One layer per surface, lowest first. A layer's coefficient in the final colour is its alpha times
        // the cover the layers above leave it: with alpha = f w / (1 - f A), A the share of the layers above,
        // that comes to f w, and what is behind keeps 1 - f.
        std::vector<float> above(points.size(), 0.0f);
        for (const auto& [surface, share] : shares)
        {
            for (std::size_t p = 0; p < points.size(); ++p)
                above[p] += share[p];
        }
        for (const auto& [surface, share] : shares)
        {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (std::size_t p = 0; p < points.size(); ++p)
            {
                above[p] -= share[p];
                const float f = fade(distances[p / n]);
                const float cover = 1 - f * above[p];
                const float alpha = cover > 0 ? std::clamp(f * share[p] / cover, 0.0f, 1.0f) : 0;
                const float s = sunlight(normals[p]);
                const glm::vec3& q = points[p];
                mesh.vertices.push_back({ q, { s, s, s, alpha }, { q.x / kTextureSpan, q.z / kTextureSpan } });
            }
            GroundBatch batch { GroundPaint::Surface, surface, static_cast<std::uint32_t>(mesh.indices.size()), 0 };
            for (std::size_t k = 0; k + 1 < rings; ++k)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::array<std::size_t, 4> q { at(k, i), at(k, i + 1), at(k + 1, i), at(k + 1, i + 1) };
                    if (std::ranges::all_of(q, [&](std::size_t p) { return share[p] <= 0; }))
                        continue;
                    const auto v = [&](std::size_t p) { return base + static_cast<std::uint32_t>(p); };
                    mesh.indices.insert(mesh.indices.end(), { v(q[0]), v(q[2]), v(q[1]), v(q[1]), v(q[2]), v(q[3]) });
                }
            }
            batch.count = static_cast<std::uint32_t>(mesh.indices.size()) - batch.first;
            mesh.batches.push_back(batch);
        }

        const float far = distances.back();
        mesh.low = { x0 - far, std::ranges::min(points, { }, &glm::vec3::y).y, z0 - far };
        mesh.high = { x0 + width + far, std::ranges::max(points, { }, &glm::vec3::y).y, z0 + depth + far };
        return mesh;
    }

} // namespace rct3::park
