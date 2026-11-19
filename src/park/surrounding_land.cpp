// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/surrounding_land.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

namespace rct3::park
{

    namespace
    {

        /// A grid point on the map's edge, walking round it: along +X at the first row, along +Z at the last
        /// column, back along -X at the last row, back along -Z at the first column.
        struct EdgePoint
        {
            i32 x = 0;
            i32 z = 0;
        };

        std::vector<EdgePoint> edge_points(const save::model::TerrainGrid& g)
        {
            std::vector<EdgePoint> out;
            out.reserve(static_cast<usize>(2 * (g.width + g.height)));
            for (i32 x = 0; x < g.width; ++x)
                out.push_back({ x, 0 });
            for (i32 z = 0; z < g.height; ++z)
                out.push_back({ g.width, z });
            for (i32 x = g.width; x > 0; --x)
                out.push_back({ x, g.height });
            for (i32 z = g.height; z > 0; --z)
                out.push_back({ 0, z });
            return out;
        }

        /// The tile an edge point is a corner of, and which corner: the walk's points each have one, so
        /// the land meets that tile's ground exactly.
        struct EdgeCorner
        {
            i32 col = 0;
            i32 row = 0;
            i32 corner = 0;
        };

        EdgeCorner edge_corner(const save::model::TerrainGrid& g, EdgePoint p)
        {
            const i32 col = std::clamp(p.x, 0, g.width - 1);
            const i32 row = std::clamp(p.z, 0, g.height - 1);
            return { col, row, (p.x - col) + 2 * (p.z - row) };
        }

        f32 edge_height(const save::model::TerrainGrid& g, EdgePoint p)
        {
            const EdgeCorner c = edge_corner(g, p);
            return g.at(c.col, c.row).corners.at(static_cast<usize>(c.corner));
        }

        /// Each value averaged with its neighbours round the ring.
        std::vector<f32> smoothed(const std::vector<f32>& h)
        {
            const usize n = h.size();
            std::vector<f32> out(n);
            for (usize i = 0; i < n; ++i)
                out[i] = (h[(i + n - 1) % n] + 2 * h[i] + h[(i + 1) % n]) / 4;
            return out;
        }

        std::vector<f32> smoothed(std::vector<f32> h, i32 passes)
        {
            for (i32 pass = 0; pass < passes; ++pass)
                h = smoothed(h);
            return h;
        }

        f32 fade(f32 distance)
        {
            if (distance <= kSurroundFadeStart)
                return 1;
            return std::clamp(1 - (distance - kSurroundFadeStart) / (kSurroundReach - kSurroundFadeStart), 0.0f, 1.0f);
        }

        std::vector<f32> ring_distances()
        {
            std::vector<f32> out { 0 };
            for (f32 step = kSurroundFirstStep; out.back() < kSurroundReach; step *= kSurroundGrowth)
                out.push_back(std::min(out.back() + step, kSurroundReach));
            return out;
        }

        bool whole(const save::model::TerrainGrid& g)
        {
            return !g.empty() && g.cells.size() == static_cast<usize>(g.width) * static_cast<usize>(g.height);
        }

        /// For each point of `edge`, the level of the water on its tile when that stands over the point's ground;
        /// NaN elsewhere.
        std::vector<f32> sea_levels(const save::model::TerrainGrid& g, const std::vector<EdgePoint>& edge, const save::model::WaterLayer& water)
        {
            std::map<std::pair<i32, i32>, f32> on_edge; // Highest water on each edge tile
            for (const save::model::WaterBody& body : water.bodies)
                for (const save::model::WaterTri& t : body.tris)
                    if (g.contains(t.col, t.row) && (t.col == 0 || t.row == 0 || t.col == g.width - 1 || t.row == g.height - 1))
                    {
                        auto [it, _] = on_edge.try_emplace({ t.col, t.row }, body.level);
                        it->second = std::max(it->second, body.level);
                    }
            std::vector<f32> out(edge.size(), std::numeric_limits<f32>::quiet_NaN());
            for (usize i = 0; i < edge.size(); ++i)
            {
                const EdgeCorner c = edge_corner(g, edge[i]);
                if (const auto it = on_edge.find({ c.col, c.row }); it != on_edge.end() && it->second > edge_height(g, edge[i]))
                    out[i] = it->second;
            }
            return out;
        }

    } // namespace

