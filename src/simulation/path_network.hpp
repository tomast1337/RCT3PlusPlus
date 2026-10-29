// SPDX-License-Identifier: GPL-3.0-or-later
//
// The footpaths as the simulation walks them: one node per path tile, joined to the path tiles beside it.
// A tile is a column, a row and a level (height in steps); tiles side by side join when their levels differ
// by at most `max_step`, which is how slopes join a flat path to a higher one. Tiles join only where their
// surfaces meet along the edge between them, within kEdgeGap at both its ends: a raised path running past a
// lower one, or the side of a ramp, does not join it. A queue tile joins only across its two open ends, the
// sides its line comes in and goes out by; its railings close the rest.
//
// Where four footpath tiles at one level make a full 2 by 2 square, each joined to the two beside it, a walk
// may also cut across it from corner to opposite corner, at its true length. Queues never take part.
//
// A wall along a tile's edge keeps the tiles either side of it apart, unless one is a queue: a queue's own
// sides decide what it joins. A wall stands from one height to another; the lower of two tiles' middles must
// be within it to be kept apart.
//
// A tile taken away keeps its node, so the nodes walkers and attractions hold stay valid, but the node is
// joined to nothing and found by nothing; a tile laid there later gets a new node.
//
// A no-entry sign on a tile bars one of its sides to guests coming in across it; they may still leave that way.
// Staff walk past the signs, but never step onto a tile kept from them (off the park's land, or the park
// entrance's own path), unless they already stand on one, so a member of staff found outside can come back in.
//
// Nothing is sized to the original game's maps: columns, rows and levels are full integers.
#pragma once

