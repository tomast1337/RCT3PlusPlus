// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water/fill.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::water;

namespace
{

    // Six by one, flat at height 4 but for a dip of two tiles at height 1 (columns 1 and 2) and another at
    // column 4, with a ridge between them at column 3.
    save::model::TerrainGrid valley()
    {
        save::model::TerrainGrid g;
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

    bool any(save::model::Tile)
    {
        return true;
    }

} // namespace

TEST_CASE("water fills the dip it is poured in and stops at higher ground")
{
    const save::model::TerrainGrid grid = valley();
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
    const auto lake = fill_water(valley(), { 1, 0 }, 2, [](save::model::Tile t) { return t.col != 2; });
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
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(valley(), { 4, 0 }, 2, any));
    const auto poured = pour_water(water, valley(), { 0, 0 }, 4, any);
    REQUIRE(poured.has_value());
    CHECK(poured->bodies.size() == 1);
}

TEST_CASE("water poured over a ridge takes in the lake beyond it")
{
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(valley(), { 4, 0 }, 2, any));
    const auto poured = pour_water(water, valley(), { 1, 0 }, 4.5f, any);
    REQUIRE(poured.has_value());
    REQUIRE(poured->bodies.size() == 1);
    CHECK(poured->bodies[0].level == 5);
    CHECK(holds(poured->bodies[0], { 4, 0 }));
}

TEST_CASE("water that would spread to a refused tile is refused")
{
    CHECK_FALSE(pour_water({ }, valley(), { 1, 0 }, 2, [](save::model::Tile t) { return t.col != 2; }).has_value());
}

TEST_CASE("water that would reach a higher lake is refused")
{
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(valley(), { 1, 0 }, 3, any));
    // A stand-in: a lower pour from the same dip reaches the higher lake's tiles.
    CHECK_FALSE(pour_water(water, valley(), { 2, 0 }, 1.5f, any).has_value());
}

TEST_CASE("taking a lake away takes the waterfalls pouring from or into it")
{
    save::model::TerrainGrid grid = valley();
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(grid, { 1, 0 }, 2, any));
    water.bodies.push_back(*fill_water(grid, { 4, 0 }, 2, any));
    std::vector<save::model::Waterfall> falls(2);
    falls[0].top = { 6, 2, 2 };  // Over tile 1
    falls[1].top = { 18, 2, 2 }; // Over tile 4
    CHECK(remove_lake(water, falls, grid, { 2, 0 }));
    CHECK(water.bodies.size() == 1);
    REQUIRE(falls.size() == 1);
    CHECK(falls[0].top.x == 18);
    CHECK_FALSE(remove_lake(water, falls, grid, { 0, 0 }));
}

