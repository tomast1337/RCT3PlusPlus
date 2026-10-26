// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;
namespace dice_rolls = rct3::random;

namespace
{
    /// Of `tries` picks for a guest of `age` with `energy`, the share running.
    double running_share(AgeGroup age, float energy, int tries = 4000)
    {
        auto dice = dice_rolls::seeded(dice_rolls::Seed { 7 });
        const Mood mood { .happiness = { 0.5f }, .energy = { energy } };
        int runs = 0;
        for (int i = 0; i < tries; ++i)
            runs += pick_gait(age, mood, Needs { }, dice) == Gait::Run;
        return static_cast<double>(runs) / tries;
    }

    const Leanings kKeen { .hands = 1, .brace = 1, .eyes = 0 };
    const Leanings kTimid { .hands = 0, .brace = 0, .eyes = 1 };
    const Felt kCruising { .vertical = 1, .lateral = 0, .slope = 0 };
} // namespace

TEST_CASE("children run most, adults seldom, and the tired less than the fresh")
{
    const double child = running_share(AgeGroup::Child, 1);
    const double teen = running_share(AgeGroup::Teen, 1);
    const double adult = running_share(AgeGroup::Adult, 1);
    CHECK(child > teen);
    CHECK(teen > adult);
    CHECK(adult > 0);
    CHECK(adult < 0.1);
    CHECK(running_share(AgeGroup::Child, 0.4f) < child);
}

TEST_CASE("worn out, sick or bursting guests show it in how they walk")
{
    auto dice = dice_rolls::seeded(dice_rolls::Seed { 1 });
    CHECK(pick_gait(AgeGroup::Child, Mood { .energy = { 0.05f } }, Needs { }, dice) == Gait::Weary);
    CHECK(pick_gait(AgeGroup::Child, Mood { .nausea = { 0.95f } }, Needs { }, dice) == Gait::Queasy);
    CHECK(pick_gait(AgeGroup::Child, Mood { }, Needs { .bladder = { 0.95f } }, dice) == Gait::Bursting);
}

TEST_CASE("a guest's pace grows with their energy and with their gait")
{
    const Pace walk { 1.2f };
    CHECK(gait_pace(walk, { 1 }, Gait::Walk).value > gait_pace(walk, { 0 }, Gait::Walk).value);
    CHECK(gait_pace(walk, { 0.5f }, Gait::Run).value > 1.5f * gait_pace(walk, { 0.5f }, Gait::Walk).value);
    CHECK(gait_pace(walk, { 0.5f }, Gait::Weary).value < gait_pace(walk, { 0.5f }, Gait::Walk).value);
    CHECK(gait_pace(walk, { 0.5f }, Gait::Queasy).value < gait_pace(walk, { 0.5f }, Gait::Walk).value);
}

TEST_CASE("the guests walking a park do not all go at one pace")
{
    Sandbox park;
    park.path({ 0, 0 }, { 12, 0 });
    std::vector<entt::entity> guests;
    for (int i = 0; i < 30; ++i)
        guests.push_back(park.guest({ 0, 0 }, { .energy = 0.3f + 0.02f * static_cast<float>(i) }));
    park.run_seconds(30);
    auto& r = park.world().registry;
    float slowest = 100, fastest = 0;
    for (const entt::entity g : guests)
    {
        const auto& stride = r.get<Stride>(g);
        const float pace = gait_pace(stride.speed, r.get<Mood>(g).energy, stride.gait).value;
        slowest = std::min(slowest, pace);
        fastest = std::max(fastest, pace);
    }
    CHECK(fastest > slowest * 1.1f);
}

TEST_CASE("riders wait in the station and up the lift")
{
    auto dice = dice_rolls::seeded(dice_rolls::Seed { 2 });
    CHECK(react(RideReaction::HandsUp, kCruising, false, false, { 1 }, 0, kKeen, true, dice, 1) == RideReaction::Waiting);
    CHECK(react(RideReaction::HandsUp, kCruising, true, true, { 1 }, 0, kKeen, true, dice, 1) == RideReaction::Waiting);
}

TEST_CASE("thrilled riders throw their hands up and keep them up till the forces grow strong")
{
    auto dice = dice_rolls::seeded(dice_rolls::Seed { 3 });
    RideReaction now = RideReaction::Calm;
    for (int i = 0; i < 40 && now != RideReaction::HandsUp; ++i)
        now = react(now, kCruising, true, false, { 1 }, 0, kKeen, false, dice, 0.25f);
    CHECK(now == RideReaction::HandsUp);
    CHECK(react(now, kCruising, true, false, { 0.5f }, 0, kKeen, false, dice, 0.25f) == RideReaction::HandsUp);
    CHECK(react(now, Felt { .vertical = 2.5f, .lateral = 5 }, true, false, { 0.5f }, 0, Leanings { .hands = 1 }, false, dice, 0.25f) == RideReaction::Calm);
}

TEST_CASE("riders who are not thrilled raise their hands only floating over a hill")
{
    auto dice = dice_rolls::seeded(dice_rolls::Seed { 4 });
    int cruising = 0, floating = 0;
    for (int i = 0; i < 400; ++i)
    {
        cruising += react(RideReaction::Calm, kCruising, true, false, { 0.5f }, 0, kKeen, false, dice, 0.25f) == RideReaction::HandsUp;
        floating += react(RideReaction::Calm, Felt { .vertical = 0 }, true, false, { 0.5f }, 0, kKeen, false, dice, 0.25f) == RideReaction::HandsUp;
    }
    CHECK(cruising == 0);
    CHECK(floating > 0);
}

TEST_CASE("timid riders never raise their hands, and the front car has fewer of them")
{
    auto dice = dice_rolls::seeded(dice_rolls::Seed { 5 });
    for (int i = 0; i < 200; ++i)
        CHECK(react(RideReaction::Calm, kCruising, true, false, { 1 }, 0, kTimid, true, dice, 1) != RideReaction::HandsUp);
    const Leanings middling { .hands = 0.4f };
    int front = 0, back = 0;
    for (int i = 0; i < 200; ++i)
    {
        front += react(RideReaction::Calm, kCruising, true, false, { 1 }, 0, middling, true, dice, 1) == RideReaction::HandsUp;
        back += react(RideReaction::Calm, kCruising, true, false, { 1 }, 0, middling, false, dice, 1) == RideReaction::HandsUp;
    }
    CHECK(front > 0);
    CHECK(back == 0);
}

TEST_CASE("riders brace against strong forces and cover their eyes going down a ride too wild for them")
{
    auto dice = dice_rolls::seeded(dice_rolls::Seed { 6 });
    CHECK(react(RideReaction::Calm, Felt { .vertical = 4 }, true, false, { 0.5f }, 0, kKeen, false, dice, 0.25f) == RideReaction::Strained);
    CHECK(react(RideReaction::Calm, Felt { .vertical = 4 }, true, false, { 0.5f }, 0, kTimid, false, dice, 0.25f) != RideReaction::Strained);
    const Leanings shy { .eyes = 0 };
    CHECK(react(RideReaction::Calm, Felt { .slope = -0.8f }, true, false, { 0.5f }, 0.4f, shy, false, dice, 0.25f) == RideReaction::CoverEyes);
    CHECK(react(RideReaction::Calm, Felt { .slope = 0.2f }, true, false, { 0.5f }, 0.4f, shy, false, dice, 0.25f) != RideReaction::CoverEyes);
    CHECK(react(RideReaction::Calm, Felt { .slope = -0.8f }, true, false, { 0.5f }, 0, shy, false, dice, 0.25f) != RideReaction::CoverEyes);
}
