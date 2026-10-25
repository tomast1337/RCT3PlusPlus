// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/structure_file.hpp"

#include "parkfile/saved_park.hpp"

#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("a structure file reads back the structure written")
{
    const save::Structure s {
        .name = "Arch",
        .cols = 2,
        .rows = 1,
        .tiles = { 1, 1 },
        .pieces = { { .symbol = "pillar", .overlay = "Scenery/Stone", .col = 1, .row = 0, .height = 4, .rotation = 3, .quadrant = 2, .colours = { 1, 2, 3 } } },
    };
    const auto bytes = parkfile::write_structure_file(s);
    const auto back = parkfile::read_structure_file(bytes);
    REQUIRE(back);
    CHECK(back->name == "Arch");
    CHECK(back->tiles == s.tiles);
    REQUIRE(back->pieces.size() == 1);
    CHECK(back->pieces[0].symbol == "pillar");
    CHECK(back->pieces[0].height == 4);
    CHECK(back->pieces[0].rotation == 3);
    CHECK(back->pieces[0].colours == s.pieces[0].colours);
    CHECK(parkfile::write_structure_file(*back) == bytes);
}

TEST_CASE("a structure file whose tiles do not fit its box, or a cut one, is refused")
{
    const save::Structure bad { .name = "Bad", .cols = 3, .rows = 2, .tiles = { 1 } };
    CHECK_FALSE(parkfile::read_structure_file(parkfile::write_structure_file(bad)));
    auto bytes = parkfile::write_structure_file({ .name = "Ok", .cols = 1, .rows = 1, .tiles = { 1 } });
    REQUIRE(parkfile::read_structure_file(bytes));
    bytes.pop_back();
    CHECK_FALSE(parkfile::read_structure_file(bytes));
}
