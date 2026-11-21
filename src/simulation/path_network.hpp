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
//
// The network counts a tile's sides its own way, +col, +row, -col, -row (`next`, `open`, `no_entry`), the order
// its searches try them in. Walls and no-entry signs are given as park sides and converted by network_side.
#pragma once

#include "core/id.hpp"
#include "core/types.hpp"
#include "math/glm.hpp"
#include "park/turns.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    /// A path tile's place in the network's nodes.
    using NodeId = core::Id<struct NodeIdTag, u32>;

    /// The network's number for park side `side`: the park's count with each pair swapped.
    [[nodiscard]] constexpr usize network_side(rct3::park::Side side) noexcept
    {
        return static_cast<usize>(side.value ^ 1);
    }
    /// Most a path's surface may stand above or below its neighbour's at the ends of their shared edge and
    /// still join it. Chosen by eye: a quarter of a level absorbs rounding in saved heights, while a ramp's side
    /// or a raised path beside the ground is off by at least a whole level at one end.
    inline constexpr f32 kEdgeGap = 0.25f;

    /// Who walks: guests keep to the no-entry signs, staff to the tiles not kept from them.
    enum class Walker : u8
    {
        Guest,
        Staff,
    };

    /// What is over a path tile.
    enum class Cover : u8
    {
        Open,
        Shade, ///< Trees, which keep the sun off but not the rain
        Roof,  ///< Keeps the rain and the sun off
    };

    struct PathTileSpot
    {
        i32 col = 0;
        i32 row = 0;
        i32 level = 0;
        bool operator==(const PathTileSpot&) const = default;
    };

    struct PathNode
    {
        PathTileSpot tile;
        bool queue = false;
        f32 height = 0;       ///< World height of the middle of the tile
        u64 line = 0;         ///< Of a queue tile, the queue line it belongs to as the save names it; 0 if none
        bool covered = false; ///< Under a roof that keeps the rain off
        bool shaded = false;  ///< Out of the sun: under a roof or trees
        bool removed = false; ///< Its tile was taken away
        u8 no_entry = 0;      ///< Sides guests may not come in across, bit k for side k as `next` numbers them
        bool staff_kept_off = false;
        u8 open = 0b1111; ///< Sides it may join across, bit k for side k as `next` numbers them; a queue only its two ends
        /// World heights of the path's surface at the tile's corners: low col low row, high col low row, low col
        /// high row, high col high row. Between them it is two flat triangles, split from the second corner to
        /// the third, as the ground is.
        std::array<f32, 4> corners { };
        /// The node beside this one on each side (+col, +row, -col, -row), if any.
        std::array<std::optional<NodeId>, 4> next { };
        /// The node a step across a square of paths reaches at each corner (+col +row, -col +row, -col -row,
        /// +col -row), if any.
        std::array<std::optional<NodeId>, 4> corner { };
    };

    /// A way across the park that is not walked: a guest steps on at `from` and off at `to`, as if `length`
    /// steps apart, paying `toll`.
    struct PathLink
    {
        NodeId from { };
        NodeId to { };
        f32 length = 0; ///< In steps between tiles side by side
        units::Money toll { };
        u32 owner = 0; ///< What it stands for, as whoever made it numbers it
    };

    class PathNetwork
    {
    public:
        f32 tile_size = 4;         ///< World units across a tile
        f32 level_height = 1;      ///< World units per level
        glm::vec2 origin { 0.0f }; ///< World x and z of the corner of tile (0, 0)
        i32 max_step = 1;          ///< Most levels between tiles that join; set before adding tiles

        /// Adds a path tile and joins it to its neighbours; an existing tile at the same spot is returned instead.
        /// The middle of the tile is at `height`, or at its level when not given; a queue tile belongs to `line`.
        NodeId add(PathTileSpot tile, bool queue = false, std::optional<f32> height = std::nullopt, u64 line = 0);

        /// Takes a tile away: its node is joined to nothing any more, and the squares it made lose their
        /// crossings.
        void remove(NodeId node);

        /// Stands a wall along `side` of tile (`col`, `row`), from world height `low` to `high`, and parts the
        /// tiles it stands between.
        void add_wall(i32 col, i32 row, rct3::park::Side side, f32 low, f32 high);
        /// Takes such a wall away again, and joins what it kept apart.
        void remove_wall(i32 col, i32 row, rct3::park::Side side, f32 low, f32 high);

        [[nodiscard]] const std::vector<PathNode>& nodes() const
        {
            return nodes_;
        }
        [[nodiscard]] std::optional<NodeId> find(PathTileSpot tile) const;
        /// Puts a queue tile's node in queue line `line`.
        void set_line(NodeId node, u64 line)
        {
            nodes_[node.value].line = line;
        }
        /// Moves a node to `level` with its middle at `height`, as when the ground under it rose or sank; set its
        /// corners after, which joins it again.
        void set_height(NodeId node, i32 level, f32 height)
        {
            nodes_[node.value].tile.level = level;
            nodes_[node.value].height = height;
        }
        /// Gives a node's tile a surface that is not flat at its height (a ramp, or ground that is not level) and
        /// the sides it may join across (`open`, as PathNode::open); joins its tile again to suit.
        void set_corners(NodeId node, std::array<f32, 4> corners, u8 open = 0b1111);
        /// Bars side `side` of a node's tile to guests coming in, or lifts the bar.
        void set_no_entry(NodeId node, rct3::park::Side side, bool barred)
        {
            const auto bit = static_cast<u8>(1U << network_side(side));
            nodes_[node.value].no_entry = barred ? nodes_[node.value].no_entry | bit : nodes_[node.value].no_entry & ~bit;
        }
        /// Keeps staff off a node's tile, or lets them on again.
        void keep_staff_off(NodeId node, bool kept)
        {
            nodes_[node.value].staff_kept_off = kept;
        }
        /// Whether `who` may step from `from` to `to`, a node beside it or across a square from it.
        [[nodiscard]] bool may_step(NodeId from, NodeId to, Walker who) const;
        void cover(NodeId node, Cover cover)
        {
            nodes_[node.value].covered = cover == Cover::Roof;
            nodes_[node.value].shaded = cover != Cover::Open;
        }
        /// The path tile whose column and row hold world point `at`, at the level nearest its height.
        [[nodiscard]] std::optional<NodeId> nearest(glm::vec3 at) const;
        /// The middle of a node's tile in world space.
        [[nodiscard]] glm::vec3 centre(NodeId node) const;
        /// The height of the path's surface under world point `at` (x and z) on a walk from `from` to `to`: on
        /// whichever of their tiles holds it, else on the path tile there nearest them in height.
        [[nodiscard]] f32 surface(NodeId from, NodeId to, glm::vec2 at) const;

        /// Adds a link; a queue tile it starts from is entered on the way only to take it.
        void link(const PathLink& link);
        void clear_links()
        {
            links_.clear();
        }
        [[nodiscard]] std::optional<const PathLink&> link_between(NodeId from, NodeId to) const;
        /// Every link, in the order the network goes through them, which adding them in again keeps.
        [[nodiscard]] std::vector<PathLink> links() const;

        /// The nodes after `from` on a shortest walk to `to`, ending with `to`; empty when `from` is `to`, none
        /// when no walk reaches it. Queues are only entered at `to`, never walked through, but one starting in a queue
        /// may walk out along its line. Only steps `who` may take.
        /// Given a `purse`, the way may also take links, as many as their tolls together fit it; a link shows in
        /// the walk as a step from its start to its end.
        [[nodiscard]] std::optional<std::vector<NodeId>> route(NodeId from, NodeId to, Walker who = Walker::Guest, std::optional<i64> purse = std::nullopt) const;
        /// The nearest of several targets, and the walk there.
        struct NearestRoute
        {
            usize target { };             ///< Its place among the targets: the first, where several share a node
            std::vector<NodeId> walk { }; ///< As route gives it
        };
        /// Like route, to whichever of `targets` is nearest. One search, however many targets.
        [[nodiscard]] std::optional<NearestRoute> route_nearest(NodeId from, std::span<const NodeId> targets, Walker who = Walker::Guest, std::optional<i64> purse = std::nullopt) const;
        /// From a queue tile, the walk along its line to the nearest tile off the queue a guest may step onto, as
        /// route gives it; empty when `from` is no queue, none when the line leads nowhere.
        [[nodiscard]] std::optional<std::vector<NodeId>> way_out(NodeId from) const;

    private:
        static u64 key(i32 col, i32 row);
        /// Joins a node to the tiles beside it that nothing keeps apart.
        void join_sides(NodeId id);
        void join_corners(NodeId id);
        /// Unjoins the nodes on a tile and joins them again, and the squares round it.
        void rejoin(i32 col, i32 row);
        struct Wall
        {
            usize side = 0; ///< As `next` numbers them
            f32 low = 0;
            f32 high = 0;
            bool operator==(const Wall&) const = default;
        };
        std::unordered_multimap<u64, Wall> walls_; ///< By tile, listed on both tiles of each
        bool corners_ = false;                     ///< Whether any square of paths has been found
        std::vector<PathNode> nodes_;
        std::unordered_multimap<u64, NodeId> by_tile_;
        std::unordered_multimap<NodeId, PathLink> links_; ///< By the node each starts from
    };

} // namespace rct3::simulation
