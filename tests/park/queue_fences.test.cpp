// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_fences.hpp"

#include "park/object/placement.hpp"
#include "park/path/furniture.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    save::model::TerrainGrid flat_grid()
    {
        save::model::TerrainGrid g;
        g.width = 8;
        g.height = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(64);
        return g;
    }

    save::model::PathTile queue(std::uint8_t start, std::uint8_t end, std::uint8_t slope)
    {
        save::model::PathTile p;
        p.kind = save::model::PathKind::Queue;
        p.col = 4;
        p.row = 7;
        p.elevation = save::model::PathElevation { };
        p.elevation->slope = slope;
        p.elevation->quantised_height = 6;
        p.direction = end; // Laid from its start towards its end
        p.queue = save::model::PathQueueInfo { };
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
    for (const Side side : ground::kSidesFromRowBefore)
    {
        const glm::vec2 x = object::turned_x(edge_turns(side));
        const auto [dc, dr] = side.step();
        CHECK(x.x == doctest::Approx(static_cast<float>(dc)));
        CHECK(x.y == doctest::Approx(static_cast<float>(dr)));
        CHECK(edge_turns(side) == object::turn_facing(side)); // An edge piece counts its turns as path furniture does
    }
}

TEST_CASE("a ground queue is fenced on every side but its two open ones, standing on the ground")
{
    const std::vector<save::model::PathTile> paths { queue(0, 1, save::model::kSlopeOnGround) }; // Open towards -x and +x
    const auto fences = queue_fences(paths, flat_grid(), 100).pieces;
    REQUIRE(fences.size() == 2);
    CHECK(fences[0].rotation == edge_turns(Side::kRowBefore).value);
    CHECK(fences[1].rotation == edge_turns(Side::kNextRow).value);
    for (const save::model::PlacedObject& f : fences)
    {
        CHECK(f.symbol == "Rail");
        CHECK(f.fixture);
        CHECK(f.overlay == "Fences/Rail");
        CHECK(f.colours == std::array<std::int32_t, 3> { 3, 2, 1 });
        CHECK(f.col == 4);
        CHECK(f.row == 7);
        CHECK_FALSE(f.base_height.has_value());
        CHECK(f.id > 100);
    }
    CHECK(fences[0].id != fences[1].id);
}

TEST_CASE("a level raised queue is fenced at its height; queues without a fence are not")
{
    const auto level = queue_fences(std::vector<save::model::PathTile> { queue(0, 2, 0) }, flat_grid(), 0).pieces;
    REQUIRE(level.size() == 2);
    CHECK(level[0].base_height == 6);
    CHECK(level[0].force_absolute_height);

    save::model::PathTile bare = queue(0, 1, save::model::kSlopeOnGround);
    bare.queue->fence_symbol.clear();
    CHECK(queue_fences(std::vector<save::model::PathTile> { bare }, flat_grid(), 0).pieces.empty());
}

TEST_CASE("a ground queue's railing leans with its edge of the ground")
{
    save::model::TerrainGrid g = flat_grid();
    g.cells[7 * 8 + 4].corners = { 1, 3, 1, 3 }; // Rising along +x by 2 over the tile
    const QueueFences fences = queue_fences(std::vector<save::model::PathTile> { queue(0, 1, save::model::kSlopeOnGround) }, g, 0);
    REQUIRE(fences.pieces.size() == 2);
    for (const save::model::PlacedObject& f : fences.pieces)
    {
        REQUIRE(fences.leans.contains(f.id));
        CHECK(fences.leans.at(f.id).x == doctest::Approx(0.5f));
        CHECK(fences.leans.at(f.id).y == doctest::Approx(0.0f));
    }
    CHECK(queue_fences(std::vector<save::model::PathTile> { queue(0, 1, save::model::kSlopeOnGround) }, flat_grid(), 0).leans.empty());
}

TEST_CASE("a sloping queue is fenced along its slope, halfway up and leaning with it")
{
    // Falling steeply from its start towards -x to its end towards +x, the way it faces: its stored height is
    // the start's.
    const QueueFences falling = queue_fences(std::vector<save::model::PathTile> { queue(0, 1, 2) }, flat_grid(), 0);
    REQUIRE(falling.pieces.size() == 2);
    for (const save::model::PlacedObject& f : falling.pieces)
    {
        CHECK(f.rotation != edge_turns(Side::kColBefore).value);
        CHECK(f.rotation != edge_turns(Side::kNextCol).value);
        CHECK(f.base_height == 4);
        CHECK(f.height_adjust == doctest::Approx(1.0f));
        CHECK(f.force_absolute_height);
        CHECK(falling.leans.at(f.id).x == doctest::Approx(-0.5f));
    }

    // A gentle climb towards +z.
    const QueueFences climbing = queue_fences(std::vector<save::model::PathTile> { queue(2, 3, 4) }, flat_grid(), 0);
    REQUIRE(climbing.pieces.size() == 2);
    const save::model::PlacedObject& f = climbing.pieces[0];
    CHECK(f.base_height == 6);
    CHECK(f.height_adjust == doctest::Approx(0.5f));
    CHECK(climbing.leans.at(f.id).x == doctest::Approx(0.0f));
    CHECK(climbing.leans.at(f.id).y == doctest::Approx(0.25f));

    // A line running the other way over the same piece leaves its railings as they were.
    save::model::PathTile reversed = queue(3, 2, 4);
    reversed.direction = 3;
    const QueueFences turned = queue_fences(std::vector<save::model::PathTile> { reversed }, flat_grid(), 0);
    REQUIRE(turned.pieces.size() == 2);
    CHECK(turned.pieces[0].base_height == 6);
    CHECK(turned.leans.at(turned.pieces[0].id).y == doctest::Approx(0.25f));
}
