// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fence_wear.hpp"

#include "core/types.hpp"
#include "park/enclosures.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/staff/work.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

    const AnimalGround kFlat { .height = [](glm::vec2) { return 0.0f; } };

    void run(SimWorld& world, save::model::EnclosureMap& map, f32 seconds)
    {
        rct3::simulation::park::ParkMessages messages;
        for (f32 t = 0; t < seconds; t += 0.25f)
            step_fence_wear(world, map, kFlat, strength, { 0.25f }, messages);
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

TEST_CASE("fence wear: knocking the fences by hand wears panels until none are left to break")
{
    SimWorld world;
    save::model::EnclosureMap map = pen("Wood");
    CHECK(knock_fences(world, map, 10) == 10);
    CHECK(knock_fences(world, map, 10) == 6); // Eight panelled tiles, two knocks each
    CHECK(std::ranges::count(map.wear, FenceWear::Broken) == 8);
}

namespace
{
    /// A park with a path along row 0 from column 0 to 9, and a wooden enclosure on columns 2 to 5, rows 2 to 5,
    /// whose top panel at column 3 is broken and at column 4 dented.
    struct MendingPark
    {
        Sandbox park;
        save::model::EnclosureMap map;
        rct3::simulation::park::ParkMessages messages;

        MendingPark()
        {
            park.path({ 0, 0 }, { 9, 0 });
            map = { .cols = 10, .rows = 10, .tiles = std::vector<std::optional<i32>>(100), .fences = { "Wood" } };
            for (i32 row = 2; row <= 5; ++row)
                for (i32 col = 2; col <= 5; ++col)
                    map.tiles[static_cast<usize>(row * 10 + col)] = 0;
            map.wear = std::vector<FenceWear>(100);
            map.wear[3 + 20] = FenceWear::Broken;
            map.wear[4 + 20] = FenceWear::Damaged;
        }

        bool run_until(const std::function<bool()>& done, f32 seconds)
        {
            for (f32 t = 0; t < seconds; t += kTickSeconds)
            {
                park.run(1);
                step_fence_wear(park.world(), map, kFlat, strength, { kTickSeconds }, messages);
                if (done())
                    return true;
            }
            return false;
        }
    };
} // namespace

TEST_CASE("fence wear: a free mechanic walks to the path nearest a broken fence and mends it, then the dented one")
{
    MendingPark p;
    auto& r = p.park.world().registry;
    const auto mechanic = p.park.staff(StaffRole::Mechanic, { 9, 0 });
    CHECK(p.run_until([&] { return r.all_of<staff::FenceMending>(mechanic); }, 2));
    CHECK(r.get<staff::FenceMending>(mechanic).tile == save::model::Tile { 3, 2 }); // Broken first
    CHECK(p.run_until([&] { return p.map.wear_at(3, 2) == FenceWear::Sound; }, 120));
    CHECK(r.get<Staff>(mechanic).fixed == 1);
    CHECK(p.run_until([&] { return p.map.wear_at(4, 2) == FenceWear::Sound; }, 120));
    CHECK(r.get<Staff>(mechanic).inspected == 1);
    CHECK_FALSE(r.all_of<staff::FenceMending>(mechanic));
}

TEST_CASE("fence wear: a mechanic told not to fix, or kept to a patrol away from the fence, leaves it broken")
{
    MendingPark p;
    auto& r = p.park.world().registry;
    p.map.wear[4 + 20] = FenceWear::Sound;
    const auto told = p.park.staff(StaffRole::Mechanic, { 9, 0 });
    r.get<Staff>(told).tasks &= ~kFixRidesTask;
    const auto kept = p.park.staff(StaffRole::Mechanic, { 9, 0 });
    r.emplace<staff::Patrol>(kept).tiles = { { 9, 0 }, { 8, 0 } };
    CHECK_FALSE(p.run_until([&] { return r.all_of<staff::FenceMending>(told) || r.all_of<staff::FenceMending>(kept); }, 30));
    CHECK(p.map.wear_at(3, 2) == FenceWear::Broken);
}

TEST_CASE("fence wear: a fence far from every path is mended from the nearest")
{
    MendingPark p;
    auto& r = p.park.world().registry;
    p.map.wear = std::vector<FenceWear>(100);
    p.map.wear[3 + 50] = FenceWear::Broken; // The bottom side, five rows from the path
    const auto mechanic = p.park.staff(StaffRole::Mechanic, { 9, 0 });
    CHECK(p.run_until([&] { return r.all_of<staff::FenceMending>(mechanic) && r.get<Stride>(mechanic).route.empty(); }, 60));
    CHECK(p.park.world().paths.nodes()[r.get<Stride>(mechanic).to.value].tile.col == 3);
    CHECK(p.run_until([&] { return p.map.wear_at(3, 5) == FenceWear::Sound; }, 120));
}

TEST_CASE("fence wear: the player is told as a fence breaks, reminded while it stays broken, and told once it is mended")
{
    MendingPark p;
    p.map.wear = std::vector<FenceWear>(100);
    const auto keys = [&] {
        std::vector<std::string> out;
        for (const rct3::simulation::park::ParkMessage& m : p.messages.list)
            out.push_back(m.text_key);
        return out;
    };
    p.run_until([] { return false; }, 2);
    CHECK(p.messages.list.empty());
    p.map.wear[3 + 20] = FenceWear::Broken;
    p.run_until([] { return false; }, 2);
    REQUIRE(keys() == std::vector<std::string> { std::string(kFenceBroken) });
    REQUIRE(p.messages.list.back().place);
    CHECK(p.messages.list.back().place->x == doctest::Approx(3.5f * 4));
    CHECK(p.messages.list.back().place->z == doctest::Approx(2.5f * 4));
    p.run_until([] { return false; }, kFenceWearRates.reminder.value + 2);
    CHECK(keys().back() == kFenceStillBroken);
    // Mended by a mechanic hired now.
    p.park.staff(StaffRole::Mechanic, { 9, 0 });
    CHECK(p.run_until([&] { return keys().back() == kFenceFixed; }, 120));
    CHECK(p.map.wear_at(3, 2) == FenceWear::Sound);
}

TEST_CASE("fence wear: a park loaded with a broken fence is not told of it as new")
{
    MendingPark p;
    p.run_until([] { return false; }, 2);
    CHECK(p.messages.list.empty());
    CHECK(fence_gap(p.map, 0) == save::model::Tile { 3, 2 });
}
