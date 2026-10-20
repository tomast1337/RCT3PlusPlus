// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_queues.hpp"

#include "park/ground_paths.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // Stored directions.
    constexpr std::uint8_t kMinusX = 0;
    constexpr std::uint8_t kPlusX = 1;
    constexpr std::uint8_t kMinusZ = 2;
    constexpr std::uint8_t kPlusZ = 3;

    save::TerrainGrid flat_grid(int width, int height, float level = 0)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.origin_x = -4;
        g.origin_z = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.corners = { level, level, level, level };
        return g;
    }

    save::PathTile queue(int col, int row, std::uint8_t start, std::uint8_t end, std::string style = "Lanes", std::array<std::int32_t, 3> colours = { 4, 5, 6 })
    {
        save::PathTile p;
        p.kind = save::PathKind::Queue;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        p.surface_name = std::move(style);
        p.surface_colours = colours;
        p.elevation = save::PathElevation { };
        p.elevation->slope = save::kSlopeOnGround;
        p.queue = save::PathQueueInfo { };
        p.queue->start_direction = start;
        p.queue->end_direction = end;
        return p;
    }

} // namespace

TEST_CASE("stored directions name the sides they point out of")
{
    CHECK(side_of(kMinusX) == TileSide::Left);
    CHECK(side_of(kPlusX) == TileSide::Right);
    CHECK(side_of(kMinusZ) == TileSide::Back);
    CHECK(side_of(kPlusZ) == TileSide::Front);
    CHECK_FALSE(side_of(save::kNoDirection).has_value());
    CHECK(side_step(TileSide::Right) == std::array<int, 2> { 1, 0 });
    CHECK(side_step(TileSide::Back) == std::array<int, 2> { 0, -1 });
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
    // Turned clockwise, the back side goes round Right, Front, Left: the cell's open sides follow.
    const auto open_sides = [](QueueShape s) {
        const int second = s.cell == QueueCell::TurnLeftSide ? 3 : 1;
        return std::array<int, 2> { s.turns % 4, (second + s.turns) % 4 };
    };
    for (const std::uint8_t start : { kMinusX, kPlusX, kMinusZ, kPlusZ })
    {
        for (const std::uint8_t end : { kMinusX, kPlusX, kMinusZ, kPlusZ })
        {
            const int a = static_cast<int>(*side_of(start));
            const int b = static_cast<int>(*side_of(end));
            if (a == b || (a + 2) % 4 == b)
                continue;
            const QueueShape s = queue_shape(start, end);
            REQUIRE(s.cell != QueueCell::Straight);
            CHECK(open_sides(s) == std::array<int, 2> { a, b });
        }
    }
}

TEST_CASE("a queue tile without usable sides is closed")
{
    CHECK(queue_shape(9, kPlusX).cell == QueueCell::Closed);
}

TEST_CASE("ground queues draw one whole tile each, grouped by style and colours")
{
    const save::TerrainGrid g = flat_grid(4, 2, 1);
    save::PathTile raised = queue(3, 1, kMinusX, kPlusX);
    raised.elevation->slope = 0;
    const std::vector<save::PathTile> paths { queue(0, 0, kMinusX, kPlusX), queue(1, 0, kMinusX, kPlusX), queue(2, 0, kMinusX, kPlusX, "Lanes", { 1, 1, 1 }), queue(9, 9, kMinusX, kPlusX), raised };
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
    const save::TerrainGrid g = flat_grid(3, 2);
    save::PathTile path;
    path.kind = save::PathKind::Ground;
    path.surface_name = "Gravel";
    save::PathTile below = path;
    below.row = 1;
    const std::vector<save::PathTile> paths { path, queue(1, 0, kMinusX, kPlusX), below, queue(1, 1, kPlusX, kMinusZ) };
    const GroundPathMap map = ground_path_map(paths, g);
    CHECK(path_continues(map, g, { 0, 0 }, 1, 0));       // The queue is open towards -x
    CHECK_FALSE(path_continues(map, g, { 0, 1 }, 1, 0)); // This one is open towards +x and -z only
}
