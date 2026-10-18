// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_route.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <unordered_map>

namespace rct3::park
{

    namespace
    {
        struct Node
        {
            PathSpot spot;           ///< Where the last piece is
            std::int32_t height = 0; ///< Where it ends
            std::uint8_t heading = 0;
            bool on_slope = false;
            bool started = true; ///< False for the first node of a trail that has laid nothing yet
            std::int64_t cost = 0;
            std::size_t parent = 0;
            RouteStep step;
        };

        std::uint64_t key_of(const Node& n)
        {
            const auto col = static_cast<std::uint64_t>(static_cast<std::uint16_t>(n.spot.col));
            const auto row = static_cast<std::uint64_t>(static_cast<std::uint16_t>(n.spot.row));
            const auto height = static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.height));
            return col | row << 16 | height << 32 | std::uint64_t { n.heading } << 60 | std::uint64_t { n.on_slope } << 62;
        }

        /// The builder as it stands after `node`.
        FootpathBuilder builder_at(const FootpathBuilder& from, const Node& node)
        {
            FootpathBuilder b;
            b.cut_into_ground = from.cut_into_ground;
            b.queue = from.queue;
            b.on_slope = node.on_slope;
            b.heading = node.heading;
            b.start = node.started ? node.spot : *from.start;
            if (node.started)
                b.trail.push_back({ node.spot, 0, node.height });
            return b;
        }

        std::int64_t price_of(const NextPiece& piece, const FootpathBuilder& b, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::int32_t ground_cost)
        {
            if (same_piece_at(paths, piece, b))
                return 0;
            return piece_price(piece, grid, ground_cost);
        }

        /// kRouteCutCost for each started unit the ground of `piece`'s tile is cut down by.
        std::int64_t cut_cost(const NextPiece& piece, const save::TerrainGrid& grid)
        {
            if (!piece.cut)
                return 0;
            const float depth = grid.at(piece.spot.col, piece.spot.row).max_height() - static_cast<float>(std::min(piece.start, piece.end));
            return kRouteCutCost * std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(depth)));
        }
    } // namespace

    std::optional<std::vector<RouteStep>> find_route(const FootpathBuilder& from, PathSpot goal, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::int32_t ground_cost, std::optional<std::int32_t> goal_height, const SolidTiles* solids)
    {
        if (!from.start || !grid.contains(goal.col, goal.row) || grid.at(goal.col, goal.row).ownership() != save::Ownership::Owned)
            return std::nullopt;
        std::vector<Node> nodes;
        Node first;
        first.heading = from.heading;
        first.on_slope = from.on_slope;
        first.started = !from.trail.empty();
        if (first.started)
        {
            first.spot = from.trail.back().spot;
            first.height = from.trail.back().height;
            if (first.spot == goal && (!goal_height || first.height == *goal_height))
                return std::vector<RouteStep> { };
        }
        else
            first.spot = *from.start;
        nodes.push_back(first);

        // Each piece is checked only against the paths on its own tile.
        std::unordered_map<std::uint32_t, std::vector<save::PathTile>> by_tile;
        for (const save::PathTile& p : paths)
            by_tile[static_cast<std::uint32_t>(p.row) << 16 | p.col].push_back(p);
        const std::vector<save::PathTile> none;
        const auto paths_at = [&](PathSpot spot) -> std::span<const save::PathTile> {
            const auto found = by_tile.find(static_cast<std::uint32_t>(spot.row) << 16 | static_cast<std::uint16_t>(spot.col));
            return found == by_tile.end() ? std::span<const save::PathTile>(none) : std::span<const save::PathTile>(found->second);
        };

        // shortcut: a route may cross its own tiles at heights the search kept apart, which the builder then
        // refuses part way; check the tiles taken along a route once routes get long or winding.
        const std::int64_t least_step = std::max<std::int64_t>(ground_cost, 0) + kRouteStepCost;
        const auto estimate = [&](PathSpot at) { return (std::abs(at.col - goal.col) + std::abs(at.row - goal.row)) * least_step; };
        using Open = std::pair<std::int64_t, std::size_t>;
        std::priority_queue<Open, std::vector<Open>, std::greater<>> open;
        std::unordered_map<std::uint64_t, std::int64_t> best;
        open.push({ estimate(first.spot), 0 });
        std::size_t looked = 0;
        while (!open.empty() && looked < kRouteSearchLimit)
        {
            const std::size_t at = open.top().second;
            open.pop();
            const Node node = nodes[at];
            if (node.started && node.spot == goal && at != 0)
            {
                std::vector<RouteStep> route;
                for (std::size_t i = at; i != 0; i = nodes[i].parent)
                    route.push_back(nodes[i].step);
                std::ranges::reverse(route);
                return route;
            }
            if (const auto known = best.find(key_of(node)); at != 0 && known != best.end() && known->second < node.cost)
                continue;
            ++looked;
            for (std::uint8_t heading = 0; heading < 4; ++heading)
            {
                const bool turn = heading != node.heading;
                if (node.started && (turn && node.on_slope || heading == (node.heading + 2) % 4))
                    continue;
                for (std::size_t s = 0; s < kPathSlopeCount; ++s)
                {
                    FootpathBuilder b = builder_at(from, node);
                    b.heading = heading;
                    b.slope = static_cast<PathSlope>(s);
                    const auto piece = next_piece(b, grid);
                    if (!piece)
                        continue;
                    const auto here = paths_at(piece->spot);
                    if (lay_problem(b, grid, here, solids) != LayProblem::None)
                        continue;
                    // The goal is reached only on its ground or onto a path already there, the one asked for if any.
                    if (piece->spot == goal)
                    {
                        const auto same = same_piece_at(here, *piece, b);
                        const bool onto = same && here[*same].elevation && here[*same].elevation->quantised_height == goal_height;
                        if (goal_height ? !onto : !piece->ground && !same)
                            continue;
                    }
                    Node next;
                    next.spot = piece->spot;
                    next.height = piece->end;
                    next.heading = heading;
                    next.on_slope = b.slope != PathSlope::Flat;
                    next.parent = at;
                    next.step = { heading, b.slope, *piece };
                    next.cost = node.cost + price_of(*piece, b, grid, here, ground_cost) + kRouteStepCost + (node.started && turn ? kRouteTurnCost : 0) + cut_cost(*piece, grid) + (b.slope != PathSlope::Flat ? kRouteSlopeCost : 0);
                    const std::uint64_t key = key_of(next);
                    if (const auto known = best.find(key); known != best.end() && known->second <= next.cost)
                        continue;
                    best[key] = next.cost;
                    nodes.push_back(next);
                    open.push({ next.cost + estimate(next.spot), nodes.size() - 1 });
                }
            }
        }
        return std::nullopt;
    }

    std::int64_t route_price(std::span<const RouteStep> route, const FootpathBuilder& from, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::int32_t ground_cost)
    {
        FootpathBuilder b = from;
        std::int64_t total = 0;
        for (const RouteStep& s : route)
        {
            b.heading = s.heading;
            b.slope = s.slope;
            total += price_of(s.piece, b, grid, paths, ground_cost);
            b.trail.push_back({ s.piece.spot, 0, s.piece.end });
            b.on_slope = s.slope != PathSlope::Flat;
        }
        return total;
    }

} // namespace rct3::park
