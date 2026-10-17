// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "ui/thought_text.hpp"

#include <doctest/doctest.h>
#include <utility>

using namespace rct3::simulation;

namespace
{
    std::size_t times(Sandbox& park, entt::entity guest, ThoughtKind kind)
    {
        const auto* thoughts = park.world().registry.try_get<Thoughts>(guest);
        return thoughts == nullptr ? 0 : static_cast<std::size_t>(std::ranges::count(thoughts->recent, kind, &Thought::kind));
    }
} // namespace

TEST_CASE("each kind of thought is numbered as the language file's texts for it")
{
    using rct3::ui::thought_key;
    CHECK(thought_key(std::to_underlying(ThoughtKind::Tired)) == "Thought_ImTired");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Litter)) == "Thought_ThisPathHasLitter");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Hungry)) == "Thought_ImHungry");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Thirsty)) == "Thought_ImThirsty");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Toilet)) == "Thought_INeedToilet");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Vandalism)) == "Thought_ThisPathIsVandalised");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Vomit)) == "Thought_ThisPathHasSickOnIt");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Sick)) == "Thought_IFeelSick");
    CHECK(thought_key(std::to_underlying(ThoughtKind::VerySick)) == "Thought_IFeelVerySick");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Broke)) == "Thought_IveSpentAllMyMoney");
    CHECK(thought_key(std::to_underlying(ThoughtKind::CantAfford)) == "Thought_ICantAffordThis");
    CHECK(thought_key(std::to_underlying(ThoughtKind::QueueTooLong)) == "Thought_IveBeenQueueingAges");
    CHECK(thought_key(std::to_underlying(ThoughtKind::TooIntense)) == "Thought_IPreferLessIntenseRides");
    CHECK(thought_key(std::to_underlying(ThoughtKind::Enjoyed)) == "Thought_IEnjoyedThisRide");
}

TEST_CASE("a hungry guest thinks so, once a while rather than over and over")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto g = park.guest({ 0, 0 }, { .hunger = 0.9f });
    park.run_seconds(120);
    CHECK(times(park, g, ThoughtKind::Hungry) == 1);
    CHECK(park.world().registry.get<Thoughts>(g).had >= 1);
}

TEST_CASE("riders think of the ride: enjoyed it, and the queue was long")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Twister", { 3, 0 }, 0, { .capacity = 1, .cycle_seconds = 200, .excitement = 0.9f });
    std::vector<entt::entity> guests;
    for (int i = 0; i < 4; ++i)
        guests.push_back(park.guest({ 0, 0 }));
    auto& r = park.world().registry;
    park.run_seconds(900);
    CHECK(std::ranges::any_of(guests, [&](entt::entity g) { return times(park, g, ThoughtKind::Enjoyed) > 0 && r.get<Thoughts>(g).recent.front().about == ride; }));
    CHECK(std::ranges::any_of(guests, [&](entt::entity g) { return times(park, g, ThoughtKind::QueueTooLong) > 0; }));
}

TEST_CASE("a guest who cannot pay thinks so, and keeps no more than six thoughts")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto shop = park.shop("Drinks", Product::Drink, { 3, 0 }, 500);
    const auto g = park.guest({ 0, 0 }, { .thirst = 0.9f, .hunger = 0.9f, .bladder = 0.9f, .energy = 0.1f, .nausea = 0.7f, .cash = 600 });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind == ErrandKind::Heading; }, 10));
    r.get<Attraction>(shop).price = 800; // Dearer by the time they get there
    park.run_seconds(60);
    CHECK(times(park, g, ThoughtKind::CantAfford) == 1);
    CHECK(park.world().registry.get<Thoughts>(g).recent.size() == 6);
}

TEST_CASE("guests go on rides their taste takes in, and say so of one too tame")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto gentle = park.flat_ride("Carousel", { 3, 0 }, 0, { .capacity = 4, .cycle_seconds = 10, .intensity = 0.1f });
    auto& r = park.world().registry;
    const auto thrill_seeker = park.guest({ 0, 0 });
    r.emplace<Taste>(thrill_seeker, 0.4f, 0.9f);
    const auto anyone = park.guest({ 0, 0 });
    park.run_seconds(600);
    CHECK(r.get<Attraction>(gentle).customers > 0);
    CHECK(times(park, thrill_seeker, ThoughtKind::TooTame) == 0); // Never chose it
    CHECK(std::ranges::none_of(r.get<Thoughts>(anyone).recent, [&](const Thought& t) { return t.kind == ThoughtKind::TooTame; }));
    // Brought along anyway, they find it too tame.
    r.get<Errand>(thrill_seeker) = { ErrandKind::Heading, gentle, 0 };
    r.get<Stride>(thrill_seeker).route = { };
    CHECK(park.run_until([&] { return times(park, thrill_seeker, ThoughtKind::TooTame) == 1; }, 600));
}
