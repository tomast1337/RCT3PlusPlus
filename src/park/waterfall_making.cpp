// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_making.hpp"

#include "park/boundary_fence.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace rct3::park
{

    namespace
    {
        constexpr float kMinDrop = 2.0f;    ///< Least height, in world units, the water must fall.
        constexpr int kMaxReach = 6;        ///< Most tiles out from the edge the lower lake may lie.
        constexpr int kMaxEdgeTiles = 5;    ///< Most tiles along the edge one waterfall pours over.
        constexpr float kPickMargin = 1.0f; ///< World units round a waterfall it can be picked.

        save::Tile step(save::Tile t, save::Tile d, int n = 1)
        {
            return { t.col + d.col * n, t.row + d.row * n };
        }

        save::WorldPoint3 world(glm::vec3 p)
        {
            return { p.x, p.y, p.z };
        }
    } // namespace

    std::expected<save::Waterfall, std::string_view> make_waterfall(const save::TerrainGrid& grid, std::span<const save::WaterBody> bodies, std::size_t first, std::size_t second, save::Tile near)
    {
        if (first == second || first >= bodies.size() || second >= bodies.size())
            return std::unexpected(kWaterfallSameWater);
        const save::WaterBody& high = bodies[first].level >= bodies[second].level ? bodies[first] : bodies[second];
        const save::WaterBody& low = &high == &bodies[first] ? bodies[second] : bodies[first];
        if (high.level - low.level < kMinDrop)
            return std::unexpected(kWaterfallTooLittleDrop);

        // Which tiles each lake holds, looked up for every tile tried.
        std::vector<std::uint8_t> holding(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), 0);
        for (const auto& [body, bit] : { std::pair { &high, 1 }, std::pair { &low, 2 } })
            for (const save::WaterTri& tri : body->tris)
                if (grid.contains(tri.col, tri.row))
                    holding[static_cast<std::size_t>(tri.row) * static_cast<std::size_t>(grid.width) + tri.col] |= bit;
        const auto in = [&](save::Tile t, int bit) { return grid.contains(t.col, t.row) && (holding[static_cast<std::size_t>(t.row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(t.col)] & bit) != 0; };

        // The edge tile of the higher lake with the shortest way straight out to the lower one.
        constexpr std::array<save::Tile, 4> kWays { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } } };
        struct Way
        {
            save::Tile from;
            save::Tile out;
            int reach = 0;
            int off = 0; ///< Squared tiles from `near`
        };
        std::optional<Way> best;
        for (const save::WaterTri& tri : high.tris)
        {
            const save::Tile from { tri.col, tri.row };
            for (const save::Tile out : kWays)
                for (int k = 1; k <= kMaxReach; ++k)
                {
                    const save::Tile t = step(from, out, k);
                    if (!grid.contains(t.col, t.row) || in(t, 1))
                        break;
                    if (!in(t, 2))
                        continue;
                    const int off = (from.col - near.col) * (from.col - near.col) + (from.row - near.row) * (from.row - near.row);
                    if (!best || k < best->reach || (k == best->reach && off < best->off))
                        best = Way { from, out, k, off };
                    break;
                }
        }
        if (!best)
            return std::unexpected(kWaterfallTooFarApart);

        // The edge runs on to each side while the lake's border does and the lower lake lies as far out.
        const save::Tile side { -best->out.row, best->out.col };
        const auto edge = [&](save::Tile t) { return in(t, 1) && !in(step(t, best->out), 1) && in(step(t, best->out, best->reach), 2); };
        int from = 0;
        int to = 0;
        while (to - from + 1 < kMaxEdgeTiles && edge(step(best->from, side, to + 1)))
            ++to;
        while (to - from + 1 < kMaxEdgeTiles && edge(step(best->from, side, from - 1)))
            --from;
        for (int s = from; s <= to; ++s)
            for (int k = 0; k <= best->reach; ++k)
            {
                const save::Tile t = step(step(best->from, side, s), best->out, k);
                if (!owned_outright(grid.at(t.col, t.row)))
                    return std::unexpected(kWaterfallNotOwned);
            }

        // In world units: the tiles' sizes along the map's two ways.
        const glm::vec3 flow { static_cast<float>(best->out.col), 0.0f, static_cast<float>(best->out.row) };
        const glm::vec3 across { static_cast<float>(side.col), 0.0f, static_cast<float>(side.row) };
        const glm::vec3 size { grid.tile_size_x, 0.0f, grid.tile_size_z };
        const glm::vec3 centre = glm::vec3 { grid.world_x(best->from.col) + grid.tile_size_x / 2, high.level, grid.world_z(best->from.row) + grid.tile_size_z / 2 } + across * size * (static_cast<float>(from + to) / 2);
        const glm::vec3 lip = centre + flow * size / 2.0f;
        const glm::vec3 half = across * size * (static_cast<float>(to - from + 1) / 2);
        glm::vec3 bottom = lip + flow * size * (static_cast<float>(best->reach) - 0.5f);
        bottom.y = low.level;
        return save::Waterfall { .lip_left = world(lip - half), .lip_right = world(lip + half), .top = world(centre), .bottom = world(bottom), .flow = world(flow) };
    }

    std::optional<std::size_t> waterfall_at(std::span<const save::Waterfall> falls, float x, float z)
    {
        std::optional<std::size_t> found;
        float nearest = 0;
        for (std::size_t i = 0; i < falls.size(); ++i)
        {
            const save::Waterfall& f = falls[i];
            const glm::vec2 left { f.lip_left.x, f.lip_left.z };
            const glm::vec2 right { f.lip_right.x, f.lip_right.z };
            const glm::vec2 lip = (left + right) / 2.0f;
            const glm::vec2 out = glm::vec2 { f.bottom.x, f.bottom.z } - lip;
            const float reach = glm::length(out);
            const float half = glm::length(right - left) / 2;
            if (reach <= 0 || half <= 0)
                continue;
            // The ground under the falling water: a box from the edge to where it lands, as wide as the edge.
            const glm::vec2 d = glm::vec2 { x, z } - lip;
            const float along = glm::dot(d, out / reach);
            const float aside = std::abs(glm::dot(d, (right - left) / (2 * half)));
            if (along < -kPickMargin || along > reach + kPickMargin || aside > half + kPickMargin)
                continue;
            const float off = std::abs(along - reach / 2) + aside;
            if (!found || off < nearest)
            {
                found = i;
                nearest = off;
            }
        }
        return found;
    }

} // namespace rct3::park
