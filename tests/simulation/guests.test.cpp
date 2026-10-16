// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("a thirsty guest walks to the drink stand and buys a drink")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    const auto stand = park.shop("Drinks", Product::Drink, { 9, 0 }, 150);
    const auto guest = park.guest({ 0, 0 }, { .thirst = 0.9f, .cash = 1000 });
    auto& r = park.world().registry;

    CHECK(park.run_until([&] { return r.get<Attraction>(stand).customers == 1; }, 120));
    CHECK(r.get<Needs>(guest).thirst < 0.05f);
    CHECK(r.get<Wallet>(guest).cash == 850);
    CHECK(r.get<Attraction>(stand).income == 150);
    // On the stand's tile, wherever on it they walked.
    const glm::vec3 off = r.get<Pose>(guest).position - park.world().paths.centre(r.get<Attraction>(stand).entrance);
    CHECK(std::max(std::abs(off.x), std::abs(off.z)) <= kLaneReach + 0.01f);
    // Nine tiles of four units at 1.3 a second, give or take the first thought.
    CHECK(park.world().tick < ticks(40));
}

TEST_CASE("a guest goes for what they need most")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto drinks = park.shop("Drinks", Product::Drink, { 0, 0 }, 100);
    const auto food = park.shop("Food", Product::Food, { 6, 0 }, 300);
    const auto guest = park.guest({ 3, 0 }, { .thirst = 0.6f, .hunger = 0.95f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Attraction>(food).customers == 1; }, 60));
    CHECK(r.get<Attraction>(drinks).customers == 0);
    CHECK(r.get<Needs>(guest).hunger < 0.05f);
}

TEST_CASE("a guest picks the nearer of two stands")
{
    Sandbox park;
    park.path({ 0, 0 }, { 10, 0 });
    const auto far = park.shop("Far", Product::Drink, { 0, 0 }, 100);
    const auto near = park.shop("Near", Product::Drink, { 10, 0 }, 100);
    park.guest({ 7, 0 }, { .thirst = 0.9f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Attraction>(near).customers == 1; }, 60));
    CHECK(r.get<Attraction>(far).customers == 0);
}

TEST_CASE("a guest who cannot pay does not go, and stays on the paths")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 }).path({ 2, 1 }, { 2, 4 });
    const auto stand = park.shop("Drinks", Product::Drink, { 5, 0 }, 500);
    const auto guest = park.guest({ 0, 0 }, { .thirst = 0.9f, .cash = 100 });
    auto& r = park.world().registry;
    for (int i = 0; i < 600; ++i)
    {
        park.run(1);
        REQUIRE(park.world().paths.nearest(r.get<Pose>(guest).position).has_value());
    }
    CHECK(r.get<Attraction>(stand).customers == 0);
    CHECK(r.get<Wallet>(guest).cash == 100);
}

TEST_CASE("needs grow and a pressing need spoils the mood")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    const auto guest = park.guest({ 0, 0 }, { .thirst = 0.69f, .happiness = 0.5f, .cash = 0 });
    park.run_seconds(120);
    auto& r = park.world().registry;
    CHECK(r.get<Needs>(guest).thirst > 0.9f);
    CHECK(r.get<Needs>(guest).hunger > 0.1f);
    CHECK(r.get<Mood>(guest).happiness < 0.5f);
}

TEST_CASE("a flat ride takes its queue a load at a time and cheers its riders")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Spinner", { 4, 0 }, 200, { .capacity = 2, .cycle_seconds = 20, .excitement = 0.6f });
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 3; ++i)
    {
        const auto g = park.guest({ 4, 0 }, { .happiness = 0.4f, .cash = 1000 });
        // Sent straight to the ride, as if they had chosen it.
        r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
        guests.push_back(g);
    }
    park.run(2);
    const auto& f = r.get<CycleRide>(ride);
    CHECK(f.riders.size() == 2);
    CHECK(f.queue.size() == 1);
    CHECK(r.get<Errand>(guests[0]).kind == ErrandKind::Riding);
    CHECK(r.get<Errand>(guests[2]).kind == ErrandKind::Queueing);

    CHECK(park.run_until([&] { return r.get<Attraction>(ride).customers == 3; }, 30));
    CHECK(r.get<Mood>(guests[0]).happiness > 0.5f);
    CHECK(r.get<Mood>(guests[0]).nausea > 0);
    CHECK(r.get<Errand>(guests[0]).kind == ErrandKind::Wandering);
    CHECK(r.get<Wallet>(guests[0]).cash == 800);
    CHECK(r.get<Attraction>(ride).income == 600);
}

