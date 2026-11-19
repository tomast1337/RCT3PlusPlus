// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/enjoyment.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
    CHECK(fit(0.75f, 0.3f, 0.6f, 0.1f) == Fit::Far);
    CHECK(fit(0.95f, 0.3f, 0.6f, 0.1f) == Fit::WayOff);
}

TEST_CASE("a ride too tame for a guest is borne further off their range than one too wild")
{
    CHECK(fit(0.15f, 0.3f, 0.6f, 0.1f) == Fit::Near);
    CHECK(fit(0.75f, 0.3f, 0.6f, 0.1f) == Fit::Far);
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
    SUBCASE("nor does one it only cheered up")
    {
        Mood glum { .happiness = { 0.3f } };
        Favourite none { };
        CHECK_FALSE(take_ride(glum, none, second, 0, suiting(), 125));
        CHECK(glum.happiness.value > kTuning.enjoyment.content.value);
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
    const f32 plain = ride_appeal(fair_ride(), kMiddling, mood);
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

TEST_CASE("an untested ride appeals just enough for a few, far less than the same ride tested")
{
    const Mood mood { .happiness = { 0.5f } };
    RideInMind untested = fair_ride();
    untested.untested = true;
    CHECK(ride_appeal(untested, kMiddling, mood) >= kTuning.choosing.bar);
    CHECK(ride_appeal(untested, kMiddling, mood) * 2 < ride_appeal(fair_ride(), kMiddling, mood));
}

TEST_CASE("of two rides alike, guests mostly take the tested one")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    // Both entered from the same path tile.
    const RideSetup alike { .capacity = 16, .cycle_seconds = 20, .excitement = 0.6f, .intensity = 0.45f, .nausea = 0.2f };
    const auto tested = park.flat_ride("Tested", { 4, 0 }, 0, alike);
    const auto untested = park.flat_ride("Untested", { 4, 0 }, 0, alike);
    auto& r = park.world().registry;
    r.get<CycleRide>(untested).untested = true;
    for (i32 i = 0; i < 12; ++i)
        park.guest({ 0, 0 }, { .cash = 1000 });
    park.run_seconds(900);
    CHECK(r.get<Attraction>(untested).customers > 0);
    CHECK(r.get<Attraction>(untested).customers * 2 < r.get<Attraction>(tested).customers);
}

TEST_CASE("a guest picks a ride the likelier the more they want it, the nearer it is and the shorter its queue")
{
    CHECK(ride_pull(0.6f, 1, 0) > ride_pull(0.3f, 1, 0));
    CHECK(ride_pull(0.6f, 1, 0) > ride_pull(0.6f, 0.5f, 0));
    CHECK(ride_pull(0.6f, 1, 0) > ride_pull(0.6f, 1, 5));
    CHECK(ride_pull(0.6f, 1, 40) > 0);
    CHECK(ride_pull(0.6f, 1, static_cast<usize>(kTuning.choosing.queue_halves)) == doctest::Approx(ride_pull(0.6f, 1, 0) / 2));
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

TEST_CASE("guests come with every strength of stomach, the middling ones most")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    auto dice = rct3::random::seeded(rct3::random::Seed { 7 });
    const std::array<Newcomer, 1> one { { { .age = AgeGroup::Teen } } };
    std::array<i32, 4> by_level { };
    for (i32 i = 0; i < 2000; ++i)
        for (const entt::entity g : admit_guests(park.world(), dice, park.world().paths.find({ 0, 0, 0 }).value(), one, kTuning.parties.middle))
            ++by_level[static_cast<usize>(std::lround(park.world().registry.get<Mood>(g).tolerance.value / kTuning.sickness.tolerance_per_saved_level.value)) - 1];
    CHECK(by_level[0] > 0);
    CHECK(by_level[2] > by_level[1]);
    CHECK(by_level[2] > by_level[3]);
    CHECK(by_level[0] < by_level[1]);
}

TEST_CASE("guests come some needing the toilet, liking pools anywhere from least to most, and wishing for a swim however much")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    auto dice = rct3::random::seeded(rct3::random::Seed { 7 });
    const std::array<Newcomer, 1> one { { { .age = AgeGroup::Adult } } };
    i32 needing = 0;
    i32 least = 0;
    i32 most = 0;
    f32 swim_liking_most = 0; // Wish to swim of those liking pools most, summed, against those liking them least
    f32 swim_liking_least = 0;
    constexpr i32 kGuests = 2000;
    auto& r = park.world().registry;
    for (i32 i = 0; i < kGuests; ++i)
        for (const entt::entity g : admit_guests(park.world(), dice, park.world().paths.find({ 0, 0, 0 }).value(), one, kTuning.parties.middle))
        {
            const Needs& needs = r.get<Needs>(g);
            const f32 liking = r.get<Taste>(g).pools.value;
            CHECK(needs.bladder.value <= kTuning.arrivals.bladder.value);
            needing += needs.bladder.value > 0;
            if (liking == kTuning.pools.least_liking.value)
            {
                ++least;
                swim_liking_least += needs.swim.value;
            }
            if (liking == kTuning.pools.most_liking.value)
            {
                ++most;
                swim_liking_most += needs.swim.value;
            }
        }
    CHECK(needing > kGuests * 9 / 10);
    CHECK(least > kGuests / 20);
    CHECK(least < kGuests / 5);
    CHECK(most > kGuests / 20);
    CHECK(most < kGuests / 5);
    CHECK(swim_liking_least / static_cast<f32>(least) == doctest::Approx(swim_liking_most / static_cast<f32>(most)).epsilon(0.25));
}

