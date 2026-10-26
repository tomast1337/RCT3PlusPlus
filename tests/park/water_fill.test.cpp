// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_fill.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // Six by one, flat at height 4 but for a dip of two tiles at height 1 (columns 1 and 2) and another at
    // column 4, with a ridge between them at column 3.
    save::TerrainGrid valley()
    {
        save::TerrainGrid g;
        g.width = 6;
        g.height = 1;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(6);
        for (int c = 0; c < 6; ++c)
        {
            const bool low = c == 1 || c == 2 || c == 4;
            g.at(c, 0).corners = low ? std::array { 1.0f, 1.0f, 1.0f, 1.0f } : std::array { 4.0f, 4.0f, 4.0f, 4.0f };
        }
        return g;
    }

    bool any(save::Tile)
    {
        return true;
    }

} // namespace

TEST_CASE("water fills the dip it is poured in and stops at higher ground")
{
    const save::TerrainGrid grid = valley();
    const auto lake = fill_water(grid, { 1, 0 }, 2, any);
    REQUIRE(lake.has_value());
    CHECK(lake->tris.size() == 4);
    CHECK(holds(*lake, { 1, 0 }));
    CHECK(holds(*lake, { 2, 0 }));
    CHECK_FALSE(holds(*lake, { 4, 0 }));
    CHECK(lake->tris.front().mask == 0b111);
}

TEST_CASE("water poured where the ground is above the level holds nothing")
{
    const auto lake = fill_water(valley(), { 0, 0 }, 2, any);
    REQUIRE(lake.has_value());
    CHECK(lake->tris.empty());
}

TEST_CASE("a flood reaching a refused tile is refused")
{
    const auto lake = fill_water(valley(), { 1, 0 }, 2, [](save::Tile t) { return t.col != 2; });
    CHECK_FALSE(lake.has_value());
}

TEST_CASE("an item floating on water stands at its surface rounded up, and nowhere on dry land")
{
    const auto lake = fill_water(valley(), { 1, 0 }, 1.5f, any);
    REQUIRE(lake.has_value());
    const std::array bodies { *lake };
    CHECK(water_surface(bodies, { 1, 0 }) == 2);
    CHECK_FALSE(water_surface(bodies, { 4, 0 }).has_value());
}

TEST_CASE("water poured from a click rises to the clicked height rounded up")
{
    const auto water = pour_water({ }, valley(), { 1, 0 }, 1.2f, any);
    REQUIRE(water.has_value());
    REQUIRE(water->bodies.size() == 1);
    CHECK(water->bodies[0].level == 2);
    CHECK(holds(water->bodies[0], { 2, 0 }));
}

TEST_CASE("water poured on flat ground at a whole height leaves the water as it was")
{
    save::WaterLayer water;
    water.bodies.push_back(*fill_water(valley(), { 4, 0 }, 2, any));
    const auto poured = pour_water(water, valley(), { 0, 0 }, 4, any);
    REQUIRE(poured.has_value());
    CHECK(poured->bodies.size() == 1);
}

TEST_CASE("water poured over a ridge takes in the lake beyond it")
{
    save::WaterLayer water;
    water.bodies.push_back(*fill_water(valley(), { 4, 0 }, 2, any));
    const auto poured = pour_water(water, valley(), { 1, 0 }, 4.5f, any);
    REQUIRE(poured.has_value());
    REQUIRE(poured->bodies.size() == 1);
    CHECK(poured->bodies[0].level == 5);
    CHECK(holds(poured->bodies[0], { 4, 0 }));
}

TEST_CASE("water that would spread to a refused tile is refused")
{
    CHECK_FALSE(pour_water({ }, valley(), { 1, 0 }, 2, [](save::Tile t) { return t.col != 2; }).has_value());
}

TEST_CASE("water that would reach a higher lake is refused")
{
    save::WaterLayer water;
    water.bodies.push_back(*fill_water(valley(), { 1, 0 }, 3, any));
    // A stand-in: a lower pour from the same dip reaches the higher lake's tiles.
    CHECK_FALSE(pour_water(water, valley(), { 2, 0 }, 1.5f, any).has_value());
}

TEST_CASE("taking a lake away takes the waterfalls pouring from or into it")
{
    save::TerrainGrid grid = valley();
    save::WaterLayer water;
    water.bodies.push_back(*fill_water(grid, { 1, 0 }, 2, any));
    water.bodies.push_back(*fill_water(grid, { 4, 0 }, 2, any));
    std::vector<save::Waterfall> falls(2);
    falls[0].top = { 6, 2, 2 };  // Over tile 1
    falls[1].top = { 18, 2, 2 }; // Over tile 4
    CHECK(remove_lake(water, falls, grid, { 2, 0 }));
    CHECK(water.bodies.size() == 1);
    REQUIRE(falls.size() == 1);
    CHECK(falls[0].top.x == 18);
    CHECK_FALSE(remove_lake(water, falls, grid, { 0, 0 }));
}
