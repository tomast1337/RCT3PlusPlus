// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/queues.hpp"

#include "park/ground/paths.hpp"
#include "park/path/decks.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{

    // Stored directions.
    constexpr std::uint8_t kMinusX = 0;
    constexpr std::uint8_t kPlusX = 1;
    constexpr std::uint8_t kMinusZ = 2;
    constexpr std::uint8_t kPlusZ = 3;

    save::model::TerrainGrid flat_grid(int width, int height, float level = 0)
    {
        save::model::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.origin_x = -4;
        g.origin_z = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::model::TerrainCell& c : g.cells)
            c.corners = { level, level, level, level };
        return g;
    }

    save::model::PathTile queue(int col, int row, std::uint8_t start, std::uint8_t end, std::string style = "Lanes", std::array<std::int32_t, 3> colours = { 4, 5, 6 })
    {
        save::model::PathTile p;
        p.kind = save::model::PathKind::Queue;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        p.surface_name = std::move(style);
        p.surface_colours = colours;
        p.elevation = save::model::PathElevation { };
        p.elevation->slope = save::model::kSlopeOnGround;
        p.queue = save::model::PathQueueInfo { };
        p.queue->start_direction = start;
        p.queue->end_direction = end;
        return p;
    }

} // namespace

TEST_CASE("stored directions name the sides they point out of")
{
    CHECK(path::stored_heading(kMinusX) == Side::kColBefore);
    CHECK(path::stored_heading(kPlusX) == Side::kNextCol);
    CHECK(path::stored_heading(kMinusZ) == Side::kRowBefore);
    CHECK(path::stored_heading(kPlusZ) == Side::kNextRow);
    CHECK_FALSE(path::stored_heading(save::model::kNoDirection).has_value());
}

TEST_CASE("a queue tile open on opposite sides is straight, turned to their axis")
{
    CHECK(queue_shape(kMinusZ, kPlusZ).cell == QueueCell::Straight);
    CHECK(queue_shape(kMinusZ, kPlusZ).turns % 2 == 0);
    CHECK(queue_shape(kPlusZ, kMinusZ).turns % 2 == 0);
    CHECK(queue_shape(kPlusX, kMinusX).cell == QueueCell::Straight);
    CHECK(queue_shape(kPlusX, kMinusX).turns % 2 == 1);
    CHECK(queue_shape(kPlusX, kPlusX).cell == QueueCell::Straight);
}

TEST_CASE("a turn is turned so its open sides face the start and end")
{
    // Worked by hand: the picture's turns count from the row before towards the next column.
    const auto shape = [](std::uint8_t start, std::uint8_t end) {
        const QueueShape s = queue_shape(start, end);
        return std::pair { s.cell, int { s.turns } };
    };
    CHECK(shape(kMinusX, kMinusZ) == std::pair { QueueCell::TurnRightSide, 3 });
    CHECK(shape(kMinusX, kPlusZ) == std::pair { QueueCell::TurnLeftSide, 3 });
    CHECK(shape(kPlusX, kMinusZ) == std::pair { QueueCell::TurnLeftSide, 1 });
    CHECK(shape(kPlusX, kPlusZ) == std::pair { QueueCell::TurnRightSide, 1 });
    CHECK(shape(kMinusZ, kMinusX) == std::pair { QueueCell::TurnLeftSide, 0 });
    CHECK(shape(kMinusZ, kPlusX) == std::pair { QueueCell::TurnRightSide, 0 });
    CHECK(shape(kPlusZ, kMinusX) == std::pair { QueueCell::TurnRightSide, 2 });
    CHECK(shape(kPlusZ, kPlusX) == std::pair { QueueCell::TurnLeftSide, 2 });
}

TEST_CASE("a queue tile without usable sides is closed")
{
    CHECK(queue_shape(9, kPlusX).cell == QueueCell::Closed);
}

TEST_CASE("ground queues draw one whole tile each, grouped by style and colours")
{
    const save::model::TerrainGrid g = flat_grid(4, 2, 1);
    save::model::PathTile raised = queue(3, 1, kMinusX, kPlusX);
    raised.elevation->slope = 0;
    const std::vector<save::model::PathTile> paths { queue(0, 0, kMinusX, kPlusX), queue(1, 0, kMinusX, kPlusX), queue(2, 0, kMinusX, kPlusX, "Lanes", { 1, 1, 1 }), queue(9, 9, kMinusX, kPlusX), raised };
    const GroundQueues q = ground_queues(paths, g);
    REQUIRE(q.paintings.size() == 2);
    CHECK(q.paintings[0] == QueuePainting { "Lanes", { 4, 5, 6 } });
    REQUIRE(q.mesh.batches.size() == 2);
    CHECK(q.mesh.batches[0].paint == GroundPaint::Queue);
    CHECK(q.mesh.batches[0].count == 2 * 6);
    CHECK(q.mesh.batches[1].count == 6);
    for (const GroundVertex& v : q.mesh.vertices)
    {
        CHECK(v.position.y == doctest::Approx(1 + kPathLift));
        CHECK(v.uv.x >= 0);
        CHECK(v.uv.x <= 1);
    }
}

TEST_CASE("a ground path opens towards a queue's open end, not its walls")
{
    const save::model::TerrainGrid g = flat_grid(3, 2);
    save::model::PathTile path;
    path.kind = save::model::PathKind::Ground;
    path.surface_name = "Gravel";
    save::model::PathTile below = path;
    below.row = 1;
    const std::vector<save::model::PathTile> paths { path, queue(1, 0, kMinusX, kPlusX), below, queue(1, 1, kPlusX, kMinusZ) };
    const GroundPathMap map = ground_path_map(paths, g);
    CHECK(path_continues(map, g, { 0, 0 }, 1, 0));       // The queue is open towards -x
    CHECK_FALSE(path_continues(map, g, { 0, 1 }, 1, 0)); // This one is open towards +x and -z only
}
