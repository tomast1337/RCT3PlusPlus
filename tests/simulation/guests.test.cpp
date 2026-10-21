// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

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
    // Nine tiles of four units at an adult's walk, give or take the first thought.
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

TEST_CASE("guests stand a moment at a ride's exit to show how it left them, then walk on")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Spinner", { 4, 0 }, 0, { .capacity = 8, .cycle_seconds = 5, .excitement = 0.9f });
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 8; ++i)
    {
        const auto g = park.guest({ 4, 0 });
        r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
        guests.push_back(g);
    }
    REQUIRE(park.run_until([&] { return r.view<Gesture>().size() > 0; }, 30));
    // Loved it: glad gestures only, and those showing it stay put while they do.
    std::vector<std::pair<entt::entity, glm::vec3>> standing;
    for (const auto [g, gesture] : r.view<const Gesture>().each())
    {
        CHECK((gesture.kind == GestureKind::Cheer || gesture.kind == GestureKind::Laugh || gesture.kind == GestureKind::Nod));
        standing.emplace_back(g, r.get<Pose>(g).position);
    }
    park.run_seconds(1);
    for (const auto& [g, at] : standing)
        CHECK(r.get<Pose>(g).position == at);
    park.run_seconds(kTuning.gestures.seconds);
    CHECK(r.view<Gesture>().size() == 0);
}

TEST_CASE("children walk slower than grown-ups, and every step is counted toward the walk")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    const auto adult = park.guest({ 0, 0 });
    auto& r = park.world().registry;
    r.get<Visitor>(adult).age = AgeGroup::Adult;
    const auto child = park.guest({ 0, 0 });
    r.get<Visitor>(child).age = AgeGroup::Child;
    start_walking(park.world(), child, r.get<Stride>(child).from);
    CHECK(r.get<Stride>(child).speed < r.get<Stride>(adult).speed);
    park.run_seconds(5);
    const Pose& pose = r.get<Pose>(adult);
    CHECK(pose.walked > 0);
    CHECK(pose.walked <= 5 * r.get<Stride>(adult).speed + 0.01f);
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
    // The rest, who walked in side by side, end up standing behind in order.
    CHECK(park.run_until([&] { return r.get<Stride>(guests[1]).along < r.get<Stride>(guests[2]).along; }, 5));

    REQUIRE(park.run_until([&] { return r.get<Errand>(guests[0]).kind == ErrandKind::Wandering; }, 30));
    CHECK(r.get<Stride>(guests[0]).from == *park.world().paths.find({ 4, 1, 0 }));
    CHECK(park.run_until([&] { return r.get<Attraction>(ride).customers == 3; }, 120));
}

TEST_CASE("a queue fills two or three abreast, round its turn, and a full one puts newcomers off")
{
    Sandbox park;
    // An L-shaped line: (6, 0) and (5, 0), then round the turn up to (5, 2), where it meets the path.
    park.path({ 0, 3 }, { 5, 3 }).queue({ 5, 0 }, { 5, 2 }).queue({ 6, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 7, 0 }, 100, { .capacity = 1, .cycle_seconds = 600, .line = { { 6, 0 }, { 5, 0 }, { 5, 1 }, { 5, 2 } } });
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 60; ++i)
    {
        const auto g = park.guest({ i % 5, 3 }, { .cash = 1000 });
        r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
        guests.push_back(g);
    }
    park.run_seconds(240);
    const auto& queue = r.get<CycleRide>(ride).queue;
    // Three tiles of line hold far more than single file would, but not everyone.
    CHECK(queue.size() > 25);
    CHECK(queue.size() < 50);
    CHECK(std::ranges::any_of(guests, [&](entt::entity g) {
        const auto* thoughts = r.try_get<Thoughts>(g);
        return thoughts != nullptr && std::ranges::count(thoughts->recent, ThoughtKind::QueueTooLong, &Thought::kind) > 0;
    }));
    const PathNetwork& paths = park.world().paths;
    const auto& line = r.get<CycleRide>(ride).line;
    for (std::size_t i = 0; i < queue.size(); ++i)
    {
        const glm::vec3 at = r.get<Pose>(queue[i]).position;
        // Inside the line's tiles, also at the turn.
        CHECK(std::ranges::any_of(line, [&](NodeId n) {
            const glm::vec3 off = at - paths.centre(n);
            return std::max(std::abs(off.x), std::abs(off.z)) <= paths.tile_size / 2;
        }));
        for (std::size_t j = 0; j < i; ++j)
            CHECK(glm::distance(at, r.get<Pose>(queue[j]).position) > 0.05f);
    }
}

