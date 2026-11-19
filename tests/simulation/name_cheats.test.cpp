// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/name_cheats.hpp"

#include "core/types.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/holding.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <glm/geometric.hpp>

using namespace rct3::simulation;

TEST_CASE("name cheats: every guest on foot jumps for joy, laughs or retches at once, and no one is really sick")
{
    for (const auto [cheat, kind] : { std::pair { NameCheat::Cheer, GestureKind::Cheer }, std::pair { NameCheat::Laugh, GestureKind::Laugh }, std::pair { NameCheat::Retch, GestureKind::Retch } })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 4, 0 });
        const auto a = park.guest({ 0, 0 });
        const auto b = park.guest({ 3, 0 });
        auto& r = park.world().registry;
        CHECK(set_off(park.world(), cheat) == 2);
        for (const auto g : { a, b })
        {
            REQUIRE(r.all_of<Gesture>(g));
            CHECK(r.get<Gesture>(g).kind == kind);
        }
        CHECK(r.view<const Litter>().empty());
    }
}

TEST_CASE("name cheats: with rides unbreakable a ride worn right down runs on, never breaking, and is as good as new")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 4, .cycle_seconds = 10 });
    for (i32 i = 0; i < 12; ++i)
        park.guest({ i % 4, 0 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .reliability = { 0 }, .inspect_every = std::nullopt });
    set_off(park.world(), NameCheat::Unbreakable);
    park.run_seconds(600);
    CHECK(r.get<Attraction>(ride).customers > 4);
    CHECK(r.get<Upkeep>(ride).breakdowns == 0);
    CHECK(r.get<Upkeep>(ride).reliability.value == 1);
}

TEST_CASE("name cheats: guests try any ride off track, however wild for them; the coasters' cheat leaves those alone")
{
    for (const auto cheat : { NameCheat::RideAllFlat, NameCheat::RideAllTracked })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        const auto ride = park.flat_ride("Top Spin", { 3, 0 }, 0, { .intensity = 0.9f });
        const auto g = park.guest({ 0, 0 });
        park.world().registry.get<Mood>(g).tolerance.value = 0.1f;
        set_off(park.world(), cheat);
        park.run_seconds(300);
        CHECK((park.world().registry.get<Attraction>(ride).customers > 0) == (cheat == NameCheat::RideAllFlat));
    }
}

TEST_CASE("name cheats: a big blast knocks over a guest out of an ordinary one's reach")
{
    for (const bool big : { false, true })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 8, 0 });
        const auto ride = park.flat_ride("Looper", { 8, 0 }, 100, { .capacity = 2, .cycle_seconds = 1000 });
        auto& r = park.world().registry;
        const glm::vec3 from = park.world().paths.centre(*r.get<Attraction>(ride).entrance);
        Coaster train;
        train.distance = { 0, 20 };
        train.height = { 0, 0 };
        train.points = { from, from + glm::vec3(20, 0, 0) };
        train.kinds = { Stretch::Plain, Stretch::Plain };
        train.fixed_speed = 5;
        train.train_length = 4;
        train.cars = 2;
        train.closed = false;
        train.start = train.at = 4;
        r.emplace<Trains>(ride, Trains { { train }, { { } } });
        r.get<Attraction>(ride).testing = true;
        const auto near = park.guest({ 0, 0 }, { .nausea = 0.7f }); // Too queasy to queue, so on foot
        if (big)
            set_off(park.world(), NameCheat::BigBlasts);
        REQUIRE(park.run_until([&] { return r.all_of<Wrecks>(ride); }, 30));
        // Twice an ordinary blast's reach away from the wreck till it blows up.
        Wreck& wreck = r.get<Wrecks>(ride).trains[0][0];
        for (i32 i = 0; i < 200 && wreck.state == WreckState::Flying; ++i)
        {
            wreck.at.x = r.get<Pose>(near).position.x + 2 * kTuning.crashes.blast.value;
            wreck.at.z = r.get<Pose>(near).position.z;
            park.run(1);
        }
        CHECK(wreck.state == WreckState::Burning);
        CHECK(r.all_of<Dropped>(near) == big);
    }
}

TEST_CASE("name cheats: guests in a hurry walk faster for a while, then at their own pace again")
{
    const auto walked = [](bool hurry) {
        Sandbox park;
        park.path({ 0, 0 }, { 30, 0 });
        const auto g = park.guest({ 0, 0 });
        auto& r = park.world().registry;
        if (hurry)
            set_off(park.world(), NameCheat::Hurry);
        f32 total = 0;
        for (i32 i = 0; i < 40; ++i)
        {
            const glm::vec3 was = r.get<Pose>(g).position;
            park.run(1);
            total += glm::distance(was, r.get<Pose>(g).position);
        }
        return std::pair { total, park.world().cheats.hurry.value };
    };
    const auto [calm, none] = walked(false);
    const auto [rushed, left] = walked(true);
    CHECK(rushed > calm * 1.5f);
    CHECK(none == 0);
    CHECK(left == doctest::Approx(kHurry.value - 40 * kTickSeconds));
    CHECK(current_guest_slowdown(SimWorld { }) == 1);
}
