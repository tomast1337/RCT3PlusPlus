// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/challenges.hpp"

#include "core/types.hpp"
#include "simulation/park/state.hpp"
#include "simulation/research.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <doctest/doctest.h>
#include <optional>
#include <span>

using namespace rct3;
using save::model::ChallengeState;
using save::model::ConditionKind;

namespace
{
    constexpr f32 kDay = simulation::kChallengeDay;

    /// Two challenges: a sales one offered after 3 days, and one on dung that waits for the first level to be won.
    save::model::ObjectiveSet sample()
    {
        save::model::ObjectiveSet set;
        set.objectives = { { .difficulty = 0 } };
        set.challenges = {
            { .challenge_id = 0, .prize_cash = 50000, .prize_publicity = 2, .prize_research_boost = 20000, .time_limit = 30, .offer_in = 3 * kDay, .postponed_for = 10 * kDay },
            { .challenge_id = 1, .time_limit = 10, .start_condition = 1, .offer_in = kDay },
        };
        set.conditions = {
            { .kind = ConditionKind::ItemsBought, .value = 2, .items = save::model::ItemsTarget { .counts = save::model::ItemsTarget::Counts::AtStall, .stall = "MadeUpStall" }, .challenge = 0 },
            { .kind = ConditionKind::Dung, .value = 0, .greater_than = false, .challenge = 1 },
        };
        return set;
    }
} // namespace

TEST_CASE("a challenge is offered after its wait, one at a time, and once its level is won")
{
    save::model::ObjectiveSet set = sample();
    simulation::park::ParkMessages messages;
    const simulation::ParkFigures figures;
    CHECK_FALSE(simulation::step_challenges(set, figures, 2 * kDay, messages));
    CHECK_FALSE(simulation::challenge_offered(set));
    CHECK(set.challenges[1].offer_in == kDay); // Its level is not won yet
    (void)simulation::step_challenges(set, figures, kDay, messages);
    CHECK(simulation::challenge_offered(set) == 0);
    set.objectives[0].success = true;
    (void)simulation::step_challenges(set, figures, kDay, messages);
    CHECK(set.challenges[1].state == ChallengeState::Waiting); // The first is still waiting on the player
    CHECK(set.challenges[1].offer_in == 0.0f);
    simulation::answer_challenge(set, 0, simulation::ChallengeAnswer::Decline);
    (void)simulation::step_challenges(set, figures, kDay, messages);
    CHECK(simulation::challenge_offered(set) == 1);
}

TEST_CASE("a challenge may be put off once, and comes back after its wait")
{
    save::model::ObjectiveSet set = sample();
    simulation::park::ParkMessages messages;
    (void)simulation::step_challenges(set, { }, 3 * kDay, messages);
    REQUIRE(simulation::challenge_offered(set) == 0);
    CHECK(simulation::may_postpone(set.challenges[0]));
    simulation::answer_challenge(set, 0, simulation::ChallengeAnswer::Postpone);
    CHECK(set.challenges[0].state == ChallengeState::Postponed);
    (void)simulation::step_challenges(set, { }, 9 * kDay, messages);
    CHECK_FALSE(simulation::challenge_offered(set));
    (void)simulation::step_challenges(set, { }, 2 * kDay, messages);
    CHECK(simulation::challenge_offered(set) == 0);
    CHECK_FALSE(simulation::may_postpone(set.challenges[0]));
    simulation::answer_challenge(set, 0, simulation::ChallengeAnswer::Postpone); // Not again
    CHECK(set.challenges[0].state == ChallengeState::Offered);
}

TEST_CASE("a challenge taken on counts the sales it asks for, and once met gives its prizes")
{
    save::model::ObjectiveSet set = sample();
    set.conditions[0].items->bought = 7; // Counted before it was taken on
    simulation::park::ParkMessages messages;
    (void)simulation::step_challenges(set, { }, 3 * kDay, messages);
    simulation::answer_challenge(set, 0, simulation::ChallengeAnswer::Accept);
    CHECK(set.challenges[0].state == ChallengeState::Running);
    CHECK(set.challenges[0].time_remaining == doctest::Approx(30 * kDay));
    CHECK(set.conditions[0].items->bought == 0);
    simulation::count_sale(set, "madeupstall:tag", "Burger:cid");
    simulation::count_sale(set, "OtherStall", "Burger:cid"); // Elsewhere
    CHECK_FALSE(simulation::step_challenges(set, { }, kDay, messages));
    simulation::count_sale(set, "MadeUpStall", "");
    CHECK(simulation::step_challenges(set, { }, kDay, messages) == 0);
    CHECK(set.challenges[0].state == ChallengeState::Done);

    simulation::park::ParkState park;
    park.cash = units::Money { 0 };
    park.time = 1000;
    save::model::Finance finance;
    std::optional<i64> kept;
    simulation::Research research;
    simulation::give_challenge_prizes(park, finance, kept, research, set, set.challenges[0]);
    CHECK(park.cash == units::Money { 50000 });
    CHECK(research.boost == units::Money { 20000 });
    CHECK(set.publicity_until == doctest::Approx(1000 + 2 * simulation::kTuning.prizes.publicity.value));
}

TEST_CASE("a challenge not met in time fails, with a week's warning first")
{
    save::model::ObjectiveSet set = sample();
    set.challenges[0].time_limit = 10;
    simulation::park::ParkMessages messages;
    (void)simulation::step_challenges(set, { }, 3 * kDay, messages);
    simulation::answer_challenge(set, 0, simulation::ChallengeAnswer::Accept);
    (void)simulation::step_challenges(set, { }, 2 * kDay, messages);
    CHECK(messages.list.empty());
    (void)simulation::step_challenges(set, { }, 2 * kDay, messages);
    REQUIRE(messages.list.size() == 1);
    CHECK(messages.list.back().text_key == "Challenge_OneWeekRemaining");
    CHECK_FALSE(simulation::step_challenges(set, { }, 6 * kDay, messages));
    CHECK(set.challenges[0].state == ChallengeState::Failed);
    CHECK(messages.list.back().text_key == "Challenge_DefaultFailureText");
}

TEST_CASE("a challenge with a goal the simulation cannot judge is never offered")
{
    save::model::ObjectiveSet set = sample();
    set.conditions[0].kind = ConditionKind::EscapedAnimals;
    simulation::park::ParkMessages messages;
    (void)simulation::step_challenges(set, { }, 10 * kDay, messages);
    CHECK_FALSE(simulation::challenge_offered(set));
}

TEST_CASE("a themed-attraction goal counts the attractions of its theme")
{
    const save::model::Condition goal { .kind = ConditionKind::ThemedAttractions, .value = 2, .theme = 3, .challenge = 0 };
    const std::array<i32, 4> themed { 5, 0, 0, 2 };
    CHECK(simulation::read_goal(goal, { .themed = themed }).met);
    CHECK(simulation::read_goal(goal, { .themed = std::span(themed).first(3) }).now == 0); // None counted
}
