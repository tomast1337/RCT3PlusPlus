// SPDX-License-Identifier: GPL-3.0-or-later
//
// Queues laid on the ground. Like a ground path, a ground queue is a picture laid over its tile's terrain,
// but it covers the whole tile and has no see-through edges: the picture draws the lanes and the low walls
// round them. A queue tile is open on two sides, the ones its line of guests comes in and goes out by; the
// save names them as the queue's start and end directions (0 towards -x, 1 towards +x, 2 towards -z, 3
// towards +z).
//
// Each queue style has one recolourable picture in a 2 by 2 grid of cells (u across, v down):
//
//     [ straight, open at the top and bottom  | closed all round                ]
//     [ turn, open at the top and left        | turn, open at the top and right ]
//
// A tile shows one cell turned by quarter turns until its open sides face the right way. The two turns are
// mirror images, so a line of guests turning left and one turning right each keep their lanes' look. The
// queue's ground surface picks the colours of the picture's three recolourable slots.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// The sides of a tile, clockwise as seen from above: towards -z, +x, +z and -x.
    enum class TileSide : std::uint8_t
    {
        Back,
        Right,
        Front,
        Left,
    };

    /// The side a stored direction (0 -x, 1 +x, 2 -z, 3 +z) points out of; nothing for any other value.
    [[nodiscard]] std::optional<TileSide> side_of(std::uint8_t direction) noexcept;

    /// The step from a tile to its neighbour across `side`.
    [[nodiscard]] std::array<int, 2> side_step(TileSide side) noexcept;

    enum class QueueCell : std::uint8_t
    {
        Straight,
        Closed,
        TurnLeftSide,  ///< Open at the back and the left
        TurnRightSide, ///< Open at the back and the right
    };

    /// Which cell a queue tile shows and how many quarter turns clockwise it is turned by.
    struct QueueShape
    {
        QueueCell cell = QueueCell::Closed;
        std::uint8_t turns = 0;
    };

    /// The shape of a tile open on its start and end sides. A tile whose two sides are the same is drawn
    /// straight through on that axis; one without a usable side is closed.
    [[nodiscard]] QueueShape queue_shape(std::uint8_t start, std::uint8_t end) noexcept;

    /// A queue style painted in its ground surface's colours.
    struct QueuePainting
    {
        std::string style;
        std::array<std::int32_t, 3> colours { };
        friend bool operator==(const QueuePainting&, const QueuePainting&) = default;
    };

    struct GroundQueues
    {
        std::vector<QueuePainting> paintings; ///< GroundPaint::Queue batches index this
        TerrainMesh mesh;
    };

    /// True for a queue tile laid on the ground.
    [[nodiscard]] bool is_ground_queue(const save::PathTile& path) noexcept;

    /// The ground queues among `paths`, as one whole picture over each tile's ground, lifted as ground paths
    /// are. Tiles off the grid or without a style are left out.
    [[nodiscard]] GroundQueues ground_queues(std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

} // namespace rct3::park
