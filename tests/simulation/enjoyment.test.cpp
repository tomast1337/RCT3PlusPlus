// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/enjoyment.hpp"

#include "simulation/sandbox.hpp"
#include "simulation/tuning.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;
using rct3::units::Money;
using rct3::units::Seconds;
using rct3::units::Share;

namespace
{
    // A guest who likes middling rides and has a middling stomach.
    constexpr Taste kMiddling { .least = { 0.3f }, .most = { 0.6f } };

    RideTaken suiting()
    {
        return { .excitement = { 0.6f }, .intensity = { 0.45f }, .nausea = { 0.2f }, .price = Money { 100 }, .worth = Money { 300 }, .waited = Seconds { 0 }, .new_to_them = true };
    }
} // namespace

TEST_CASE("a rating fits a range within it, near it, far off it or way off")
{
    CHECK(fit(0.5f, 0.3f, 0.6f, 0.1f) == Fit::Within);
    CHECK(fit(0.65f, 0.3f, 0.6f, 0.1f) == Fit::Near);
    CHECK(fit(0.15f, 0.3f, 0.6f, 0.1f) == Fit::Far);
    CHECK(fit(0.95f, 0.3f, 0.6f, 0.1f) == Fit::WayOff);
}

TEST_CASE("a ride that suits a guest in every way scores best, one that suits them in none worst")
{
    const Mood mood { .happiness = { 0 } }; // Nothing nearly suits them
    CHECK(enjoyment_points(suiting(), kMiddling, mood) == 125);

    RideTaken awful = suiting();
    awful.intensity = Share { 1.0f };
    awful.nausea = Share { 1.0f };
    awful.price = Money { 900 };
    awful.waited = Seconds { 900 };
    awful.new_to_them = false;
    CHECK(enjoyment_points(awful, kMiddling, mood) == -122);

    SUBCASE("each part takes off on its own")
    {
        RideTaken dear = suiting();
        dear.price = Money { 450 };
        CHECK(enjoyment_points(dear, kMiddling, mood) < 125);
        RideTaken slow = suiting();
        slow.waited = Seconds { 240 };
        CHECK(enjoyment_points(slow, kMiddling, mood) < 125);
        RideTaken again = suiting();
        again.new_to_them = false;
        CHECK(enjoyment_points(again, kMiddling, mood) == 110);
    }
}

TEST_CASE("a happy guest takes a ride a little past their range in their stride")
{
    RideTaken wild = suiting();
    wild.intensity = Share { 0.75f };
    const Mood glum { .happiness = { 0.1f } };
    const Mood happy { .happiness = { 1.0f } };
    CHECK(enjoyment_points(wild, kMiddling, happy) > enjoyment_points(wild, kMiddling, glum));
}

TEST_CASE("a weak stomach takes only the mildest rides in comfort")
{
    CHECK(stomach_range(Share { 0.25f }).most.value < stomach_range(Share { 1.0f }).most.value);
    CHECK(stomach_range(Share { 0.25f }).least.value == 0);
    CHECK(stomach_range(Share { 1.0f }).least.value > 0);
}

TEST_CASE("after a ride a guest is happier by its points and its best one is their favourite")
{
    Mood mood { .happiness = { 0.5f } };
    Favourite favourite { };
    const entt::entity first { 1 };
    const entt::entity second { 2 };
    CHECK(take_ride(mood, favourite, first, 7, suiting(), 125));
    CHECK(mood.happiness.value == doctest::Approx(0.5f + 125.0f / 300));
    CHECK(favourite.ride == first);
    CHECK(favourite.saved == 7);
    CHECK(favourite.score == doctest::Approx(favourite_score(Share { 0.6f }, 125)));

    SUBCASE("a worse ride leaves the favourite")
    {
        CHECK_FALSE(take_ride(mood, favourite, second, 0, suiting(), 60));
        CHECK(favourite.ride == first);
    }
    SUBCASE("a better one takes its place")
    {
        RideTaken thrilling = suiting();
        thrilling.excitement = Share { 0.9f };
        CHECK(take_ride(mood, favourite, second, 0, thrilling, 125));
        CHECK(favourite.ride == second);
    }
    SUBCASE("a glum guest takes no ride for their favourite")
    {
        Mood glum { .happiness = { 0.1f } };
        Favourite none { };
        CHECK_FALSE(take_ride(glum, none, second, 0, suiting(), 20));
        CHECK(none.ride == entt::null);
    }
}

