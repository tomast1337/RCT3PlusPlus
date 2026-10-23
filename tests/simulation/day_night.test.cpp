// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("after dark a coaster thrills teens more and families less; other rides and daytime are as rated")
{
    CHECK(felt_excitement(0.6f, true, AgeGroup::Teen, true) > 0.6f);
    CHECK(felt_excitement(0.6f, true, AgeGroup::Adult, true) < 0.6f);
    CHECK(felt_excitement(0.6f, true, AgeGroup::Child, true) < 0.6f);
    CHECK(felt_excitement(0.6f, true, AgeGroup::Teen, false) == 0.6f);
    CHECK(felt_excitement(0.6f, false, AgeGroup::Teen, true) == 0.6f);
    CHECK(is_night(22.0f / 24));
    CHECK(is_night(2.0f / 24));
    CHECK_FALSE(is_night(12.0f / 24));
}

TEST_CASE("guests come in at the gate: families by day, mostly teens at night")
{
    for (const float time : { 0.5f, 23.0f / 24 })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        auto& r = park.world().registry;
        r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
        r.ctx().emplace<Daylight>(Daylight { time });
        park.run_seconds(1800);
        const auto& turnstile = r.ctx().get<Turnstile>();
        const std::uint32_t teens = turnstile.in[std::to_underlying(AgeGroup::Teen)];
        const std::uint32_t all = turnstile.in[0] + turnstile.in[1] + turnstile.in[2];
        CHECK(all > 20);
        if (is_night(time))
            CHECK(teens * 2 > all);
        else
            CHECK(teens * 2 < all);
    }
}

TEST_CASE("in the dark a family heads for the gate and goes home, together")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    auto& r = park.world().registry;
    const auto gate = r.create();
    r.emplace<ParkGate>(gate, ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    r.ctx().emplace<Daylight>(Daylight { 22.0f / 24 });
    const auto parent = park.guest({ 5, 0 });
    const auto child = park.guest({ 5, 0 });
    r.get<Visitor>(child).age = AgeGroup::Child;
    park.party({ parent, child });
    CHECK(park.run_until([&] { return r.all_of<Left>(parent); }, 3600));
    CHECK(r.all_of<Left>(child));
    CHECK_FALSE(r.all_of<Errand>(child));
}

TEST_CASE("guests done with the park go home by day: the broke with no cash machine, the miserable; content ones stay")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    const auto content = park.guest({ 5, 0 });
    const auto broke = park.guest({ 5, 0 });
    r.get<Wallet>(broke).cash.value = 0;
    const auto miserable = park.guest({ 5, 0 });
    r.get<Mood>(miserable).happiness.value = 0.05f;
    CHECK(park.run_until([&] { return r.all_of<Left>(broke); }, 120));
    CHECK(park.run_until([&] { return r.all_of<Left>(miserable); }, 1200));
    CHECK_FALSE(r.all_of<Left>(content));
}