TEST_CASE("guests a save put in a queue stand off its middle, to each side in turn")
{
    Sandbox park;
    park.path({ 0, 1 }, { 2, 1 }).queue({ 3, 1 }, { 5, 1 });
    const auto ride = park.flat_ride("Spinner", { 6, 1 }, 100, { .capacity = 1, .cycle_seconds = 600, .line = { { 5, 1 }, { 4, 1 }, { 3, 1 } } });
    auto& r = park.world().registry;
    auto& cycle = r.get<CycleRide>(ride);
    for (int i = 0; i < 2; ++i)
    {
        const auto g = park.guest({ 3, 1 }, { .cash = 1000 });
        r.get<Errand>(g) = { ErrandKind::Queueing, ride, 0 };
        r.get<Stride>(g).along = 2;
        cycle.queue.push_back(g);
    }
    park.run_seconds(1);
    const float a = r.get<Stride>(cycle.queue[0]).side;
    const float b = r.get<Stride>(cycle.queue[1]).side;
    CHECK(a != 0);
    CHECK(a * b < 0);
}

TEST_CASE("an unhappy guest who has queued very long gives up and walks off")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 }).queue({ 3, 0 }, { 5, 0 });
    // Takes nobody, so the line never moves.
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 100, { .capacity = 0, .cycle_seconds = 10, .line = { { 5, 0 }, { 4, 0 }, { 3, 0 } } });
    auto& r = park.world().registry;
    const auto g = park.guest({ 0, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    REQUIRE(park.run_until([&] { return r.get<CycleRide>(ride).queue.size() == 1; }, 30));
    r.get<Errand>(g).queued = 1000;
    r.get<Mood>(g).happiness = 0.1f;
    CHECK(park.run_until([&] { return r.get<CycleRide>(ride).queue.empty(); }, 600));
    CHECK(std::ranges::count(r.get<Thoughts>(g).recent, ThoughtKind::GaveUpQueue, &Thought::kind) == 1);
    CHECK(r.get<Errand>(g).kind != ErrandKind::Queueing);
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
        auto park = std::make_unique<Sandbox>(rct3::random::Seed { 42 }, threads);
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
    r.emplace<Trains>(ride, Trains { { train }, { { } } });
    const auto g = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    park.run(2);
    CHECK(r.get<Errand>(g).kind == ErrandKind::Riding);
    CHECK(r.get<Trains>(ride).trains[0].running);
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind != ErrandKind::Riding; }, 15));
    CHECK_FALSE(r.get<Trains>(ride).trains[0].running);
}

TEST_CASE("a coaster's trains run on their own, each waiting behind the one in front")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Looper", { 4, 0 }, 100, { .capacity = 1, .cycle_seconds = 1000 });
    auto& r = park.world().registry;
    Coaster track;
    track.distance = { 0, 100 };
    track.height = { 0, 0 };
    track.kinds = { Stretch::Plain, Stretch::Plain };
    track.fixed_speed = 10;
    track.train_length = 10;
    // One standing at the start, one coming round 30 m behind it.
    Coaster waiting = track;
    Coaster coming = track;
    coming.at = 70;
    coming.travelled = 70;
    coming.running = true;
    r.emplace<Trains>(ride, Trains { { waiting, coming }, { { }, { } } });
    const auto& fleet = r.get<Trains>(ride);
    const auto gap = [&] { return std::fmod(fleet.trains[0].at - fleet.trains[1].at + 100.0f, 100.0f) - track.train_length; };
    // Held back behind the standing train, which then goes round empty to make way.
    CHECK(park.run_until(
        [&] {
            CHECK(gap() >= kBlockGap - 1e-3f);
            return fleet.trains[0].running;
        },
        5));
    for (int i = 0; i < 80; ++i)
    {
        park.run(1);
        CHECK(gap() >= kBlockGap - 1e-3f); // Never closer than the gap, round and round
    }
    // A rider boards whichever train stands at the start, and gets off when it comes round.
    const auto g = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind == ErrandKind::Riding; }, 30));
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind != ErrandKind::Riding; }, 30));
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
        // Beside the parent, or a step behind and catching up.
        CHECK((r.get<Stride>(child).to == r.get<Stride>(parent).to || r.get<Stride>(child).to == r.get<Stride>(parent).from));
    }
    CHECK(park.run_until([&] { return r.get<Attraction>(stand).customers == 1; }, 60));
    CHECK(r.get<Needs>(child).thirst < 0.05f);
    CHECK(r.get<Wallet>(child).cash == 900);
    CHECK(r.get<Wallet>(parent).cash == 1000); // Not thirsty, so bought nothing
}

