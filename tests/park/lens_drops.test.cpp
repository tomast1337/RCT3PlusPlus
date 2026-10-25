// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/lens_drops.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3::park;

namespace
{
    LensWater one(LensDrop drop)
    {
        LensWater water;
        water.drops.push_back(drop);
        return water;
    }

    void run(LensWater& water, const LensPush& push, float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 60); ++i)
            run_lens_water(water, push, 1.0f / 60);
    }
} // namespace

TEST_CASE("wetting the lens lands no more drops and sheets than it holds")
{
    LensWater water;
    wet_lens(water, 1000, 1000, 1.5f);
    CHECK(std::ranges::count(water.drops, false, &LensDrop::sheet) == kMostLensDrops);
    CHECK(std::ranges::count(water.drops, true, &LensDrop::sheet) == kMostLensSheets);
    CHECK(std::ranges::all_of(water.drops, [](const LensDrop& d) { return d.at.x >= 0 && d.at.x <= 1.5f && d.radius > 0; }));
}

TEST_CASE("a big drop runs down the lens while a small one holds on")
{
    LensWater water;
    water.drops = { { .at = { 0.5f, 0.2f }, .radius = 0.02f, .life = 100 }, { .at = { 1.0f, 0.2f }, .radius = 0.004f, .life = 100 } };
    run(water, { .aspect = 1.5f }, 1.5f);
    REQUIRE(water.drops.size() == 2);
    CHECK(water.drops[0].at.y > 0.25f);
    CHECK(water.drops[1].at.y == doctest::Approx(0.2f));
}

TEST_CASE("wind blows drops across the lens")
{
    LensWater water = one({ .at = { 0.75f, 0.5f }, .radius = 0.006f, .life = 100 });
    run(water, { .aspect = 1.5f, .down = { 0, 0 }, .wind = { 1.0f, 0 } }, 1.5f);
    REQUIRE(water.drops.size() == 1);
    CHECK(water.drops[0].at.x > 0.8f);
}

TEST_CASE("turning the view throws drops the way the world sweeps")
{
    LensWater water = one({ .at = { 0.75f, 0.5f }, .radius = 0.006f, .life = 100 });
    run(water, { .aspect = 1.5f, .down = { 0, 0 }, .turn = { -2.0f, 0 } }, 1.0f);
    REQUIRE(water.drops.size() == 1);
    CHECK(water.drops[0].at.x < 0.7f);
}

TEST_CASE("flying forward pushes drops out from the middle")
{
    LensWater water = one({ .at = { 0.4f, 0.5f }, .radius = 0.006f, .life = 100 });
    run(water, { .aspect = 1.5f, .down = { 0, 0 }, .onrush = 200 }, 1.0f);
    REQUIRE(water.drops.size() == 1);
    CHECK(water.drops[0].at.x < 0.35f);
}

TEST_CASE("drops that touch run together, keeping their water")
{
    LensWater water;
    water.drops = { { .at = { 0.5f, 0.5f }, .radius = 0.006f, .clinging = 10, .life = 100 }, { .at = { 0.505f, 0.5f }, .radius = 0.008f, .clinging = 10, .life = 100 } };
    run_lens_water(water, { .aspect = 1.5f, .down = { 0, 0 } }, 1.0f / 60);
    REQUIRE(water.drops.size() == 1);
    CHECK(water.drops[0].radius == doctest::Approx(0.01f));
}

TEST_CASE("drops dry away and run off the screen")
{
    LensWater water;
    water.drops = { { .at = { 0.5f, 0.5f }, .radius = 0.004f, .life = 0.5f }, { .at = { 0.5f, 0.99f }, .radius = 0.03f, .life = 100 } };
    wet_lens(water, 0, 2, 1.5f);
    run(water, { .aspect = 1.5f }, 3.0f);
    CHECK(water.drops.empty());
}
