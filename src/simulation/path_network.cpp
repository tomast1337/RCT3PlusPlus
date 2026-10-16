// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/path_network.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>

namespace rct3::simulation
{

    namespace
    {
        constexpr std::array<std::array<int, 2>, 4> kSides { { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } } };
    }

    std::uint64_t PathNetwork::key(int col, int row)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(col)) << 32) | static_cast<std::uint32_t>(row);
    }

    std::optional<NodeId> PathNetwork::find(PathTileSpot tile) const
    {
        const auto [first, last] = by_tile_.equal_range(key(tile.col, tile.row));
        for (auto it = first; it != last; ++it)
            if (nodes_[it->second].tile.level == tile.level)
                return it->second;
        return std::nullopt;
    }

    NodeId PathNetwork::add(PathTileSpot tile, bool queue, std::optional<float> height, std::uint64_t line)
    {
        if (const auto found = find(tile))
            return *found;
        const auto id = static_cast<NodeId>(nodes_.size());
        nodes_.push_back({ .tile = tile, .queue = queue, .height = height.value_or(static_cast<float>(tile.level) * level_height), .line = line });
        by_tile_.emplace(key(tile.col, tile.row), id);
        for (std::size_t side = 0; side < kSides.size(); ++side)
        {
            const auto [first, last] = by_tile_.equal_range(key(tile.col + kSides[side][0], tile.row + kSides[side][1]));
            // The closest level within max_step, lowest first when two are as close.
            NodeId best = kNoNode;
            for (auto it = first; it != last; ++it)
            {
                const int gap = std::abs(nodes_[it->second].tile.level - tile.level);
                if (gap <= max_step && (best == kNoNode || gap < std::abs(nodes_[best].tile.level - tile.level)))
                    best = it->second;
            }
            if (best == kNoNode || nodes_[best].next[(side + 2) % 4] != kNoNode)
                continue;
            nodes_[id].next[side] = best;
            nodes_[best].next[(side + 2) % 4] = id;
        }
        return id;
    }

    std::optional<NodeId> PathNetwork::nearest(glm::vec3 at) const
    {
        const int col = static_cast<int>(std::floor((at.x - origin.x) / tile_size));
        const int row = static_cast<int>(std::floor((at.z - origin.y) / tile_size));
        const auto [first, last] = by_tile_.equal_range(key(col, row));
        std::optional<NodeId> best;
        float gap = std::numeric_limits<float>::max();
        for (auto it = first; it != last; ++it)
        {
            const float g = std::abs(nodes_[it->second].height - at.y);
            if (g < gap)
            {
                gap = g;
                best = it->second;
            }
        }
        return best;
    }

    glm::vec3 PathNetwork::centre(NodeId node) const
    {
        const PathTileSpot& t = nodes_[node].tile;
        return { origin.x + (static_cast<float>(t.col) + 0.5f) * tile_size, nodes_[node].height, origin.y + (static_cast<float>(t.row) + 0.5f) * tile_size };
    }

    std::optional<std::vector<NodeId>> PathNetwork::route(NodeId from, NodeId to) const
    {
        if (from >= nodes_.size() || to >= nodes_.size())
            return std::nullopt;
        if (from == to)
            return std::vector<NodeId> { };
        // Every step costs the same, so a breadth-first search finds a shortest walk. Sides are tried in a fixed
        // order, so the same network always gives the same walk.
        std::vector<NodeId> came(nodes_.size(), kNoNode);
        std::deque<NodeId> open { from };
        came[from] = from;
        while (!open.empty())
        {
            const NodeId at = open.front();
            open.pop_front();
            for (const NodeId n : nodes_[at].next)
            {
                if (n == kNoNode || came[n] != kNoNode || (nodes_[n].queue && n != to))
                    continue;
                came[n] = at;
                if (n == to)
                {
                    std::vector<NodeId> walk;
                    for (NodeId s = to; s != from; s = came[s])
                        walk.push_back(s);
                    std::ranges::reverse(walk);
                    return walk;
                }
                open.push_back(n);
            }
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
