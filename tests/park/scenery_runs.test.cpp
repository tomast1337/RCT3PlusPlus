// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/scenery_runs.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::model::PlacedObject at(i32 col, i32 row, i32 rotation = 0, i32 quadrant = 0)
    {
        save::model::PlacedObject o;
        o.symbol = "Thing";
        o.col = col;
        o.row = row;
        o.rotation = rotation;
        o.quadrant = quadrant;
        return o;
    }

    std::vector<save::model::Tile> tiles(const std::vector<save::model::PlacedObject>& items)
    {
        std::vector<save::model::Tile> out;
        for (const save::model::PlacedObject& o : items)
            out.push_back({ o.col, o.row });
        return out;
    }
} // namespace

TEST_CASE("a dragged item fills the area spanned, the first where the drag began, each in the first's quarter and turn")
{
    const auto items = dragged_items(at(3, 3, 1, 2), { 1, 4 }, object::Positioning::Quarter);
    REQUIRE(items.size() == 6);
    CHECK(items.front().col == 3);
    CHECK(items.front().row == 3);
    for (const save::model::Tile t : std::vector<save::model::Tile> { { 1, 3 }, { 2, 4 }, { 3, 4 } })
        CHECK(std::ranges::contains(tiles(items), t));
    CHECK(std::ranges::all_of(items, [](const save::model::PlacedObject& o) { return o.rotation == 1 && o.quadrant == 2; }));
    CHECK(dragged_items(at(2, 2), { 2, 2 }, object::Positioning::Tile).size() == 1);
    // An item two tiles wide is set down two tiles apart, short of a tile it would only half cover.
    CHECK(tiles(dragged_items(at(0, 0), { 4, 0 }, object::Positioning::Tile, { 2, 1 })) == std::vector<save::model::Tile> { { 0, 0 }, { 2, 0 }, { 4, 0 } });
}

TEST_CASE("a dragged wall runs along the edge it stands on, whichever way the pointer strays")
{
    // Unturned, a wall faces the column before, so it runs along the column; a quarter turn on, along the row.
    const auto down = dragged_items(at(2, 2, 0), { 5, 6 }, object::Positioning::Edge);
    CHECK(tiles(down) == std::vector<save::model::Tile> { { 2, 2 }, { 2, 3 }, { 2, 4 }, { 2, 5 }, { 2, 6 } });
    const auto across = dragged_items(at(2, 2, 1), { 0, 6 }, object::Positioning::Edge);
    CHECK(tiles(across) == std::vector<save::model::Tile> { { 2, 2 }, { 1, 2 }, { 0, 2 } });
}

TEST_CASE("a scatter brush puts items within its reach, kept apart, until there is no room left")
{
    random::RandomState stream = random::seeded({ 7 });
    const glm::vec2 centre { 10, 10 };
    std::vector<glm::vec2> taken;
    for (i32 i = 0; i < 400; ++i)
        if (const auto spot = scatter_spot(stream, centre, 2.0f, Scatter::Dense, taken))
            taken.push_back(*spot);
    CHECK(taken.size() > 4);
    CHECK(taken.size() < 20); // A disc of radius 2 has no room for more a tile apart
    for (usize a = 0; a < taken.size(); ++a)
    {
        CHECK(glm::distance(taken[a], centre) <= 2.0f);
        for (usize b = a + 1; b < taken.size(); ++b)
            CHECK(glm::distance(taken[a], taken[b]) >= 1.0f);
    }
    CHECK_FALSE(scatter_spot(stream, centre, 2.0f, Scatter::Off, { }));
    const glm::vec2 one[] { centre };
    random::RandomState sparse = random::seeded({ 7 });
    const auto spot = scatter_spot(sparse, centre, 4.0f, Scatter::Sparse, one);
    REQUIRE(spot);
    CHECK(glm::distance(*spot, centre) >= 2.0f);
}
