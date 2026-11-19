// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/guest_edits.hpp"

#include "core/types.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("each guest level is named, and found by its name")
{
    for (usize k = 0; k < kGuestLevels; ++k)
        CHECK(level_named(level_name(static_cast<GuestLevel>(k))) == static_cast<GuestLevel>(k));
    CHECK_FALSE(level_named("courage"));
}

TEST_CASE("a level set by hand is kept within 0..1")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto guest = park.guest({ 0, 0 });
    auto& r = park.world().registry;

    CHECK(set_level(r, guest, GuestLevel::Thirst, { 0.7f }));
    CHECK(r.get<Needs>(guest).thirst.value == doctest::Approx(0.7f));
    CHECK(set_level(r, guest, GuestLevel::Happiness, { 3 }));
    CHECK(level_of(r, guest, GuestLevel::Happiness)->value == doctest::Approx(1));
    CHECK(set_level(r, guest, GuestLevel::Tolerance, { -1 }));
    CHECK(r.get<Mood>(guest).tolerance.value == doctest::Approx(0));

    // One gone home has nothing to set.
    r.remove<Mood, Needs>(guest);
    CHECK_FALSE(set_level(r, guest, GuestLevel::Happiness, { 0.5f }));
    CHECK_FALSE(level_of(r, guest, GuestLevel::Happiness));
}

TEST_CASE("a held level stays where it was set while the park runs, and drifts once let go")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    // Starving with nothing to eat: their happiness sinks.
    const auto guest = park.guest({ 0, 0 }, { .hunger = 1, .happiness = 0.9f });
    auto& r = park.world().registry;

    hold_level(r, guest, GuestLevel::Happiness, true);
    CHECK(level_held(r, guest, GuestLevel::Happiness));
    CHECK_FALSE(level_held(r, guest, GuestLevel::Energy));
    park.run_seconds(60);
    CHECK(r.get<Mood>(guest).happiness.value == doctest::Approx(0.9f));

    // Set again while held, it is held at the new level.
    CHECK(set_level(r, guest, GuestLevel::Happiness, { 0.6f }));
    park.run_seconds(30);
    CHECK(r.get<Mood>(guest).happiness.value == doctest::Approx(0.6f));

    hold_level(r, guest, GuestLevel::Happiness, false);
    CHECK_FALSE(r.all_of<HeldLevels>(guest));
    park.run_seconds(60);
    CHECK(r.get<Mood>(guest).happiness.value < 0.6f);
}

TEST_CASE("making a guest happy and healthy settles their feelings and needs")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto guest = park.guest({ 0, 0 }, { .thirst = 0.8f, .hunger = 0.9f, .bladder = 0.7f, .happiness = 0.1f, .energy = 0.2f, .nausea = 0.9f });
    auto& r = park.world().registry;

    SUBCASE("cure nausea")
    {
        cure_nausea(r, guest);
        CHECK(r.get<Mood>(guest).nausea.value == 0);
        CHECK(r.get<Needs>(guest).hunger.value == doctest::Approx(0.9f));
    }
    SUBCASE("happy and healthy")
    {
        make_happy_and_healthy(r, guest);
        const Mood& mood = r.get<Mood>(guest);
        const Needs& needs = r.get<Needs>(guest);
        CHECK(mood.happiness.value == 1);
        CHECK(mood.energy.value == 1);
        CHECK(mood.nausea.value == 0);
        CHECK(needs.hunger.value == 0);
        CHECK(needs.thirst.value == 0);
        CHECK(needs.bladder.value == 0);
    }
}

TEST_CASE("a guest sent to a stall walks there")
{
    Sandbox park;
    // The stall one way, and as much path the other, so wandering alone would not take them there.
    park.path({ -9, 0 }, { 9, 0 });
    const auto stall = park.shop("Drinks", Product::Drink, { 9, 0 }, 150);
    const auto guest = park.guest({ 0, 0 });
    auto& r = park.world().registry;

    REQUIRE(send_to(park.world(), guest, stall));
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Heading);
    CHECK(r.get<Errand>(guest).target == stall);
    const glm::vec3 counter = park.world().paths.centre(*r.get<Attraction>(stall).entrance);
    CHECK(park.run_until([&] { return glm::distance(r.get<Pose>(guest).position, counter) < kLaneReach * 2; }, 30));
}

TEST_CASE("a guest sent home walks out through the gate, with their party")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    const auto leader = park.guest({ 6, 0 });
    const auto follower = park.guest({ 6, 0 });
    (void)park.party({ leader, follower });

    // Asking for the follower sends the party, which the leader leads.
    REQUIRE(send_home(park.world(), follower));
    CHECK(r.get<Errand>(leader).kind == ErrandKind::Heading);
    CHECK(park.run_until([&] { return r.all_of<Left>(leader) && r.all_of<Left>(follower); }, 60));
}

TEST_CASE("a guest busy elsewhere cannot be sent")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto stall = park.shop("Drinks", Product::Drink, { 4, 0 }, 150);
    const auto guest = park.guest({ 0, 0 });
    auto& r = park.world().registry;
    r.get<Errand>(guest).kind = ErrandKind::Riding;
    CHECK_FALSE(send_to(park.world(), guest, stall));
    CHECK_FALSE(send_home(park.world(), guest)); // No gate either
}
