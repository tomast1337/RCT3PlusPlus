// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_making.hpp"

#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // Eight by four tiles of 4 units, all owned: a lake at height 10 over columns 0 and 1 of rows 0 to 2, and
    // one at height 2 over columns 4 and 5 of rows 0 to 3.
    save::TerrainGrid owned_grid()
    {
        save::TerrainGrid g;
        g.width = 8;
        g.height = 4;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(32);
        for (save::TerrainCell& c : g.cells)
            c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
        return g;
    }

    save::WaterBody lake(float level, int col0, int col1, int row0, int row1)
    {
        save::WaterBody b { .level = level };
        for (int r = row0; r <= row1; ++r)
            for (int c = col0; c <= col1; ++c)
                b.tris.push_back({ .col = static_cast<std::uint8_t>(c), .row = static_cast<std::uint8_t>(r) });
        return b;
    }

    const std::vector<save::WaterBody> kLakes { lake(10, 0, 1, 0, 2), lake(2, 4, 5, 0, 3) };

} // namespace

TEST_CASE("make_waterfall: pours over the higher lake's edge toward the lower one, along the whole edge")
{
    const auto fall = make_waterfall(owned_grid(), kLakes, 1, 0, { 1, 1 }); // Picked in either order
    REQUIRE(fall);
    CHECK(fall->flow.x == 1.0f);
    CHECK(fall->flow.z == 0.0f);
    CHECK(fall->lip_left.x == 12.0f); // The far side of column 2, the spillway beyond the edge
    CHECK(fall->lip_left.y == 10.0f);
    CHECK(std::abs(fall->lip_right.z - fall->lip_left.z) == 12.0f); // Rows 0 to 2
    CHECK(fall->bottom.y == 2.0f);
    CHECK(fall->bottom.x == 18.0f); // The middle of column 4
}

TEST_CASE("make_waterfall: refuses one lake, too little drop, too far apart and land not owned")
{
    const save::TerrainGrid grid = owned_grid();
    CHECK(make_waterfall(grid, kLakes, 0, 0, { }).error() == kWaterfallSameWater);
    std::vector<save::WaterBody> level = kLakes;
    level[1].level = 9;
    CHECK(make_waterfall(grid, level, 0, 1, { }).error() == kWaterfallTooLittleDrop);
    const std::vector<save::WaterBody> far { kLakes[0], lake(2, 7, 7, 3, 3) }; // Off to the side, not straight out
    CHECK(make_waterfall(grid, far, 0, 1, { }).error() == kWaterfallTooFarApart);
    save::TerrainGrid sold = grid;
    sold.at(3, 1).flags = 0;
    sold.at(3, 0).flags = 0;
    sold.at(3, 2).flags = 0;
    CHECK(make_waterfall(sold, kLakes, 0, 1, { }).error() == kWaterfallNotOwned);
}

TEST_CASE("waterfall_at: finds the fall over the ground picked, and none beside it")
{
    const auto fall = make_waterfall(owned_grid(), kLakes, 0, 1, { 1, 1 });
    REQUIRE(fall);
    const std::vector<save::Waterfall> falls { *fall };
    CHECK(waterfall_at(falls, 12.0f, 6.0f) == 0u);
    CHECK_FALSE(waterfall_at(falls, 12.0f, 30.0f));
    CHECK_FALSE(waterfall_at(falls, -10.0f, 6.0f));
}

TEST_CASE("make_waterfall: keeps the ground of the spillway, cut_spillway cuts it and restore_spillway puts it back")
{
    // A bank at 12 from column 2 on, the higher lake's floor at 8.
    save::TerrainGrid grid = owned_grid();
    for (int r = 0; r < grid.height; ++r)
        for (int c = 0; c < grid.width; ++c)
            grid.at(c, r).corners.fill(c < 2 ? 8.0f : c < 4 ? 12.0f : 0.0f);
    const auto fall = make_waterfall(grid, kLakes, 0, 1, { 1, 1 });
    REQUIRE(fall);
    CHECK(fall->carved.size() == 9); // Columns 1 to 3 of rows 0 to 2
    save::TerrainGrid cut = grid;
    cut_spillway(cut, *fall);
    constexpr float kFloor = 10 - kSpillwayDepth;
    constexpr float kRim = 10 + kSpillwayRim;
    CHECK(cut.at(1, 1).corners == std::array { 8.0f, kFloor, 8.0f, kFloor });     // The old edge sinks
    CHECK(cut.at(2, 1).corners == std::array { kFloor, kRim, kFloor, kRim });     // The spillway
    CHECK(cut.at(3, 1).corners == std::array { kFloor, kFloor, kFloor, kFloor }); // On the way down
    CHECK(cut.at(2, 3).corners == grid.at(2, 3).corners);                         // Beside the edge
    CHECK(cut.at(0, 1).corners == grid.at(0, 1).corners);
    restore_spillway(cut, *fall);
    for (std::size_t i = 0; i < grid.cells.size(); ++i)
        CHECK(cut.cells[i].corners == grid.cells[i].corners);
}

TEST_CASE("make_waterfall: cuts nothing when the lower lake lies against the edge")
{
    const std::vector<save::WaterBody> close { lake(10, 0, 1, 0, 2), lake(2, 2, 3, 0, 3) };
    const auto fall = make_waterfall(owned_grid(), close, 0, 1, { 1, 1 });
    REQUIRE(fall);
    CHECK(fall->carved.empty());
    CHECK(fall->lip_left.x == 8.0f);
}