TEST_CASE("a ride sickens a guest it left happy less than one it left glum")
{
    RideTaken sickening = suiting();
    sickening.nausea = Share { 0.8f };
    Mood happy { .happiness = { 0.6f } };
    Mood glum { .happiness = { 0.6f } };
    Favourite favourite { };
    take_ride(happy, favourite, entt::entity { 1 }, 0, sickening, 100);
    take_ride(glum, favourite, entt::entity { 1 }, 0, sickening, -100);
    CHECK(happy.nausea.value < glum.nausea.value);
    CHECK(happy.nausea.value > 0);
}

TEST_CASE("a guest off a ride in the park has it for their favourite")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Carousel", { 3, 0 }, 0, { .capacity = 4, .cycle_seconds = 10, .intensity = 0.3f, .nausea = 0.1f });
    const auto guest = park.guest({ 0, 0 }, { .happiness = 0.6f });
    auto& r = park.world().registry;
    REQUIRE(park.run_until([&] { return r.all_of<Favourite>(guest) && r.get<Favourite>(guest).ride == ride; }, 600));
    CHECK(r.get<Ridden>(guest).rides.size() == 1);
}

namespace
{
    RideInMind fair_ride()
    {
        return { .excitement = { 0.6f }, .intensity = { 0.45f }, .nausea = { 0.2f }, .price = Money { 100 }, .worth = Money { 300 } };
    }
} // namespace

TEST_CASE("a guest wants a ride the more for its thrills, a fair price and suiting them")
{
    const Mood mood { .happiness = { 0.5f } };
    const float plain = ride_appeal(fair_ride(), kMiddling, mood);
    CHECK(plain >= kTuning.choosing.bar);

    RideInMind dull = fair_ride();
    dull.excitement = Share { 0 };
    RideInMind dear = fair_ride();
    dear.price = Money { 600 };
    RideInMind wild = fair_ride();
    wild.intensity = Share { 1 };
    RideInMind sickening = fair_ride();
    sickening.nausea = Share { 1 };
    RideInMind favourite = fair_ride();
    favourite.favourite = true;
    RideInMind again = fair_ride();
    again.just_been = true;
    CHECK(ride_appeal(dull, kMiddling, mood) < plain);
    CHECK(ride_appeal(dear, kMiddling, mood) < plain);
    CHECK(ride_appeal(wild, kMiddling, mood) < plain);
    CHECK(ride_appeal(sickening, kMiddling, mood) < plain);
    CHECK(ride_appeal(favourite, kMiddling, mood) > plain);
    CHECK(ride_appeal(again, kMiddling, mood) < plain);

    RideInMind awful = dull;
    awful.price = Money { 900 };
    awful.intensity = Share { 1 };
    CHECK(ride_appeal(awful, kMiddling, mood) < kTuning.choosing.bar);
}

TEST_CASE("a guest walks past a dull ride far dearer than it is worth")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Carousel", { 3, 0 }, 4000, { .capacity = 4, .cycle_seconds = 10, .excitement = 0, .intensity = 0.3f, .nausea = 0.1f });
    park.guest({ 0, 0 }, { .happiness = 0.6f });
    auto& r = park.world().registry;
    CHECK_FALSE(park.run_until([&] { return r.get<Attraction>(ride).customers > 0; }, 300));
}

TEST_CASE("a guest does not think of a ride too far off, and goes on one of those nearby they want")
{
    Sandbox park;
    park.path({ 0, 0 }, { 40, 0 });
    const auto far = park.flat_ride("Carousel", { 40, 0 }, 0, { .capacity = 4, .cycle_seconds = 10, .intensity = 0.3f, .nausea = 0.1f });
    const auto near = park.flat_ride("Carousel", { 3, 0 }, 0, { .capacity = 4, .cycle_seconds = 10, .intensity = 0.3f, .nausea = 0.1f });
    park.guest({ 0, 0 }, { .happiness = 0.6f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Attraction>(near).customers > 0; }, 600));
    CHECK(r.get<Attraction>(far).customers == 0);
}
