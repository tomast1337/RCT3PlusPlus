// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_making.hpp"

#include "park/boundary_fence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace rct3::park
{

    namespace
    {
        constexpr float kMinDrop = 8.0f;    ///< Least height, in world units, the water must fall.
        constexpr int kMaxReach = 6;        ///< Most tiles out from the edge the lower lake may lie.
        constexpr int kNarrowestEdge = 2;   ///< Fewest tiles of edge a waterfall needs.
        constexpr float kPickMargin = 1.0f; ///< World units round a waterfall it can be picked.

        save::model::Tile step(save::model::Tile t, save::model::Tile d, int n = 1)
        {
            return { t.col + d.col * n, t.row + d.row * n };
        }

        save::model::WorldPoint3 world(glm::vec3 p)
        {
            return { p.x, p.y, p.z };
        }
    } // namespace

    std::expected<save::model::Waterfall, std::string_view> make_waterfall(const save::model::TerrainGrid& grid, std::span<const save::model::WaterBody> bodies, std::size_t first, std::size_t second, save::model::Tile at_first, save::model::Tile at_second)
    {
        if (first == second || first >= bodies.size() || second >= bodies.size())
            return std::unexpected(kWaterfallSameWater);
        const save::model::WaterBody& high = bodies[first].level >= bodies[second].level ? bodies[first] : bodies[second];
        const save::model::WaterBody& low = &high == &bodies[first] ? bodies[second] : bodies[first];
        if (high.level - low.level < kMinDrop)
            return std::unexpected(kWaterfallTooLittleDrop);

        // Which tiles each lake holds, looked up for every tile tried.
        std::vector<std::uint8_t> holding(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), 0);
        for (const auto& [body, bit] : { std::pair { &high, 1 }, std::pair { &low, 2 } })
            for (const save::model::WaterTri& tri : body->tris)
                if (grid.contains(tri.col, tri.row))
                    holding[static_cast<std::size_t>(tri.row) * static_cast<std::size_t>(grid.width) + tri.col] |= bit;
        const auto in = [&](save::model::Tile t, int bit) { return grid.contains(t.col, t.row) && (holding[static_cast<std::size_t>(t.row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(t.col)] & bit) != 0; };

        // Where the player picked each lake, in tiles.
        const bool first_high = &high == &bodies[first];
        const glm::vec2 picked_high = glm::vec2 { first_high ? at_first.col : at_second.col, first_high ? at_first.row : at_second.row } + 0.5f;
        const glm::vec2 picked_low = glm::vec2 { first_high ? at_second.col : at_first.col, first_high ? at_second.row : at_first.row } + 0.5f;

        // The higher lake's border as straight runs of tile sides, each facing one way out. A tile holding two
        // triangles gives its run twice, which changes nothing below.
        constexpr std::array<save::model::Tile, 4> kWays { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } } };
        struct Run
        {
            save::model::Tile from;
            save::model::Tile out;
            int length = 0;
            glm::vec2 start { }; ///< Its two ends on the border, in tiles
            glm::vec2 end { };
            bool start_juts = false; ///< The border turns back round the lake there, rather than out into the bank
            bool end_juts = false;
        };
        std::vector<Run> runs;
        for (const save::model::WaterTri& tri : high.tris)
            for (const save::model::Tile out : kWays)
            {
                const save::model::Tile from { tri.col, tri.row };
                const save::model::Tile side { -out.row, out.col };
                const auto border = [&](save::model::Tile t) { return in(t, 1) && !in(step(t, out), 1); };
                if (!border(from) || border(step(from, side, -1)))
                    continue; // Not a run's first tile
                int length = 1;
                while (border(step(from, side, length)))
                    ++length;
                const glm::vec2 start = glm::vec2 { from.col, from.row } + 0.5f + (glm::vec2 { out.col, out.row } - glm::vec2 { side.col, side.row }) * 0.5f;
                runs.push_back({ .from = from, .out = out, .length = length, .start = start, .end = start + glm::vec2 { side.col, side.row } * static_cast<float>(length), .start_juts = !in(step(from, side, -1), 1), .end_juts = !in(step(from, side, length), 1) });
            }

        // The edge is a run the lower lake's pick lies in front of: between the lines square to the run at its two
        // ends, or, when it lies in front of none, between the lines halving the border's corners there.
        std::vector<std::size_t> facing;
        for (const bool square : { true, false })
        {
            for (std::size_t i = 0; i < runs.size(); ++i)
            {
                const Run& r = runs[i];
                const glm::vec2 out { r.out.col, r.out.row };
                const glm::vec2 along { -out.y, out.x };
                const glm::vec2 past_start = square ? along : (r.start_juts ? along + out : along - out);
                const glm::vec2 past_end = square ? along : (r.end_juts ? along - out : along + out);
                if (glm::dot(out, picked_low - r.start) > 0 && glm::dot(past_start, picked_low - r.start) > 0 && glm::dot(past_end, picked_low - r.end) < 0)
                    facing.push_back(i);
            }
            if (!facing.empty())
                break;
        }
        if (facing.empty())
            return std::unexpected(kWaterfallLandUnsuitable);
        // Of several, the one whose middle is nearest the lower pick and which faces most the way from the higher
        // pick to the lower one: the first counts from 0 to 1, the second from -1 to 1.
        const auto away = [&](std::size_t i) { return glm::length((runs[i].start + runs[i].end) / 2.0f - picked_low); };
        const auto [nearest, farthest] = std::ranges::minmax(facing, { }, away);
        const glm::vec2 way = picked_low == picked_high ? glm::vec2 { } : glm::normalize(picked_low - picked_high);
        const auto fit = [&](std::size_t i) { return (away(farthest) > away(nearest) ? 1 - (away(i) - away(nearest)) / (away(farthest) - away(nearest)) : 1.0f) + glm::dot(glm::vec2 { runs[i].out.col, runs[i].out.row }, way); };
        const Run& run = runs[*std::ranges::max_element(facing, { }, fit)];
        if (run.length < kNarrowestEdge)
            return std::unexpected(kWaterfallEdgeTooNarrow);

        // The water pours between the edge's two end tiles, which slope up to the banks; an edge of two tiles
        // pours over its middle. It lands where the lower lake lies nearest straight out from there.
        const save::model::Tile side { -run.out.row, run.out.col };
        const int from = 0;
        const int to = run.length - 1;
        const int inset = run.length > 2 ? 1 : 0;
        struct Way
        {
            save::model::Tile from;
            save::model::Tile out;
            int reach = 0;
        };
        std::optional<Way> best;
        for (int s = from + inset; s <= to - inset; ++s)
            for (int k = 1; k <= kMaxReach; ++k)
            {
                const save::model::Tile t = step(step(run.from, side, s), run.out, k);
                if (!grid.contains(t.col, t.row) || in(t, 1))
                    break;
                if (!in(t, 2))
                    continue;
                if (!best || k < best->reach)
                    best = Way { run.from, run.out, k };
                break;
            }
        if (!best)
            return std::unexpected(kWaterfallTooFarApart);
        for (int s = from; s <= to; ++s)
            for (int k = 0; k <= best->reach; ++k)
            {
                const save::model::Tile t = step(step(best->from, side, s), best->out, k);
                if (!owned_outright(grid.at(t.col, t.row)))
                    return std::unexpected(kWaterfallNotOwned);
            }

        // The edge tiles, the spillway beyond them and the ground on the way down to the lower lake; none when the
        // lower lake lies against the edge.
        std::vector<save::model::CarvedTile> carved;
        const bool spillway = best->reach > 1;
        for (int s = from; spillway && s <= to; ++s)
            for (int k = 0; k < best->reach; ++k)
            {
                const save::model::Tile t = step(step(best->from, side, s), best->out, k);
                carved.push_back({ .tile = t, .corners = grid.at(t.col, t.row).corners });
            }

        // In world units: the tiles' sizes along the map's two ways.
        const glm::vec3 flow { static_cast<float>(best->out.col), 0.0f, static_cast<float>(best->out.row) };
        const glm::vec3 across { static_cast<float>(side.col), 0.0f, static_cast<float>(side.row) };
        const glm::vec3 size { grid.tile_size_x, 0.0f, grid.tile_size_z };
        const glm::vec3 centre = glm::vec3 { grid.world_x(best->from.col) + grid.tile_size_x / 2, high.level, grid.world_z(best->from.row) + grid.tile_size_z / 2 } + across * size * (static_cast<float>(from + to) / 2);
        const glm::vec3 lip = centre + flow * size * (spillway ? 1.5f : 0.5f);
        const glm::vec3 half = across * size * (run.length > 2 ? static_cast<float>(run.length - 2) / 2 : 0.5f);
        glm::vec3 bottom = centre + flow * size * static_cast<float>(best->reach);
        bottom.y = low.level;
        return save::model::Waterfall { .lip_left = world(lip - half), .lip_right = world(lip + half), .top = world(centre), .bottom = world(bottom), .flow = world(flow), .carved = std::move(carved) };
    }

    void cut_spillway(save::model::TerrainGrid& grid, const save::model::Waterfall& fall)
    {
        const glm::vec2 flow { fall.flow.x, fall.flow.z };
        const glm::vec2 lip = (glm::vec2 { fall.lip_left.x, fall.lip_left.z } + glm::vec2 { fall.lip_right.x, fall.lip_right.z }) / 2.0f;
        const float length = std::abs(flow.x) * grid.tile_size_x + std::abs(flow.y) * grid.tile_size_z; // Of a tile, along the flow
        const float level = fall.top.y;
        const glm::vec2 across { -flow.y, flow.x };
        // A tile's size along the edge, how many tiles from its middle the water pours, and how near a line counts as on it.
        const float width = std::abs(across.x) * grid.tile_size_x + std::abs(across.y) * grid.tile_size_z;
        const float pouring = glm::length(glm::vec2 { fall.lip_right.x, fall.lip_right.z } - glm::vec2 { fall.lip_left.x, fall.lip_left.z }) / width / 2;
        constexpr float kOn = 0.01f;
        const auto aside = [&](float x, float z) { return std::abs(glm::dot(glm::vec2 { x, z } - lip, across)) / width; };
        // The edge's two ends, in tiles from its middle.
        float ends = 0;
        for (const save::model::CarvedTile& c : fall.carved)
            for (int k = 0; k < 4; ++k)
                ends = std::max(ends, aside(grid.world_x(c.tile.col) + static_cast<float>(k % 2) * grid.tile_size_x, grid.world_z(c.tile.row) + static_cast<float>(k / 2) * grid.tile_size_z));
        for (const save::model::CarvedTile& c : fall.carved)
        {
            if (!grid.contains(c.tile.col, c.tile.row))
                continue;
            // Measured in tiles out from the lip: the edge tiles' middles at -1.5, the spillway's at -0.5.
            const auto out = [&](float x, float z) { return glm::dot(glm::vec2 { x, z } - lip, flow) / length; };
            const float middle = out(grid.world_x(c.tile.col) + grid.tile_size_x / 2, grid.world_z(c.tile.row) + grid.tile_size_z / 2);
            // The ground on the way down sinks only under the falling water.
            if (middle > 0 && aside(grid.world_x(c.tile.col) + grid.tile_size_x / 2, grid.world_z(c.tile.row) + grid.tile_size_z / 2) > pouring)
                continue;
            auto& corners = grid.at(c.tile.col, c.tile.row).corners;
            for (std::size_t k = 0; k < 4; ++k)
            {
                const float x = grid.world_x(c.tile.col) + static_cast<float>(k % 2) * grid.tile_size_x;
                const float z = grid.world_z(c.tile.row) + static_cast<float>(k / 2) * grid.tile_size_z;
                const float at = out(x, z);
                // The edge's two ends stay with the banks, so its end tiles slope up to them and no wall opens.
                if (aside(x, z) > ends - kOn)
                    continue;
                if (middle > 0)
                    corners[k] = std::min(corners[k], level - kSpillwayDepth); // On the way down
                else if (std::abs(at + 1) < kOn)
                    corners[k] = level - kSpillwayDepth; // The old edge
                else if (std::abs(at) < kOn)
                    corners[k] = level + kSpillwayRim; // The spillway's far side
            }
        }
    }

    void restore_spillway(save::model::TerrainGrid& grid, const save::model::Waterfall& fall)
    {
        for (const save::model::CarvedTile& c : fall.carved)
        {
            if (grid.contains(c.tile.col, c.tile.row))
                grid.at(c.tile.col, c.tile.row).corners = c.corners;
        }
    }

    std::optional<std::size_t> waterfall_at(std::span<const save::model::Waterfall> falls, float x, float z)
    {
        std::optional<std::size_t> found;
        float nearest = 0;
        for (std::size_t i = 0; i < falls.size(); ++i)
        {
            const save::model::Waterfall& f = falls[i];
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
