// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/path_network.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>

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
        join_sides(id);
        // The squares the new tile closes, seen from each tile round it.
        for (int dc = -1; dc <= 1; ++dc)
            for (int dr = -1; dr <= 1; ++dr)
            {
                const auto [first, last] = by_tile_.equal_range(key(tile.col + dc, tile.row + dr));
                for (auto it = first; it != last; ++it)
                    join_corners(it->second);
            }
        return id;
    }

    void PathNetwork::remove(NodeId id)
    {
        if (id >= nodes_.size() || nodes_[id].removed)
            return;
        PathNode& n = nodes_[id];
        const auto [first, last] = by_tile_.equal_range(key(n.tile.col, n.tile.row));
        for (auto it = first; it != last; ++it)
            if (it->second == id)
            {
                by_tile_.erase(it);
                break;
            }
        for (std::size_t side = 0; side < 4; ++side)
            if (const NodeId other = std::exchange(n.next[side], kNoNode); other != kNoNode)
                nodes_[other].next[(side + 2) % 4] = kNoNode;
        n.corner.fill(kNoNode);
        n.removed = true;
        for (int dc = -1; dc <= 1; ++dc)
            for (int dr = -1; dr <= 1; ++dr)
            {
                const auto [near_first, near_last] = by_tile_.equal_range(key(n.tile.col + dc, n.tile.row + dr));
                for (auto it = near_first; it != near_last; ++it)
                    join_corners(it->second);
            }
    }

    void PathNetwork::join_sides(NodeId id)
    {
        const PathTileSpot tile = nodes_[id].tile;
        const auto [walls_first, walls_last] = walls_.equal_range(key(tile.col, tile.row));
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
            const float low = std::min(nodes_[id].height, nodes_[best].height);
            const bool walled = !nodes_[id].queue && !nodes_[best].queue && std::any_of(walls_first, walls_last, [&](const auto& w) { return w.second.side == side && low >= w.second.low && low < w.second.high; });
            if (walled)
                continue;
            nodes_[id].next[side] = best;
            nodes_[best].next[(side + 2) % 4] = id;
        }
    }

    void PathNetwork::rejoin(int col, int row)
    {
        const auto [first, last] = by_tile_.equal_range(key(col, row));
        for (auto it = first; it != last; ++it)
            for (std::size_t side = 0; side < 4; ++side)
                if (const NodeId other = std::exchange(nodes_[it->second].next[side], kNoNode); other != kNoNode)
                    nodes_[other].next[(side + 2) % 4] = kNoNode;
        for (auto it = first; it != last; ++it)
            join_sides(it->second);
        for (int dc = -1; dc <= 1; ++dc)
            for (int dr = -1; dr <= 1; ++dr)
            {
                const auto [near_first, near_last] = by_tile_.equal_range(key(col + dc, row + dr));
                for (auto it = near_first; it != near_last; ++it)
                    join_corners(it->second);
            }
    }

    void PathNetwork::add_wall(int col, int row, std::size_t side, float low, float high)
    {
        const int across_col = col + kSides[side % 4][0];
        const int across_row = row + kSides[side % 4][1];
        walls_.emplace(key(col, row), Wall { side % 4, low, high });
        walls_.emplace(key(across_col, across_row), Wall { (side + 2) % 4, low, high });
        rejoin(col, row);
        rejoin(across_col, across_row);
    }

    void PathNetwork::remove_wall(int col, int row, std::size_t side, float low, float high)
    {
        const int across_col = col + kSides[side % 4][0];
        const int across_row = row + kSides[side % 4][1];
        for (const auto& [at, wall] : { std::pair { key(col, row), Wall { side % 4, low, high } }, std::pair { key(across_col, across_row), Wall { (side + 2) % 4, low, high } } })
        {
            const auto [first, last] = walls_.equal_range(at);
            if (const auto found = std::find_if(first, last, [&](const auto& w) { return w.second == wall; }); found != last)
                walls_.erase(found);
        }
        rejoin(col, row);
        rejoin(across_col, across_row);
    }

    void PathNetwork::join_corners(NodeId id)
    {
        // Corner k lies between side k and the side after it.
        PathNode& a = nodes_[id];
        for (std::size_t k = 0; k < 4; ++k)
        {
            const std::size_t across = (k + 1) % 4;
            const NodeId b = a.next[k];
            const NodeId c = a.next[across];
            const NodeId d = b != kNoNode ? nodes_[b].next[across] : kNoNode;
            const bool square = !a.queue && c != kNoNode && d != kNoNode && nodes_[c].next[k] == d && std::ranges::all_of(std::array { b, c, d }, [&](NodeId n) { return !nodes_[n].queue && nodes_[n].tile.level == a.tile.level; });
            a.corner[k] = square ? d : kNoNode;
            corners_ = corners_ || square;
        }
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

    float PathNetwork::height_between(NodeId from, NodeId to, float t) const
    {
        const PathNode& a = nodes_[from];
        const PathNode& b = nodes_[to];
        if (a.sloped == b.sloped)
            return std::lerp(a.height, b.height, t);
        // A ramp meets a flat tile at the flat tile's height, on the edge halfway between their middles.
        const float edge = a.sloped ? b.height : a.height;
        return t < 0.5f ? std::lerp(a.height, edge, t * 2) : std::lerp(edge, b.height, t * 2 - 1);
    }

    glm::vec3 PathNetwork::centre(NodeId node) const
    {
        const PathTileSpot& t = nodes_[node].tile;
        return { origin.x + (static_cast<float>(t.col) + 0.5f) * tile_size, nodes_[node].height, origin.y + (static_cast<float>(t.row) + 0.5f) * tile_size };
    }

    bool PathNetwork::may_step(NodeId from, NodeId to, Walker who) const
    {
        const PathNode& a = nodes_[from];
        const PathNode& b = nodes_[to];
        if (who == Walker::Staff)
            return !b.staff_kept_off || a.staff_kept_off;
        // The sides of `to` the step comes in across: the one facing back, or the two round the corner it cuts.
        std::uint8_t across = 0;
        for (std::size_t k = 0; k < 4; ++k)
        {
            if (a.next[k] == to)
                across |= static_cast<std::uint8_t>(1U << ((k + 2) % 4));
            if (a.corner[k] == to)
                across |= static_cast<std::uint8_t>((1U << ((k + 2) % 4)) | (1U << ((k + 3) % 4)));
        }
        return (b.no_entry & across) == 0;
    }

    void PathNetwork::link(const PathLink& link)
    {
        links_.emplace(link.from, link);
    }

    std::optional<const PathLink&> PathNetwork::link_between(NodeId from, NodeId to) const
    {
        const auto [first, last] = links_.equal_range(from);
        for (auto it = first; it != last; ++it)
            if (it->second.to == to)
                return it->second;
        return std::nullopt;
    }

    std::optional<std::vector<NodeId>> PathNetwork::route(NodeId from, NodeId to, Walker who, std::optional<std::int64_t> purse) const
    {
        auto found = route_nearest(from, std::span(&to, 1), who, purse);
        return found ? std::optional(std::move(found->second)) : std::nullopt;
    }

    std::optional<std::pair<std::size_t, std::vector<NodeId>>> PathNetwork::route_nearest(NodeId from, std::span<const NodeId> targets, Walker who, std::optional<std::int64_t> purse) const
    {
        const bool linked = purse && !links_.empty();
        if (from >= nodes_.size())
            return std::nullopt;
        constexpr std::size_t kNotTarget = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> target(nodes_.size(), kNotTarget); // The first of `targets` at each node
        for (std::size_t i = targets.size(); i-- > 0;)
            if (targets[i] < nodes_.size())
                target[targets[i]] = i;
        if (target[from] != kNotTarget)
            return std::pair { target[from], std::vector<NodeId> { } };
        std::vector<NodeId> came(nodes_.size(), kNoNode);
        const auto walk_to = [&](NodeId to) {
            std::vector<NodeId> walk;
            for (NodeId s = to; s != from; s = came[s])
                walk.push_back(s);
            std::ranges::reverse(walk);
            return std::pair { target[to], std::move(walk) };
        };
        came[from] = from;
        if (corners_ || linked)
        {
            // Steps across squares and links are longer, so the shortest walk is found by length; ties go to the
            // lower node, so the same network always gives the same walk.
            constexpr float kAcross = std::numbers::sqrt2_v<float>;
            std::vector<float> length(nodes_.size(), std::numeric_limits<float>::max());
            std::vector<std::int64_t> spent(linked ? nodes_.size() : 0); // Tolls paid on the way to each node
            std::priority_queue<std::pair<float, NodeId>, std::vector<std::pair<float, NodeId>>, std::greater<>> open;
            length[from] = 0;
            open.emplace(0.0f, from);
            while (!open.empty())
            {
                const auto [so_far, at] = open.top();
                open.pop();
                if (target[at] != kNotTarget)
                    return walk_to(at);
                if (so_far > length[at])
                    continue;
                const auto step = [&](NodeId n, float cost) {
                    if (n == kNoNode || (nodes_[n].queue && target[n] == kNotTarget && !(linked && links_.contains(n))) || so_far + cost >= length[n] || !may_step(at, n, who))
                        return;
                    length[n] = so_far + cost;
                    came[n] = at;
                    open.emplace(length[n], n);
                    if (linked)
                        spent[n] = spent[at];
                };
                if (linked)
                {
                    const auto [first, last] = links_.equal_range(at);
                    for (auto it = first; it != last; ++it)
                    {
                        const PathLink& l = it->second;
                        const std::int64_t paid = spent[at] + l.toll;
                        if (paid > *purse || l.to >= nodes_.size() || nodes_[l.to].removed || so_far + l.length >= length[l.to] || (nodes_[l.to].queue && target[l.to] == kNotTarget))
                            continue;
                        length[l.to] = so_far + l.length;
                        came[l.to] = at;
                        spent[l.to] = paid;
                        open.emplace(length[l.to], l.to);
                    }
                    // From a queue come to only for its link, nowhere else.
                    if (nodes_[at].queue && at != from)
                        continue;
                }
                for (const NodeId n : nodes_[at].next)
                    step(n, 1);
                for (const NodeId n : nodes_[at].corner)
                    step(n, kAcross);
            }
            return std::nullopt;
        }
        // Every step costs the same, so a breadth-first search finds a shortest walk. Sides are tried in a fixed
        // order, so the same network always gives the same walk.
        std::deque<NodeId> open { from };
        while (!open.empty())
        {
            const NodeId at = open.front();
            open.pop_front();
            for (const NodeId n : nodes_[at].next)
            {
                if (n == kNoNode || came[n] != kNoNode || (nodes_[n].queue && target[n] == kNotTarget) || !may_step(at, n, who))
                    continue;
                came[n] = at;
                if (target[n] != kNotTarget)
                    return walk_to(n);
                open.push_back(n);
            }
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
