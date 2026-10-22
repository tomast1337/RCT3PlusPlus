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