TEST_CASE("a follower far from their leader walks back to them rather than jumping")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto leader = park.guest({ 0, 0 });
    const auto straggler = park.guest({ 8, 0 });
    park.party({ leader, straggler });
    auto& r = park.world().registry;
    const auto in_step = [&] { return r.get<Stride>(straggler).from == r.get<Stride>(leader).from && r.get<Stride>(straggler).to == r.get<Stride>(leader).to; };
    CHECK_FALSE(in_step());
    bool caught = false;
    for (int i = 0; i < 400 && !caught; ++i)
    {
        const glm::vec3 was = r.get<Pose>(straggler).position;
        park.run(1);
        // A hurried walk a tick at most, and a little for the lane changing.
        CHECK(glm::distance(was, r.get<Pose>(straggler).position) < 1.5f);
        caught = in_step();
    }
    CHECK(caught);
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
    // Walked to the tile beside the exit, off the riders' way.
    const NodeId exit = *park.world().paths.nearest({ 12, 0, 0 });
    CHECK(r.get<Stride>(queasy).to != exit);
    CHECK(std::ranges::contains(park.world().paths.nodes()[exit].next, r.get<Stride>(queasy).to));
    CHECK(park.run_until([&] { return r.get<Errand>(queasy).kind == ErrandKind::Following; }, 30));
    CHECK(r.get<Errand>(leader).kind == ErrandKind::Wandering);
    CHECK(r.get<Attraction>(ride).customers == 1);
}

TEST_CASE("a party in line rides on one load, held back for the next rather than split")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 100, { .capacity = 4, .cycle_seconds = 20, .exit = glm::ivec2 { 3, 0 } });
    auto& r = park.world().registry;
    std::vector<entt::entity> strangers;
    for (int i = 0; i < 3; ++i)
        strangers.push_back(park.guest({ 6, 0 }));
    const auto a = park.guest({ 6, 0 });
    const auto b = park.guest({ 6, 0 });
    park.party({ a, b });
    for (const auto g : { strangers[0], strangers[1], strangers[2], a, b })
    {
        r.get<CycleRide>(ride).queue.push_back(g);
        r.get<Errand>(g) = { ErrandKind::Queueing, ride, 0 };
    }
    // Three strangers and a seat left: the party waits, and rides the next load together.
    CHECK(park.run_until([&] { return r.get<Errand>(strangers[0]).kind == ErrandKind::Riding; }, 10));
    CHECK(r.get<Errand>(a).kind == ErrandKind::Queueing);
    CHECK(r.get<Errand>(b).kind == ErrandKind::Queueing);
    CHECK(r.ctx().get<PartyBoarding>().held >= 1);
    CHECK(park.run_until([&] { return r.get<Errand>(a).kind == ErrandKind::Riding; }, 60));
    CHECK(r.get<Errand>(b).kind == ErrandKind::Riding);
}

