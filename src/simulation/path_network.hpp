// SPDX-License-Identifier: GPL-3.0-or-later
//
// The footpaths as the simulation walks them: one node per path tile, joined to the path tiles beside it.
// A tile is a column, a row and a level (height in steps); tiles side by side join when their levels differ
// by at most `max_step`, which is how slopes join a flat path to a higher one.
//
// Where four footpath tiles at one level make a full 2 by 2 square, each joined to the two beside it, a walk
// may also cut across it from corner to opposite corner, at its true length. Queues never take part.
//
// Nothing is sized to the original game's maps: columns, rows and levels are full integers.
#pragma once

#include "math/glm.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rct3::simulation
{

    using NodeId = std::uint32_t;
    inline constexpr NodeId kNoNode = ~NodeId { 0 };

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
        float height = 0;       ///< World height of the middle of the tile
        std::uint64_t line = 0; ///< Of a queue tile, the queue line it belongs to as the save names it; 0 if none
        /// The node beside this one on each side (+col, +row, -col, -row), or kNoNode.
        std::array<NodeId, 4> next { kNoNode, kNoNode, kNoNode, kNoNode };
        /// The node a step across a square of paths reaches at each corner (+col +row, -col +row, -col -row,
        /// +col -row), or kNoNode.
        std::array<NodeId, 4> corner { kNoNode, kNoNode, kNoNode, kNoNode };
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

        [[nodiscard]] const std::vector<PathNode>& nodes() const
        {
            return nodes_;
        }
        [[nodiscard]] std::optional<NodeId> find(PathTileSpot tile) const;
        /// The path tile whose column and row hold world point `at`, at the level nearest its height.
        [[nodiscard]] std::optional<NodeId> nearest(glm::vec3 at) const;
        /// The middle of a node's tile in world space.
        [[nodiscard]] glm::vec3 centre(NodeId node) const;

        /// The nodes after `from` on a shortest walk to `to`, ending with `to`; empty when `from` is `to`, none
        /// when no walk reaches it. Queues are only entered at `to`, never walked through.
        [[nodiscard]] std::optional<std::vector<NodeId>> route(NodeId from, NodeId to) const;

    private:
        static std::uint64_t key(int col, int row);
        void join_corners(NodeId id);
        bool corners_ = false; ///< Whether any square of paths has been found
        std::vector<PathNode> nodes_;
        std::unordered_multimap<std::uint64_t, NodeId> by_tile_;
    };

} // namespace rct3::simulation
