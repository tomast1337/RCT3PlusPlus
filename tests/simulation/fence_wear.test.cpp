// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fence_wear.hpp"

#include "core/types.hpp"
#include "park/enclosures.hpp"
#include "simulation/animals.hpp"
#include "simulation/sim_world.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <string_view>

using namespace rct3;
using namespace rct3::simulation;
using save::model::FenceWear;

namespace
{
    /// A 6 by 6 park with one enclosure, fenced with `fence`, on the 4 by 4 tiles in its middle.
    save::model::EnclosureMap pen(std::string fence)
    {
        save::model::EnclosureMap map { .cols = 6, .rows = 6, .tiles = std::vector<std::optional<i32>>(36), .fences = { std::move(fence) } };
        for (i32 row = 1; row <= 4; ++row)
            for (i32 col = 1; col <= 4; ++col)
                map.tiles[static_cast<usize>(row * 6 + col)] = 0;
        return map;
    }

    f32 strength(std::string_view symbol)
    {
        if (symbol == "Lion" || symbol == "Electric")
            return 3;
        if (symbol == "Chain")
            return 2;
        return 0; // Goats, and wooden fences
    }

    /// `count` animals of `species` out of their crates in the pen.
    void let_in(SimWorld& world, std::string species, i32 count)
    {
        for (i32 i = 0; i < count; ++i)
            open_crate(world, add_animal(world, { .species = species, .enclosure = 0 }, { 2, 0, 2 }));
    }

    usize worn(const save::model::EnclosureMap& map)
    {
        return static_cast<usize>(std::ranges::count_if(map.wear, [](FenceWear w) { return w != FenceWear::Sound; }));
    }

    void run(SimWorld& world, save::model::EnclosureMap& map, f32 seconds)
    {
        for (f32 t = 0; t < seconds; t += 0.25f)
            step_fence_wear(world, map, strength, { 0.25f });
    }
} // namespace

TEST_CASE("fence wear: animals too strong for their fence wear it down far faster than age does")
{
    SimWorld weak_world;
    save::model::EnclosureMap weak = pen("Wood");
    let_in(weak_world, "Lion", 2);
    run(weak_world, weak, 600);
    SimWorld strong_world;
    save::model::EnclosureMap strong = pen("Electric");
    let_in(strong_world, "Lion", 2);
    run(strong_world, strong, 600);
    CHECK(worn(weak) >= 4);
    CHECK(worn(strong) < worn(weak));
}

TEST_CASE("fence wear: an enclosure with no animals out of their crates does not wear")
{
    SimWorld world;
    save::model::EnclosureMap map = pen("Wood");
    add_animal(world, { .species = "Lion", .enclosure = 0 }, { 2, 0, 2 }); // Never let out
    run(world, map, 3600);
    CHECK(worn(map) == 0);
}

TEST_CASE("fence wear: knocks damage straight panels, then break them; corners never wear")
{
    save::model::EnclosureMap map = pen("Wood");
    random::RandomState dice = random::seeded(random::Seed { 7 });
    const auto first = knock_fence(map, 0, dice);
    REQUIRE(first);
    CHECK(map.wear_at(first->col, first->row) == FenceWear::Damaged);
    // Eight tiles have panels (two on each side between the corners); each takes two knocks to break.
    for (i32 i = 1; i < 16; ++i)
        REQUIRE(knock_fence(map, 0, dice));
    CHECK_FALSE(knock_fence(map, 0, dice));
    for (const save::model::Tile corner_or_inside : std::array<save::model::Tile, 5> { { { 1, 1 }, { 4, 1 }, { 1, 4 }, { 4, 4 }, { 2, 2 } } })
        CHECK(map.wear_at(corner_or_inside.col, corner_or_inside.row) == FenceWear::Sound);
    CHECK(map.wear_at(2, 1) == FenceWear::Broken);
    CHECK(map.wear_at(1, 3) == FenceWear::Broken);
}

TEST_CASE("fence wear: where the fence is put up again, as a tile is taken out of an enclosure or put back, it is as new")
{
    save::model::EnclosureMap map = pen("Wood");
    random::RandomState dice = random::seeded(random::Seed { 7 });
    for (i32 i = 0; i < 16; ++i)
        (void)knock_fence(map, 0, dice);
    rct3::park::unenclose(map, { 2, 1 }, { 2, 1 }, [](save::model::Tile) { return true; });
    CHECK(map.wear_at(2, 1) == FenceWear::Sound);
    CHECK(map.wear_at(3, 1) == FenceWear::Sound); // A corner now
    CHECK(map.wear_at(3, 4) == FenceWear::Broken);
    rct3::park::enclose(map, { }, { 2, 1 }, { 2, 1 }, "Wood", [](save::model::Tile) { return true; });
    CHECK(map.wear_at(2, 1) == FenceWear::Sound);
    CHECK(map.wear_at(3, 4) == FenceWear::Broken);
}