    f32 edge_level(const save::model::TerrainGrid& grid)
    {
        if (!whole(grid))
            return 0;
        const std::vector<EdgePoint> edge = edge_points(grid);
        f32 sum = 0;
        for (const EdgePoint p : edge)
            sum += edge_height(grid, p);
        return sum / static_cast<f32>(edge.size());
    }

    LandRings land_rings(const save::model::TerrainGrid& grid, const save::model::WaterLayer& water)
    {
        LandRings out;
        if (!whole(grid))
            return out;
        const std::vector<EdgePoint> edge = edge_points(grid);
        out.points = edge.size();
        out.sea = sea_levels(grid, edge, water);
        // Each point's way along the edge to the nearest sea, and how far down the land there comes: all the way
        // at sea, less the farther off.
        out.shore.assign(out.points, std::numeric_limits<f32>::quiet_NaN());
        std::vector<f32> dip(out.points, 0.0f);
        for (usize i = 0; i < out.points; ++i)
        {
            for (usize d = 0; d < kShorePoints && std::isnan(out.shore[i]); ++d)
            {
                for (const usize j : { (i + d) % out.points, (i + out.points - d % out.points) % out.points })
                    if (!std::isnan(out.sea[j]) && std::isnan(out.shore[i]))
                    {
                        out.shore[i] = out.sea[j];
                        dip[i] = 1 - static_cast<f32>(d) / static_cast<f32>(kShorePoints);
                    }
            }
        }
        out.distances = ring_distances();
        const f32 level = edge_level(grid);
        std::vector<f32> heights(out.points);
        std::ranges::transform(edge, heights.begin(), [&](EdgePoint p) { return edge_height(grid, p); });
        out.heights.reserve(out.points * out.distances.size());
        for (usize k = 0; k < out.distances.size(); ++k)
        {
            if (k > 0)
            {
                heights = smoothed(std::move(heights), kSurroundSmoothing);
                for (f32& h : heights)
                    h = level + (h - level) * kSurroundKeep;
                for (usize i = 0; i < out.points; ++i)
                    if (!std::isnan(out.shore[i]))
                        heights[i] = std::min(heights[i], out.shore[i] - kSeaDepth * dip[i]);
            }
            out.heights.insert(out.heights.end(), heights.begin(), heights.end());
        }
        return out;
    }

    std::optional<f32> land_height(const LandRings& rings, const save::model::TerrainGrid& grid, f32 x, f32 z)
    {
        if (rings.points == 0 || rings.distances.size() < 2 || !whole(grid))
            return std::nullopt;
        const f32 x0 = grid.world_x(0);
        const f32 z0 = grid.world_z(0);
        const f32 x1 = grid.world_x(grid.width);
        const f32 z1 = grid.world_z(grid.height);
        // Rings are the map's rectangle grown by the same amount on every side.
        const f32 dx = std::max({ x0 - x, x - x1, 0.0f });
        const f32 dz = std::max({ z0 - z, z - z1, 0.0f });
        const f32 d = std::max(dx, dz);
        if (d <= 0 || d >= rings.distances.back())
            return std::nullopt;

        // Where along the edge walk the point is, in grid steps (see edge_points).
        const auto w = static_cast<f32>(grid.width);
        const auto h = static_cast<f32>(grid.height);
        const f32 across = (std::clamp(x, x0 - d, x1 + d) - (x0 - d)) / (x1 - x0 + 2 * d) * w;
        const f32 down = (std::clamp(z, z0 - d, z1 + d) - (z0 - d)) / (z1 - z0 + 2 * d) * h;
        f32 along = 0;
        if (dz >= dx && z < z0)
            along = across;
        else if (dx >= dz && x > x1)
            along = w + down;
        else if (dz >= dx && z > z1)
            along = w + h + (w - across);
        else
            along = 2 * w + h + (h - down);

        const auto ring = static_cast<usize>(std::ranges::upper_bound(rings.distances, d) - rings.distances.begin()) - 1;
        const f32 t = (d - rings.distances[ring]) / (rings.distances[ring + 1] - rings.distances[ring]);
        const auto n = rings.points;
        const auto i = static_cast<usize>(std::floor(along)) % n;
        const f32 s = along - std::floor(along);
        const auto height = [&](usize k, usize p) { return rings.heights[k * n + p % n]; };
        const f32 inner = height(ring, i) + (height(ring, i + 1) - height(ring, i)) * s;
        const f32 outer = height(ring + 1, i) + (height(ring + 1, i + 1) - height(ring + 1, i)) * s;
        const f32 ground = inner + (outer - inner) * t;
        const auto nearer = s < 0.5f ? i : (i + 1) % n;
        if (!rings.shore.empty() && !std::isnan(rings.shore[nearer]) && (!std::isnan(rings.sea[nearer]) || ground < rings.shore[nearer]))
            return std::nullopt; // Under water
        return ground;
    }