TEST_CASE("guests walk up a ride's queue line in turn, and the ride waits for them to reach the front")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 }).path({ 2, 1 }, { 4, 1 }).queue({ 3, 0 }, { 6, 0 });
    // The line runs from the entrance at (6, 0) back to (3, 0), where it meets the path.
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 100, { .capacity = 1, .cycle_seconds = 10, .line = { { 6, 0 }, { 5, 0 }, { 4, 0 }, { 3, 0 } }, .exit = glm::ivec2 { 4, 1 } });
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 3; ++i)
    {
        const auto g = park.guest({ 0, 0 }, { .cash = 1000 });
        r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
        guests.push_back(g);
    }
    REQUIRE(park.run_until([&] { return r.get<CycleRide>(ride).queue.size() == 3; }, 30));
    CHECK(r.get<CycleRide>(ride).riders.empty());  // Nobody at the front yet
    CHECK(r.get<Pose>(guests[0]).position.x > 12); // In at the far end, (3, 0)

    REQUIRE(park.run_until([&] { return !r.get<CycleRide>(ride).riders.empty(); }, 30));
    CHECK(r.get<CycleRide>(ride).riders.front() == guests[0]);
    // The rest stand behind in order, a quarter tile apart.
    CHECK(r.get<Stride>(guests[1]).along < r.get<Stride>(guests[2]).along);

    REQUIRE(park.run_until([&] { return r.get<Errand>(guests[0]).kind == ErrandKind::Wandering; }, 30));
    CHECK(r.get<Stride>(guests[0]).from == *park.world().paths.find({ 4, 1, 0 }));
    CHECK(park.run_until([&] { return r.get<Attraction>(ride).customers == 3; }, 120));
}

TEST_CASE("bored guests find the flat ride by themselves")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 }).path({ 4, 1 }, { 4, 6 });
    const auto ride = park.flat_ride("Spinner", { 4, 6 }, 100);
    for (int i = 0; i < 10; ++i)
        park.guest({ i % 8, 0 });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Attraction>(ride).customers >= 5; }, 600));
}

TEST_CASE("a park steps the same however many workers share the work")
{
    const auto build = [](unsigned threads) {
        auto park = std::make_unique<Sandbox>(42, threads);
        for (int r = 0; r <= 20; r += 4)
            park->path({ 0, r }, { 30, r });
        for (int c = 0; c <= 30; c += 10)
            park->path({ c, 0 }, { c, 20 });
        park->shop("Drinks", Product::Drink, { 30, 0 }, 120);
        park->shop("Food", Product::Food, { 0, 20 }, 250);
        park->flat_ride("Spinner", { 20, 20 }, 150, { .capacity = 12 });
        for (int i = 0; i < 1000; ++i)
            park->guest({ (i * 7) % 31, ((i / 31) % 6) * 4 }, { .thirst = static_cast<float>(i % 10) / 10, .hunger = static_cast<float>(i % 7) / 7 });
        park->run_seconds(300);
        return park;
    };
    const auto one = build(0);
    const auto many = build(3);
    auto& a = one->world().registry;
    auto& b = many->world().registry;
    for (const auto e : a.view<Pose>())
    {
        CHECK(a.get<Pose>(e).position == b.get<Pose>(e).position);
        CHECK(a.get<Wallet>(e).cash == b.get<Wallet>(e).cash);
        CHECK(a.get<Needs>(e).thirst == b.get<Needs>(e).thirst);
    }
    std::int64_t income = 0;
    for (const auto [e, at] : a.view<Attraction>().each())
    {
        CHECK(at.income == b.get<Attraction>(e).income);
        income += at.income;
    }
    CHECK(income > 0);
}