TEST_CASE("those waiting for a ride stand in a ring beside its exit, and go on together after")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 100, { .capacity = 4, .cycle_seconds = 30, .exit = glm::ivec2 { 3, 0 } });
    const auto leader = park.guest({ 6, 0 });
    std::vector<entt::entity> waiting;
    for (int i = 0; i < 3; ++i)
        waiting.push_back(park.guest({ 6, 0 }, { .nausea = 0.9f }));
    std::vector<entt::entity> all { leader };
    all.insert(all.end(), waiting.begin(), waiting.end());
    park.party(all);
    auto& r = park.world().registry;
    r.get<Errand>(leader) = { ErrandKind::Heading, ride, 0 };
    park.run_seconds(20);
    const NodeId exit = *park.world().paths.nearest({ 12, 0, 0 });
    std::vector<glm::vec3> at;
    for (const auto g : waiting)
    {
        CHECK(r.get<Errand>(g).kind == ErrandKind::Waiting);
        CHECK(r.get<Stride>(g).to != exit);
        at.push_back(r.get<Pose>(g).position);
    }
    for (std::size_t i = 0; i < at.size(); ++i)
        for (std::size_t k = i + 1; k < at.size(); ++k)
            CHECK(glm::distance(at[i], at[k]) > 0.5f); // Apart, not piled up
    // The last one off, all go on together.
    CHECK(park.run_until([&] { return r.get<Errand>(leader).kind == ErrandKind::Wandering; }, 30));
    for (const auto g : waiting)
        CHECK(r.get<Errand>(g).kind == ErrandKind::Following);
}

TEST_CASE("a party stops now and then to stand in a ring facing in, then walks on")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto a = park.guest({ 4, 0 });
    const auto b = park.guest({ 4, 0 });
    const auto c = park.guest({ 4, 0 });
    const auto party = park.party({ a, b, c });
    auto& r = park.world().registry;
    r.get<Party>(party).huddle = ticks(5);
    park.run(6); // Time to step across to their places
    std::vector<glm::vec3> at;
    for (const auto g : { a, b, c })
    {
        CHECK(r.get<Stride>(g).from == r.get<Stride>(g).to); // Standing
        at.push_back(r.get<Pose>(g).position);
    }
    const glm::vec3 centre = (at[0] + at[1] + at[2]) / 3.0f;
    for (std::size_t k = 0; k < 3; ++k)
    {
        CHECK(glm::distance(at[k], centre) == doctest::Approx(0.7f).epsilon(0.05));
        const glm::vec3 in = centre - at[k];
        const float facing = std::atan2(in.x, in.z) - r.get<Pose>(std::array { a, b, c }[k]).heading;
        CHECK(std::cos(facing) > 0.99f); // Facing the middle
    }
    CHECK(park.run_until([&] { return r.get<Stride>(a).from != r.get<Stride>(a).to; }, 15)); // On their way again
}

TEST_CASE("a train leaves when its wait is over and its load is in, or after its longest wait whatever its load")
{
    const Departure half { .wait_for = LoadWait::Half, .min_wait = 10, .max_wait = 60 };
    CHECK_FALSE(departs(half, 8, 8, 5));                                // Full, but not yet waited its least
    CHECK(departs(half, 4, 8, 10));                                     // Half full, waited enough
    CHECK_FALSE(departs(half, 3, 8, 30));                               // Not half full
    CHECK(departs(half, 0, 8, 60));                                     // Its longest wait: leaves empty
    CHECK(departs(half, 3, 7, 10));                                     // Half of 7 seats is 3, the odd seat left over
    CHECK(departs({ .wait_for = LoadWait::Quarter }, 1, 3, 0));         // A quarter of 3 seats is none, so one rider
    CHECK_FALSE(departs({ .min_wait = 20, .max_wait = 10 }, 0, 8, 15)); // Its least wait comes first
    CHECK_FALSE(departs({ }, 0, 8, 1000));                              // Any load waits for someone, with no longest wait
    CHECK(departs({ }, 1, 8, 0));
}

TEST_CASE("a flat ride starts, runs its loops and stops, and its riders get off only once it has stopped")
{
    CycleRide ride { .start_seconds = 2, .run_seconds = 3, .stop_seconds = 1, .loops = 2 };
    CHECK(cycle_seconds(ride) == 9);
    CHECK_FALSE(advance_cycle(ride, 5, false)); // Loading stands still
    CHECK(seconds_into_cycle(ride) == std::nullopt);
    ride.phase = RidePhase::Starting;
    CHECK_FALSE(advance_cycle(ride, 1, false));
    CHECK(ride.phase == RidePhase::Starting);
    CHECK_FALSE(advance_cycle(ride, 2.5f, false)); // Half a second into its first loop
    CHECK(ride.phase == RidePhase::Running);
    CHECK(seconds_into_cycle(ride) == doctest::Approx(3.5));
    CHECK_FALSE(advance_cycle(ride, 3, false)); // Into its second loop
    CHECK(ride.loop == 1);
    CHECK_FALSE(advance_cycle(ride, 1.5f, false)); // Done running, stopping
    CHECK(ride.phase == RidePhase::Stopping);
    CHECK_FALSE(advance_cycle(ride, 5, true)); // Stuck shut: stays stopped
    CHECK(seconds_into_cycle(ride) == doctest::Approx(9));
    CHECK(advance_cycle(ride, 0.1f, false));
    CHECK(ride.phase == RidePhase::Loading);
    CHECK(ride.loop == 0);
}