    TerrainMesh surrounding_land(const save::model::TerrainGrid& grid, const save::model::WaterLayer& water)
    {
        TerrainMesh mesh;
        if (!whole(grid))
            return mesh;

        const std::vector<EdgePoint> edge = edge_points(grid);
        const usize n = edge.size();
        if (n == 0)
            return mesh;
        const std::vector<f32> distances = ring_distances();
        const usize rings = distances.size();
        const auto at = [&](usize k, usize i) { return k * n + i % n; };

        // The surfaces round the edge as the park's ground shows them: its land's paint only, not paths or cliffs.
        std::map<u8, std::vector<f32>> shares;
        for (const EdgePoint p : edge)
        {
            const EdgeCorner c = edge_corner(grid, p);
            shares[grid.at(c.col, c.row).surface].resize(n * rings);
        }
        for (auto& [surface, share] : shares)
        {
            for (usize i = 0; i < n; ++i)
                share[i] = surface_share(grid, edge[i].x, edge[i].z, surface);
        }

        // Heights from the shape; surfaces ring by ring from the ring inside.
        const LandRings shape = land_rings(grid, water);
        for (usize k = 1; k < rings; ++k)
        {
            for (auto& [_, share] : shares)
            {
                const std::vector<f32> inside = smoothed({ share.begin() + static_cast<std::ptrdiff_t>((k - 1) * n), share.begin() + static_cast<std::ptrdiff_t>(k * n) }, kSurroundSmoothing);
                std::ranges::copy(inside, share.begin() + static_cast<std::ptrdiff_t>(k * n));
            }
        }
        const f32 x0 = grid.world_x(0);
        const f32 z0 = grid.world_z(0);
        const f32 width = grid.world_x(grid.width) - x0;
        const f32 depth = grid.world_z(grid.height) - z0;
        // Point `i` of the ring `d` out, at height `y`.
        const auto ring_point = [&](f32 d, usize i, f32 y) {
            const f32 u = static_cast<f32>(edge[i % n].x) / static_cast<f32>(grid.width);
            const f32 v = static_cast<f32>(edge[i % n].z) / static_cast<f32>(grid.height);
            return glm::vec3(x0 - d + u * (width + 2 * d), y, z0 - d + v * (depth + 2 * d));
        };
        std::vector<glm::vec3> points;
        points.reserve(n * rings);
        for (usize k = 0; k < rings; ++k)
            for (usize i = 0; i < n; ++i)
                points.push_back(ring_point(distances[k], i, shape.heights[at(k, i)]));

        // The sea and the water by its shore: between each two points either is off, over every ring and on to
        // kSeaReach. Where the land rises out of it, it is under the land.
        for (usize i = 0; i < n; ++i)
        {
            f32 a = shape.shore[i];
            f32 b = shape.shore[(i + 1) % n];
            if (std::isnan(a) && std::isnan(b))
                continue;
            a = std::isnan(a) ? b : a;
            b = std::isnan(b) ? a : b;
            const auto water_at = [&](usize k, usize p, f32 level) -> WaterVertex {
                const f32 d = k < rings ? distances[k] : kSeaReach;
                const f32 ground = k < rings ? shape.heights[at(k, p)] : level - kSeaDepth;
                return { ring_point(d, p, level), kWaterColour, std::max(level - ground, 0.0f) };
            };
            for (usize k = 0; k < rings; ++k)
            {
                const std::array q { water_at(k, i, a), water_at(k, i + 1, b), water_at(k + 1, i, a), water_at(k + 1, i + 1, b) };
                mesh.water.insert(mesh.water.end(), { q[0], q[2], q[1], q[1], q[2], q[3] });
            }
        }

        // Normals: the park ground's own at the edge, so the light carries on across it; elsewhere the faces
        // round each point.
        std::vector<glm::vec3> normals(points.size(), glm::vec3(0.0f));
        for (usize k = 0; k + 1 < rings; ++k)
        {
            for (usize i = 0; i < n; ++i)
            {
                const std::array<usize, 4> q { at(k, i), at(k, i + 1), at(k + 1, i), at(k + 1, i + 1) };
                const glm::vec3 face = glm::cross(points[q[1]] - points[q[0]], points[q[2]] - points[q[0]]); // Pointing up
                for (const usize p : q)
                    normals[p] += face;
            }
        }
        for (usize i = 0; i < n; ++i)
        {
            const EdgeCorner c = edge_corner(grid, edge[i]);
            normals[i] = ground_normal(grid, { c.col, c.row }, c.corner);
        }

        // One layer per surface, lowest first. A layer's coefficient in the final colour is its alpha times
        // the cover the layers above leave it: with alpha = f w / (1 - f A), A the share of the layers above,
        // that comes to f w, and what is behind keeps 1 - f.
        std::vector<f32> above(points.size(), 0.0f);
        for (const auto& [_, share] : shares)
        {
            for (usize p = 0; p < points.size(); ++p)
                above[p] += share[p];
        }
        for (const auto& [surface, share] : shares)
        {
            const auto base = static_cast<u32>(mesh.vertices.size());
            for (usize p = 0; p < points.size(); ++p)
            {
                above[p] -= share[p];
                const f32 f = fade(distances[p / n]);
                const f32 cover = 1 - f * above[p];
                const f32 alpha = cover > 0 ? std::clamp(f * share[p] / cover, 0.0f, 1.0f) : 0;
                const glm::vec3& q = points[p];
                mesh.vertices.push_back({ q, { 1, 1, 1, alpha }, { q.x / kTextureSpan, q.z / kTextureSpan }, normals[p] });
            }
            GroundBatch batch { GroundPaint::Surface, surface, static_cast<u32>(mesh.indices.size()), 0 };
            for (usize k = 0; k + 1 < rings; ++k)
            {
                for (usize i = 0; i < n; ++i)
                {
                    const std::array<usize, 4> q { at(k, i), at(k, i + 1), at(k + 1, i), at(k + 1, i + 1) };
                    if (std::ranges::all_of(q, [&](usize p) { return share[p] <= 0; }))
                        continue;
                    const auto v = [&](usize p) { return base + static_cast<u32>(p); };
                    mesh.indices.insert(mesh.indices.end(), { v(q[0]), v(q[2]), v(q[1]), v(q[1]), v(q[2]), v(q[3]) });
                }
            }
            batch.count = static_cast<u32>(mesh.indices.size()) - batch.first;
            mesh.batches.push_back(batch);
        }

        const f32 far = mesh.water.empty() ? distances.back() : kSeaReach;
        f32 top = std::ranges::max(points, { }, &glm::vec3::y).y;
        for (const WaterVertex& w : mesh.water)
            top = std::max(top, w.position.y);
        mesh.low = { x0 - far, std::ranges::min(points, { }, &glm::vec3::y).y, z0 - far };
        mesh.high = { x0 + width + far, top, z0 + depth + far };
        return mesh;
    }

} // namespace rct3::park
