// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_fences.hpp"

#include "park/object_placement.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    save::PathTile queue(std::uint8_t start, std::uint8_t end, std::uint8_t slope)
    {
        save::PathTile p;
        p.kind = save::PathKind::Queue;
        p.col = 4;
        p.row = 7;
        p.elevation = save::PathElevation { };
        p.elevation->slope = slope;
        p.elevation->quantised_height = 6;
        p.queue = save::PathQueueInfo { };
        p.queue->start_direction = start;
        p.queue->end_direction = end;
        p.queue->fence_symbol = "Rail";
        p.queue->fence_overlay = "Fences/Rail";
        p.queue->fence_colours = { 3, 2, 1 };
        return p;
    }

} // namespace

TEST_CASE("an edge piece is turned to face out of its side")
{
    for (const TileSide side : { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left })
    {
        const glm::vec2 x = turned_x(edge_turns(side));
        const auto [dc, dr] = side_step(side);
        CHECK(x.x == doctest::Approx(static_cast<float>(dc)));
        CHECK(x.y == doctest::Approx(static_cast<float>(dr)));
    }
}

TEST_CASE("a ground queue is fenced on every side but its two open ones, standing on the ground")
{
    const std::vector<save::PathTile> paths { queue(0, 1, save::kSlopeOnGround) }; // Open towards -x and +x
    const auto fences = queue_fences(paths, 100);
    REQUIRE(fences.size() == 2);
    CHECK(fences[0].rotation == edge_turns(TileSide::Back));
    CHECK(fences[1].rotation == edge_turns(TileSide::Front));
    for (const save::PlacedObject& f : fences)
    {
        CHECK(f.symbol == "Rail");
        CHECK(f.overlay == "Fences/Rail");
        CHECK(f.colours == std::array<std::int32_t, 3> { 3, 2, 1 });
        CHECK(f.col == 4);
        CHECK(f.row == 7);
        CHECK_FALSE(f.base_height.has_value());
        CHECK(f.id > 100);
    }
    CHECK(fences[0].id != fences[1].id);
}

TEST_CASE("a level raised queue is fenced at its height; sloping ones and queues without a fence are not")
{
    const auto level = queue_fences(std::vector<save::PathTile> { queue(0, 2, 0) }, 0);
    REQUIRE(level.size() == 2);
    CHECK(level[0].base_height == 6);
    CHECK(level[0].force_absolute_height);

    CHECK(queue_fences(std::vector<save::PathTile> { queue(0, 1, 1) }, 0).empty());
    save::PathTile bare = queue(0, 1, save::kSlopeOnGround);
    bare.queue->fence_symbol.clear();
    CHECK(queue_fences(std::vector<save::PathTile> { bare }, 0).empty());
}