TEST_CASE("a ride that delights a guest leaves them liking wilder ones")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    park.flat_ride("Carousel", { 3, 0 }, 0, { .capacity = 4, .cycle_seconds = 10, .excitement = 0.8f, .intensity = 0.3f, .nausea = 0.1f });
    const auto guest = park.guest({ 0, 0 }, { .happiness = 0.9f });
    auto& r = park.world().registry;
    r.emplace_or_replace<Taste>(guest, Taste { .least = { 0.2f }, .most = { 0.5f } });
    REQUIRE(park.run_until([&] { return r.all_of<Ridden>(guest); }, 600));
    CHECK(r.get<Taste>(guest).most.value > 0.5f);

    SUBCASE("however bold they came, up to the boldest any guest gets")
    {
        const auto bold = park.guest({ 0, 0 }, { .happiness = 0.9f });
        r.emplace_or_replace<Taste>(bold, Taste { .least = { 0.2f }, .most = { 1.2f } });
        const auto boldest = park.guest({ 0, 0 }, { .happiness = 0.9f });
        r.emplace_or_replace<Taste>(boldest, Taste { .least = { 0.2f }, .most = kTuning.enjoyment.boldest });
        REQUIRE(park.run_until([&] { return r.all_of<Ridden>(bold) && r.all_of<Ridden>(boldest); }, 600));
        CHECK(r.get<Taste>(bold).most.value > 1.2f);
        CHECK(r.get<Taste>(boldest).most.value == kTuning.enjoyment.boldest.value);
    }
}

TEST_CASE("only a delighted, settled guest thinks they enjoyed a thrilling ride well within what they take")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Carousel", { 3, 0 }, 0, { .capacity = 4, .cycle_seconds = 10, .excitement = 0.8f, .intensity = 0.3f, .nausea = 0.1f });
    auto& r = park.world().registry;
    const auto enjoyed = [&](entt::entity g) {
        const auto* had = r.try_get<Thoughts>(g);
        return had != nullptr && std::ranges::any_of(had->recent, [&](const Thought& t) { return t.kind == ThoughtKind::Enjoyed && t.about == ride; });
    };
    const auto delighted = park.guest({ 0, 0 }, { .happiness = 0.95f });
    r.emplace_or_replace<Taste>(delighted, Taste { .least = { 0.2f }, .most = { 0.7f } });
    // As wild as the most they like: it was enough for them.
    const auto at_their_limit = park.guest({ 0, 0 }, { .happiness = 0.95f });
    r.emplace_or_replace<Taste>(at_their_limit, Taste { .least = { 0.2f }, .most = { 0.3f } });
    const auto queasy = park.guest({ 0, 0 }, { .happiness = 0.95f, .nausea = 0.5f });
    r.emplace_or_replace<Taste>(queasy, Taste { .least = { 0.2f }, .most = { 0.7f } });
    REQUIRE(park.run_until([&] { return r.all_of<Ridden>(delighted) && r.all_of<Ridden>(at_their_limit) && r.all_of<Ridden>(queasy); }, 600));
    CHECK(enjoyed(delighted));
    CHECK_FALSE(enjoyed(at_their_limit));
    CHECK_FALSE(enjoyed(queasy));
}