TEST_CASE("a guest with no drink stand to go to still goes for food")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    const auto food = park.shop("Food", Product::Food, { 5, 0 }, 200);
    park.guest({ 0, 0 }, { .thirst = 1.0f, .hunger = 0.8f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.get<Attraction>(food).customers == 1; }, 60));
}

TEST_CASE("a coaster's riders get off when its train comes round, not after the ride's time")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Looper", { 4, 0 }, 100, { .capacity = 2, .cycle_seconds = 1000 });
    auto& r = park.world().registry;
    Coaster train;
    train.distance = { 0, 50 };
    train.height = { 0, 0 };
    train.kinds = { Stretch::Plain, Stretch::Plain };
    train.fixed_speed = 5; // Round in 10 s
    r.emplace<Coaster>(ride, train);
    const auto g = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    park.run(2);
    CHECK(r.get<Errand>(g).kind == ErrandKind::Riding);
    CHECK(r.get<Coaster>(ride).running);
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind != ErrandKind::Riding; }, 15));
    CHECK_FALSE(r.get<Coaster>(ride).running);
}

TEST_CASE("guests spread over the path but keep on it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 12, 0 });
    const auto guest = park.guest({ 0, 0 });
    auto& r = park.world().registry;
    float widest = 0;
    for (int i = 0; i < 400; ++i)
    {
        park.run(1);
        const float side = std::abs(r.get<Pose>(guest).position.z - park.world().paths.centre(0).z);
        CHECK(side <= kLaneReach + 0.001f);
        widest = std::max(widest, side);
    }
    CHECK(widest > 0.5f); // Not along one line
}

TEST_CASE("between steps a pose is drawn part way, turning the short way round")
{
    const Pose pose { .position = { 4, 0, 0 }, .heading = 3.0f, .was = { 0, 0, 0 }, .was_heading = -3.0f };
    const Pose half = between_steps(pose, 0.5f);
    CHECK(half.position.x == doctest::Approx(2));
    CHECK(std::cos(half.heading) == doctest::Approx(-1).epsilon(0.01)); // Through pi, not through 0
}

TEST_CASE("a party walks together, goes where its neediest wants, and each buys for themselves")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto stand = park.shop("Drinks", Product::Drink, { 8, 0 }, 100);
    const auto parent = park.guest({ 0, 0 }, { .cash = 1000 });
    const auto child = park.guest({ 0, 0 }, { .thirst = 0.9f, .cash = 1000 });
    park.party({ parent, child });
    auto& r = park.world().registry;
    for (int i = 0; i < 20; ++i)
    {
        park.run(1);
        CHECK(r.get<Stride>(child).to == r.get<Stride>(parent).to);
    }
    CHECK(park.run_until([&] { return r.get<Attraction>(stand).customers == 1; }, 60));
    CHECK(r.get<Needs>(child).thirst < 0.05f);
    CHECK(r.get<Wallet>(child).cash == 900);
    CHECK(r.get<Wallet>(parent).cash == 1000); // Not thirsty, so bought nothing
}

TEST_CASE("party members who stay off a ride wait by its exit, and all go on together after")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 100, { .capacity = 4, .cycle_seconds = 20, .exit = glm::ivec2 { 3, 0 } });
    const auto leader = park.guest({ 6, 0 });
    const auto queasy = park.guest({ 6, 0 }, { .nausea = 0.9f }); // Too sick to ride
    park.party({ leader, queasy });
    auto& r = park.world().registry;
    r.get<Errand>(leader) = { ErrandKind::Heading, ride, 0 };
    park.run(2);
    CHECK(r.get<Errand>(leader).kind == ErrandKind::Riding);
    CHECK(r.get<Errand>(queasy).kind == ErrandKind::Waiting);
    park.run_seconds(10);
    CHECK(r.get<Stride>(queasy).to == park.world().paths.nearest({ 12, 0, 0 })); // Walked to the exit tile
    CHECK(park.run_until([&] { return r.get<Errand>(queasy).kind == ErrandKind::Following; }, 30));
    CHECK(r.get<Errand>(leader).kind == ErrandKind::Wandering);
    CHECK(r.get<Attraction>(ride).customers == 1);
}