TEST_CASE("a ride waiting for a full load sends a full train when the queue is long, a part-full one after its longest wait when short")
{
    const auto train_ride = [](Sandbox& park, int guests) {
        park.path({ 0, 0 }, { 4, 0 });
        const auto ride = park.flat_ride("Looper", { 4, 0 }, 100, { .capacity = 4, .cycle_seconds = 1000 });
        auto& r = park.world().registry;
        Coaster train;
        train.distance = { 0, 500 };
        train.height = { 0, 0 };
        train.kinds = { Stretch::Plain, Stretch::Plain };
        train.fixed_speed = 5;
        r.emplace<Trains>(ride, Trains { { train }, { { } } });
        r.emplace<Departure>(ride, Departure { .wait_for = LoadWait::Full, .max_wait = 30 });
        for (int i = 0; i < guests; ++i)
            r.get<Errand>(park.guest({ 4, 0 }, { .cash = 1000 })) = { ErrandKind::Heading, ride, 0 };
        return ride;
    };
    {
        Sandbox park;
        const auto ride = train_ride(park, 6);
        const auto& fleet = park.world().registry.get<Trains>(ride);
        CHECK(park.run_until([&] { return fleet.trains[0].running; }, 5));
        CHECK(fleet.riders[0].size() == 4);
    }
    {
        Sandbox park;
        const auto ride = train_ride(park, 2);
        const auto& fleet = park.world().registry.get<Trains>(ride);
        park.run_seconds(25);
        CHECK_FALSE(fleet.trains[0].running);
        CHECK(fleet.riders[0].size() == 2);
        CHECK(park.run_until([&] { return fleet.trains[0].running; }, 10));
        CHECK(fleet.riders[0].size() == 2);
    }
}

TEST_CASE("a ride reckons its riders an hour from its departures")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Spinner", { 4, 0 }, 100, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    for (int i = 0; i < 12; ++i)
        r.get<Errand>(park.guest({ 4, 0 }, { .cash = 1000 })) = { ErrandKind::Heading, ride, 0 };
    const auto& f = r.get<CycleRide>(ride);
    CHECK(f.per_hour == 0);
    park.run_seconds(45);
    // Two riders every 10 s or so, a little over with the step it takes to load.
    CHECK(f.per_hour == doctest::Approx(720).epsilon(0.1));
}

TEST_CASE("walkers keep an even pace and a steady way through tiles, their party with them")
{
    Sandbox park;
    park.path({ 0, 0 }, { 40, 0 });
    const auto lead = park.guest({ 0, 0 });
    const auto follower = park.guest({ 0, 0 });
    park.party({ lead, follower });
    auto& r = park.world().registry;
    park.run(4);
    for (int t = 0; t < 300; ++t)
    {
        park.run(1);
        for (const auto g : { lead, follower })
        {
            const auto& pose = r.get<Pose>(g);
            const float step = glm::distance(glm::vec2(pose.was.x, pose.was.z), glm::vec2(pose.position.x, pose.position.z));
            CHECK(step > 0.25f); // Never stopping at a tile, nor waiting on the leader
            CHECK(step < 0.4f);
            CHECK(std::abs(std::remainder(pose.heading - pose.was_heading, 2 * glm::pi<float>())) < 0.3f);
        }
    }
}

TEST_CASE("a ride being tested runs empty, and guests are let on only once it is open")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 4, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    r.get<Attraction>(ride).open = false;
    CHECK_FALSE(park.run_until([&] { return r.get<CycleRide>(ride).phase != RidePhase::Loading; }, 30)); // Closed: stands still
    r.get<Attraction>(ride).testing = true;
    CHECK(park.run_until([&] { return r.get<CycleRide>(ride).phase != RidePhase::Loading; }, 5));
    CHECK(r.get<CycleRide>(ride).riders.empty());
}