TEST_CASE("a lake keeps the water the reshaped ground still holds")
{
    const save::model::TerrainGrid was = valley();
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(was, { 1, 0 }, 3, any));
    water.bodies.push_back(*fill_water(was, { 4, 0 }, 1.5f, any));
    std::vector<save::model::Waterfall> falls;

    SUBCASE("ground moved away from every lake changes nothing")
    {
        save::model::TerrainGrid grid = was;
        grid.at(3, 0).corners = { 6, 6, 6, 6 };
        CHECK_FALSE(settle_water(water, falls, was, grid));
        CHECK(water.bodies.size() == 2);
    }
    SUBCASE("digging under it keeps it at its level")
    {
        save::model::TerrainGrid grid = was;
        grid.at(1, 0).corners = { 0, -1, 0, -1 };
        grid.at(2, 0).corners = { -1, 0, -1, 0 };
        settle_water(water, falls, was, grid);
        REQUIRE(water.bodies.size() == 2);
        CHECK(water.bodies[0].level == 3);
        CHECK(holds(water.bodies[0], { 1, 0 }));
        CHECK(holds(water.bodies[0], { 2, 0 }));
    }
    SUBCASE("a tile risen out of it dries, and one risen through its middle splits it")
    {
        save::model::TerrainGrid grid = was;
        grid.at(2, 0).corners = { 5, 5, 5, 5 };
        CHECK(settle_water(water, falls, was, grid));
        REQUIRE(water.bodies.size() == 2);
        CHECK(holds(water.bodies[0], { 1, 0 }));
        CHECK_FALSE(holds(water.bodies[0], { 2, 0 }));

        // Three tiles of water, the middle one raised.
        save::model::TerrainGrid wide = was;
        wide.at(3, 0).corners = { 1, 1, 1, 1 };
        save::model::WaterLayer one;
        one.bodies.push_back(*fill_water(wide, { 1, 0 }, 3, any));
        REQUIRE(holds(one.bodies[0], { 4, 0 }));
        save::model::TerrainGrid split = wide;
        split.at(3, 0).corners = { 5, 5, 5, 5 };
        CHECK(settle_water(one, falls, wide, split));
        REQUIRE(one.bodies.size() == 2);
        CHECK(one.bodies[0].level == 3);
        CHECK(one.bodies[1].level == 3);
        CHECK(holds(one.bodies[0], { 2, 0 }) != holds(one.bodies[1], { 2, 0 }));
        CHECK(holds(one.bodies[0], { 4, 0 }) != holds(one.bodies[1], { 4, 0 }));
    }
    SUBCASE("ground dug beside it floods")
    {
        save::model::TerrainGrid grid = was;
        grid.at(3, 0).corners = { 2, 4, 2, 4 }; // Open to the lake, shut to the dip beyond
        CHECK(settle_water(water, falls, was, grid));
        REQUIRE(water.bodies.size() == 2);
        CHECK(water.bodies[0].level == 3);
        CHECK(holds(water.bodies[0], { 3, 0 }));
        CHECK(holds(water.bodies[1], { 4, 0 }));
    }
    SUBCASE("a bank cut open lowers it to the highest whole level that holds")
    {
        save::model::TerrainGrid open = was;
        open.at(5, 0).corners = { 0, 0, 0, 0 }; // Dry low ground beyond the bank, as the stroke found it
        save::model::WaterLayer high;
        high.bodies.push_back(*fill_water(open, { 1, 0 }, 3, any));
        save::model::TerrainGrid cut = open;
        cut.at(3, 0).corners = { 2.5f, 2.5f, 2.5f, 2.5f };
        cut.at(4, 0).corners = { 1, 1, 1, 0 };
        CHECK(settle_water(high, falls, open, cut));
        REQUIRE(high.bodies.size() == 1);
        CHECK(high.bodies[0].level == 2);
        CHECK(holds(high.bodies[0], { 1, 0 }));
        CHECK_FALSE(holds(high.bodies[0], { 3, 0 }));
    }
    SUBCASE("it goes only when no level holds")
    {
        save::model::TerrainGrid open = was;
        open.at(4, 0).corners = { 1, 4, 1, 4 };
        open.at(5, 0).corners = { 0, 0, 0, 0 };
        save::model::WaterLayer low;
        low.bodies.push_back(*fill_water(open, { 4, 0 }, 2, any));
        REQUIRE(low.bodies[0].tris.size() == 2);
        save::model::TerrainGrid cut = open;
        cut.at(4, 0).corners = { 1, 0, 1, 0 };
        CHECK(settle_water(low, falls, open, cut));
        CHECK(low.bodies.empty());
    }
}

TEST_CASE("shaping the ground beside a waterfall takes it away and keeps its lakes")
{
    const save::model::TerrainGrid was = valley();
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(was, { 1, 0 }, 2, any));
    std::vector<save::model::Waterfall> falls(2);
    falls[0].lip_left = falls[0].lip_right = { 20, 4, 2 }; // On tile 5's edge
    falls[0].top = { 6, 2, 2 };                            // Over tile 1
    falls[0].bottom = { 22, 0, 2 };
    falls[1].lip_left = falls[1].lip_right = falls[1].bottom = { 2, 4, 2 }; // On tile 0
    falls[1].top = { 6, 2, 2 };
    save::model::TerrainGrid grid = was;
    grid.at(5, 0).corners = { 5, 5, 5, 5 };
    CHECK(settle_water(water, falls, was, grid));
    REQUIRE(falls.size() == 1);
    CHECK(falls[0].lip_left.x == 2);
    CHECK(water.bodies.size() == 1);

    // Its lake dried under it: it goes too.
    save::model::TerrainGrid dry = was;
    dry.at(1, 0).corners = { 4, 4, 4, 4 };
    dry.at(2, 0).corners = { 4, 4, 4, 4 };
    falls[0].lip_left = falls[0].lip_right = falls[0].bottom = { 22, 4, 2 }; // Far from the tiles moved
    falls[0].top = { 10, 2, 2 };                                             // Over tile 2
    CHECK(settle_water(water, falls, was, dry));
    CHECK(water.bodies.empty());
    CHECK(falls.empty());
}

TEST_CASE("taking a lake away puts back the ground its waterfalls cut")
{
    save::model::TerrainGrid grid = valley();
    const save::model::TerrainGrid was = grid;
    save::model::WaterLayer water;
    water.bodies.push_back(*fill_water(grid, { 1, 0 }, 2, any));
    std::vector<save::model::Waterfall> falls(1);
    falls[0].top = { 6, 2, 2 }; // Over tile 1
    falls[0].carved = { { .tile = { 3, 0 }, .corners = was.at(3, 0).corners } };
    grid.at(3, 0).corners = { 1, 1, 1, 1 };
    CHECK(remove_lake(water, falls, grid, { 1, 0 }));
    CHECK(falls.empty());
    CHECK(grid.at(3, 0).corners == was.at(3, 0).corners);
}
