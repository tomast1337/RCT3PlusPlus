// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/path_network.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
        constexpr std::array<std::array<i32, 2>, 4> kSides { { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } } };
    }

    u64 PathNetwork::key(i32 col, i32 row)
    {
        return (static_cast<u64>(static_cast<u32>(col)) << 32) | static_cast<u32>(row);
    }

    std::optional<NodeId> PathNetwork::find(PathTileSpot tile) const
    {
        const auto [first, last] = by_tile_.equal_range(key(tile.col, tile.row));
        for (auto it = first; it != last; ++it)
            if (nodes_[it->second.value].tile.level == tile.level)
                return it->second;
        return std::nullopt;
    }

    NodeId PathNetwork::add(PathTileSpot tile, bool queue, std::optional<f32> height, u64 line)
    {
        if (const auto found = find(tile))
            return *found;
        const NodeId id { static_cast<u32>(nodes_.size()) };
        const f32 middle = height.value_or(static_cast<f32>(tile.level) * level_height);
        nodes_.push_back({ .tile = tile, .queue = queue, .height = middle, .line = line, .corners = { middle, middle, middle, middle } });
        by_tile_.emplace(key(tile.col, tile.row), id);
        join_sides(id);
        // The squares the new tile closes, seen from each tile round it.
        for (i32 dc = -1; dc <= 1; ++dc)
            for (i32 dr = -1; dr <= 1; ++dr)
            {
                const auto [first, last] = by_tile_.equal_range(key(tile.col + dc, tile.row + dr));
                for (auto it = first; it != last; ++it)
                    join_corners(it->second);
            }
        return id;
    }

    void PathNetwork::remove(NodeId id)
    {
        if (id.value >= nodes_.size() || nodes_[id.value].removed)
            return;
        PathNode& n = nodes_[id.value];
        const auto [first, last] = by_tile_.equal_range(key(n.tile.col, n.tile.row));
        for (auto it = first; it != last; ++it)
            if (it->second == id)
            {
                by_tile_.erase(it);
                break;
            }
        for (usize side = 0; side < 4; ++side)
            if (const std::optional<NodeId> other = std::exchange(n.next[side], std::nullopt))
                nodes_[other->value].next[(side + 2) % 4].reset();
        n.corner.fill(std::nullopt);
        n.removed = true;
        for (i32 dc = -1; dc <= 1; ++dc)
            for (i32 dr = -1; dr <= 1; ++dr)
            {
                const auto [near_first, near_last] = by_tile_.equal_range(key(n.tile.col + dc, n.tile.row + dr));
                for (auto it = near_first; it != near_last; ++it)
                    join_corners(it->second);
            }
    }

    void PathNetwork::join_sides(NodeId id)
    {
        PathNode& node = nodes_[id.value];
        const PathTileSpot tile = node.tile;
        const auto [walls_first, walls_last] = walls_.equal_range(key(tile.col, tile.row));
        for (usize side = 0; side < kSides.size(); ++side)
        {
            const auto [first, last] = by_tile_.equal_range(key(tile.col + kSides[side][0], tile.row + kSides[side][1]));
            // The closest level within max_step, lowest first when two are as close.
            std::optional<NodeId> best;
            for (auto it = first; it != last; ++it)
            {
                const i32 gap = std::abs(nodes_[it->second.value].tile.level - tile.level);
                if (gap <= max_step && (!best || gap < std::abs(nodes_[best->value].tile.level - tile.level)))
                    best = it->second;
            }
            if (!best || nodes_[best->value].next[(side + 2) % 4])
                continue;
            PathNode& beside = nodes_[best->value];
            if ((node.open >> side & 1U) == 0 || (beside.open >> ((side + 2) % 4) & 1U) == 0)
                continue;
            // The corners either side of the shared edge, this tile's first, by side.
            constexpr std::array<std::array<usize, 4>, 4> kMeet { { { 1, 3, 0, 2 }, { 2, 3, 0, 1 }, { 0, 2, 1, 3 }, { 0, 1, 2, 3 } } };
            const auto& m = kMeet[side];
            const auto& a = node.corners;
            const auto& b = beside.corners;
            if (std::abs(a[m[0]] - b[m[2]]) > kEdgeGap || std::abs(a[m[1]] - b[m[3]]) > kEdgeGap)
                continue;
            const f32 low = std::min(node.height, beside.height);
            const bool walled = !node.queue && !beside.queue && std::any_of(walls_first, walls_last, [&](const auto& w) { return w.second.side == side && low >= w.second.low && low < w.second.high; });
            if (walled)
                continue;
            node.next[side] = best;
            beside.next[(side + 2) % 4] = id;
        }
    }

    void PathNetwork::set_corners(NodeId node, std::array<f32, 4> corners, u8 open)
    {
        PathNode& n = nodes_[node.value];
        n.corners = corners;
        n.open = open;
        rejoin(n.tile.col, n.tile.row);
    }

    void PathNetwork::rejoin(i32 col, i32 row)
    {
        const auto [first, last] = by_tile_.equal_range(key(col, row));
        for (auto it = first; it != last; ++it)
            for (usize side = 0; side < 4; ++side)
                if (const std::optional<NodeId> other = std::exchange(nodes_[it->second.value].next[side], std::nullopt))
                    nodes_[other->value].next[(side + 2) % 4].reset();
        for (auto it = first; it != last; ++it)
            join_sides(it->second);
        for (i32 dc = -1; dc <= 1; ++dc)
            for (i32 dr = -1; dr <= 1; ++dr)
            {
                const auto [near_first, near_last] = by_tile_.equal_range(key(col + dc, row + dr));
                for (auto it = near_first; it != near_last; ++it)
                    join_corners(it->second);
            }
    }

    void PathNetwork::add_wall(i32 col, i32 row, rct3::park::Side side, f32 low, f32 high)
    {
        const auto [across_col, across_row] = side.beyond({ col, row });
        walls_.emplace(key(col, row), Wall { network_side(side), low, high });
        walls_.emplace(key(across_col, across_row), Wall { network_side(side.opposite()), low, high });
        rejoin(col, row);
        rejoin(across_col, across_row);
    }

    void PathNetwork::remove_wall(i32 col, i32 row, rct3::park::Side side, f32 low, f32 high)
    {
        const auto [across_col, across_row] = side.beyond({ col, row });
        for (const auto& [at, wall] : { std::pair { key(col, row), Wall { network_side(side), low, high } }, std::pair { key(across_col, across_row), Wall { network_side(side.opposite()), low, high } } })
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
        PathNode& a = nodes_[id.value];
        for (usize k = 0; k < 4; ++k)
        {
            const usize across = (k + 1) % 4;
            const std::optional<NodeId> b = a.next[k];
            const std::optional<NodeId> c = a.next[across];
            const std::optional<NodeId> d = b ? nodes_[b->value].next[across] : std::nullopt;
            const bool square = !a.queue && c && d && nodes_[c->value].next[k] == d && std::ranges::all_of(std::array { *b, *c, *d }, [&](NodeId n) { return !nodes_[n.value].queue && nodes_[n.value].tile.level == a.tile.level; });
            a.corner[k] = square ? d : std::nullopt;
            corners_ = corners_ || square;
        }
    }

    std::optional<NodeId> PathNetwork::nearest(glm::vec3 at) const
    {
        const i32 col = static_cast<i32>(std::floor((at.x - origin.x) / tile_size));
        const i32 row = static_cast<i32>(std::floor((at.z - origin.y) / tile_size));
        const auto [first, last] = by_tile_.equal_range(key(col, row));
        std::optional<NodeId> best;
        f32 gap = std::numeric_limits<f32>::max();
        for (auto it = first; it != last; ++it)
        {
            const f32 g = std::abs(nodes_[it->second.value].height - at.y);
            if (g < gap)
            {
                gap = g;
                best = it->second;
            }
        }
        return best;
    }

    f32 PathNetwork::surface(NodeId from, NodeId to, glm::vec2 at) const
    {
        const f32 x = (at.x - origin.x) / tile_size;
        const f32 z = (at.y - origin.y) / tile_size;
        const PathTileSpot spot { static_cast<i32>(std::floor(x)), static_cast<i32>(std::floor(z)) };
        const auto holds = [&](NodeId n) { return nodes_[n.value].tile.col == spot.col && nodes_[n.value].tile.row == spot.row; };
        NodeId on = holds(to) ? to : from;
        if (!holds(on))
            on = nearest({ at.x, (nodes_[from.value].height + nodes_[to.value].height) / 2, at.y }).value_or(from);
        const PathNode& n = nodes_[on.value];
        const auto& c = n.corners;
        const f32 fx = std::clamp(x - static_cast<f32>(n.tile.col), 0.0f, 1.0f);
        const f32 fz = std::clamp(z - static_cast<f32>(n.tile.row), 0.0f, 1.0f);
        if (fx + fz <= 1)
            return c[0] + fx * (c[1] - c[0]) + fz * (c[2] - c[0]);
        return c[3] + (1 - fx) * (c[2] - c[3]) + (1 - fz) * (c[1] - c[3]);
    }

    glm::vec3 PathNetwork::centre(NodeId node) const
    {
        const PathTileSpot& t = nodes_[node.value].tile;
        return { origin.x + (static_cast<f32>(t.col) + 0.5f) * tile_size, nodes_[node.value].height, origin.y + (static_cast<f32>(t.row) + 0.5f) * tile_size };
    }

    bool PathNetwork::may_step(NodeId from, NodeId to, Walker who) const
    {
        const PathNode& a = nodes_[from.value];
        const PathNode& b = nodes_[to.value];
        if (who == Walker::Staff)
            return !b.staff_kept_off || a.staff_kept_off;
        // The sides of `to` the step comes in across: the one facing back, or the two round the corner it cuts.
        u8 across = 0;
        for (usize k = 0; k < 4; ++k)
        {
            if (a.next[k] == to)
                across |= static_cast<u8>(1U << ((k + 2) % 4));
            if (a.corner[k] == to)
                across |= static_cast<u8>((1U << ((k + 2) % 4)) | (1U << ((k + 3) % 4)));
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

    std::optional<std::vector<NodeId>> PathNetwork::route(NodeId from, NodeId to, Walker who, std::optional<i64> purse) const
    {
        auto found = route_nearest(from, std::span(&to, 1), who, purse);
        return found ? std::optional(std::move(found->walk)) : std::nullopt;
    }

    std::optional<PathNetwork::NearestRoute> PathNetwork::route_nearest(NodeId from, std::span<const NodeId> targets, Walker who, std::optional<i64> purse) const
    {
        const bool linked = purse && !links_.empty();
        if (from.value >= nodes_.size())
            return std::nullopt;
        constexpr usize kNotTarget = std::numeric_limits<usize>::max();
        std::vector<usize> target(nodes_.size(), kNotTarget); // The first of `targets` at each node
        for (usize i = targets.size(); i-- > 0;)
            if (targets[i].value < nodes_.size())
                target[targets[i].value] = i;
        if (target[from.value] != kNotTarget)
            return NearestRoute { target[from.value], std::vector<NodeId> { } };
        std::vector<std::optional<NodeId>> came(nodes_.size());
        const auto walk_to = [&](NodeId to) {
            std::vector<NodeId> walk;
            for (NodeId s = to; s != from; s = *came[s.value])
                walk.push_back(s);
            std::ranges::reverse(walk);
            return NearestRoute { target[to.value], std::move(walk) };
        };
        came[from.value] = from;
        // One standing in a queue may walk out along their own line.
        const auto own_line = [&](NodeId n) { return nodes_[from.value].queue && nodes_[n.value].queue && nodes_[n.value].line == nodes_[from.value].line; };
        if (corners_ || linked)
        {
            // Steps across squares and links are longer, so the shortest walk is found by length; ties go to the
            // lower node, so the same network always gives the same walk.
            constexpr f32 kAcross = std::numbers::sqrt2_v<f32>;
            std::vector<f32> length(nodes_.size(), std::numeric_limits<f32>::max());
            std::vector<i64> spent(linked ? nodes_.size() : 0); // Tolls paid on the way to each node
            std::priority_queue<std::pair<f32, NodeId>, std::vector<std::pair<f32, NodeId>>, std::greater<>> open;
            length[from.value] = 0;
            open.emplace(0.0f, from);
            while (!open.empty())
            {
                const auto [so_far, at] = open.top();
                open.pop();
                if (target[at.value] != kNotTarget)
                    return walk_to(at);
                if (so_far > length[at.value])
                    continue;
                const auto step = [&](std::optional<NodeId> to, f32 cost) {
                    if (!to)
                        return;
                    const u32 n = to->value;
                    if ((nodes_[n].queue && target[n] == kNotTarget && !(linked && links_.contains(*to)) && !own_line(*to)) || so_far + cost >= length[n] || !may_step(at, *to, who))
                        return;
                    length[n] = so_far + cost;
                    came[n] = at;
                    open.emplace(length[n], *to);
                    if (linked)
                        spent[n] = spent[at.value];
                };
                if (linked)
                {
                    const auto [first, last] = links_.equal_range(at);
                    for (auto it = first; it != last; ++it)
                    {
                        const PathLink& l = it->second;
                        const i64 paid = spent[at.value] + l.toll.value;
                        const u32 to = l.to.value;
                        if (paid > *purse || to >= nodes_.size() || nodes_[to].removed || so_far + l.length >= length[to] || (nodes_[to].queue && target[to] == kNotTarget))
                            continue;
                        length[to] = so_far + l.length;
                        came[to] = at;
                        spent[to] = paid;
                        open.emplace(length[to], l.to);
                    }
                    // From a queue come to only for its link, nowhere else.
                    if (nodes_[at.value].queue && at != from && !own_line(at))
                        continue;
                }
                for (const std::optional<NodeId> n : nodes_[at.value].next)
                    step(n, 1);
                for (const std::optional<NodeId> n : nodes_[at.value].corner)
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
            for (const std::optional<NodeId> n : nodes_[at.value].next)
            {
                if (!n || came[n->value] || (nodes_[n->value].queue && target[n->value] == kNotTarget && !own_line(*n)) || !may_step(at, *n, who))
                    continue;
                came[n->value] = at;
                if (target[n->value] != kNotTarget)
                    return walk_to(*n);
                open.push_back(*n);
            }
        }
        return std::nullopt;
    }

    std::optional<std::vector<NodeId>> PathNetwork::way_out(NodeId from) const
    {
        if (!nodes_[from.value].queue)
            return std::vector<NodeId> { };
        std::vector<std::optional<NodeId>> came(nodes_.size());
        std::deque<NodeId> open { from };
        came[from.value] = from;
        while (!open.empty())
        {
            const NodeId at = open.front();
            open.pop_front();
            for (const std::optional<NodeId> n : nodes_[at.value].next)
            {
                if (!n || came[n->value] || !may_step(at, *n, Walker::Guest) || (nodes_[n->value].queue && nodes_[n->value].line != nodes_[from.value].line))
                    continue;
                came[n->value] = at;
                if (!nodes_[n->value].queue)
                {
                    std::vector<NodeId> walk;
                    for (NodeId s = *n; s != from; s = *came[s.value])
                        walk.push_back(s);
                    std::ranges::reverse(walk);
                    return walk;
                }
                open.push_back(*n);
            }
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