#include "math/glm.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    using NodeId = std::uint32_t;
    inline constexpr NodeId kNoNode = ~NodeId { 0 };
    /// Most a path's surface may stand above or below its neighbour's at the ends of their shared edge and
    /// still join it. Chosen by eye: a quarter of a level absorbs rounding in saved heights, while a ramp's side
    /// or a raised path beside the ground is off by at least a whole level at one end.
    inline constexpr float kEdgeGap = 0.25f;

    /// Who walks: guests keep to the no-entry signs, staff to the tiles not kept from them.
    enum class Walker : std::uint8_t
    {
        Guest,
        Staff,
    };

    struct PathTileSpot
    {
        int col = 0;
        int row = 0;
        int level = 0;
        bool operator==(const PathTileSpot&) const = default;
    };

    struct PathNode
    {
        PathTileSpot tile;
        bool queue = false;
        float height = 0;            ///< World height of the middle of the tile
        std::uint64_t line = 0;      ///< Of a queue tile, the queue line it belongs to as the save names it; 0 if none
        bool covered = false;        ///< Under a roof that keeps the rain off
        bool shaded = false;         ///< Out of the sun: under a roof or trees
        bool removed = false;        ///< Its tile was taken away
        std::uint8_t no_entry = 0;   ///< Sides guests may not come in across, bit k for side k as `next` numbers them
        bool staff_kept_off = false; ///< Staff do not step onto it
        std::uint8_t open = 0b1111;  ///< Sides it may join across, bit k for side k as `next` numbers them; a queue only its two ends
        /// World heights of the path's surface at the tile's corners: low col low row, high col low row, low col
        /// high row, high col high row. Between them it is two flat triangles, split from the second corner to
        /// the third, as the ground is.
        std::array<float, 4> corners { };
        /// The node beside this one on each side (+col, +row, -col, -row), or kNoNode.
        std::array<NodeId, 4> next { kNoNode, kNoNode, kNoNode, kNoNode };
        /// The node a step across a square of paths reaches at each corner (+col +row, -col +row, -col -row,
        /// +col -row), or kNoNode.
        std::array<NodeId, 4> corner { kNoNode, kNoNode, kNoNode, kNoNode };
    };

    /// A way across the park that is not walked: a guest steps on at `from` and off at `to`, as if `length`
    /// steps apart, paying `toll`.
    struct PathLink
    {
        NodeId from = kNoNode;
        NodeId to = kNoNode;
        float length = 0;        ///< In steps between tiles side by side
        std::int64_t toll = 0;   ///< In cents
        std::uint32_t owner = 0; ///< What it stands for, as whoever made it numbers it
    };

    class PathNetwork
    {
    public:
        float tile_size = 4;       ///< World units across a tile
        float level_height = 1;    ///< World units per level
        glm::vec2 origin { 0.0f }; ///< World x and z of the corner of tile (0, 0)
        int max_step = 1;          ///< Most levels between tiles that join; set before adding tiles

        /// Adds a path tile and joins it to its neighbours; an existing tile at the same spot is returned instead.
        /// The middle of the tile is at `height`, or at its level when not given; a queue tile belongs to `line`.
        NodeId add(PathTileSpot tile, bool queue = false, std::optional<float> height = std::nullopt, std::uint64_t line = 0);

        /// Takes a tile away: its node is joined to nothing any more, and the squares it made lose their
        /// crossings.
        void remove(NodeId node);

        /// Stands a wall along `side` (as `next` numbers them) of tile (`col`, `row`), from world height `low` to
        /// `high`, and parts the tiles it stands between.
        void add_wall(int col, int row, std::size_t side, float low, float high);
        /// Takes such a wall away again, and joins what it kept apart.
        void remove_wall(int col, int row, std::size_t side, float low, float high);

        [[nodiscard]] const std::vector<PathNode>& nodes() const
        {
            return nodes_;
        }
        [[nodiscard]] std::optional<NodeId> find(PathTileSpot tile) const;
        /// Puts a queue tile's node in queue line `line`.
        void set_line(NodeId node, std::uint64_t line)
        {
            nodes_[node].line = line;
        }
        /// Gives a node's tile a surface that is not flat at its height (a ramp, or ground that is not level) and
        /// the sides it may join across (`open`, as PathNode::open); joins its tile again to suit.
        void set_corners(NodeId node, std::array<float, 4> corners, std::uint8_t open = 0b1111);
        /// Bars side `side` (as `next` numbers them) of a node's tile to guests coming in, or lifts the bar.
        void set_no_entry(NodeId node, std::size_t side, bool barred)
        {
            const auto bit = static_cast<std::uint8_t>(1U << (side % 4));
            nodes_[node].no_entry = barred ? nodes_[node].no_entry | bit : nodes_[node].no_entry & ~bit;
        }
        /// Keeps staff off a node's tile, or lets them on again.
        void keep_staff_off(NodeId node, bool kept)
        {
            nodes_[node].staff_kept_off = kept;
        }
        /// Whether `who` may step from `from` to `to`, a node beside it or across a square from it.
        [[nodiscard]] bool may_step(NodeId from, NodeId to, Walker who) const;
        /// Marks what is over a node's tile.
        void cover(NodeId node, bool covered, bool shaded)
        {
            nodes_[node].covered = covered;
            nodes_[node].shaded = shaded || covered;
        }
        /// The path tile whose column and row hold world point `at`, at the level nearest its height.
        [[nodiscard]] std::optional<NodeId> nearest(glm::vec3 at) const;
        /// The middle of a node's tile in world space.
        [[nodiscard]] glm::vec3 centre(NodeId node) const;
        /// The height of the path's surface under world point `at` (x and z) on a walk from `from` to `to`: on
        /// whichever of their tiles holds it, else on the path tile there nearest them in height.
        [[nodiscard]] float surface(NodeId from, NodeId to, glm::vec2 at) const;

        /// Adds a link; a queue tile it starts from is entered on the way only to take it.
        void link(const PathLink& link);
        /// Takes every link away.
        void clear_links()
        {
            links_.clear();
        }
        /// The link from `from` to `to`, if there is one.
        [[nodiscard]] std::optional<const PathLink&> link_between(NodeId from, NodeId to) const;

        /// The nodes after `from` on a shortest walk to `to`, ending with `to`; empty when `from` is `to`, none
        /// when no walk reaches it. Queues are only entered at `to`, never walked through. Only steps `who` may take.
        /// Given a `purse`, the way may also take links, as many as their tolls together fit it; a link shows in
        /// the walk as a step from its start to its end.
        [[nodiscard]] std::optional<std::vector<NodeId>> route(NodeId from, NodeId to, Walker who = Walker::Guest, std::optional<std::int64_t> purse = std::nullopt) const;
        /// Like route, to whichever of `targets` is nearest: its place in `targets` (the first, where several
        /// share a node) and the walk there. One search, however many targets.
        [[nodiscard]] std::optional<std::pair<std::size_t, std::vector<NodeId>>> route_nearest(NodeId from, std::span<const NodeId> targets, Walker who = Walker::Guest, std::optional<std::int64_t> purse = std::nullopt) const;

    private:
        static std::uint64_t key(int col, int row);
        /// Joins a node to the tiles beside it that nothing keeps apart.
        void join_sides(NodeId id);
        void join_corners(NodeId id);
        /// Unjoins the nodes on a tile and joins them again, and the squares round it.
        void rejoin(int col, int row);
        struct Wall
        {
            std::size_t side = 0;
            float low = 0;
            float high = 0;
            bool operator==(const Wall&) const = default;
        };
        std::unordered_multimap<std::uint64_t, Wall> walls_; ///< By tile, listed on both tiles of each
        bool corners_ = false;                               ///< Whether any square of paths has been found
        std::vector<PathNode> nodes_;
        std::unordered_multimap<std::uint64_t, NodeId> by_tile_;
        std::unordered_multimap<NodeId, PathLink> links_; ///< By the node each starts from
    };

} // namespace rct3::simulation
