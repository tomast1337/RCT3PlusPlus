// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/challenges.hpp"

#include "core/types.hpp"
#include "simulation/park/finance.hpp"
#include "simulation/park/state.hpp"
#include "simulation/research.hpp"
#include "simulation/tuning.hpp"
#include "strings/ascii.hpp"

#include <algorithm>

namespace rct3::simulation
{
    namespace
    {
        using save::model::Challenge;
        using save::model::ChallengeState;
        using save::model::Condition;
        using save::model::ObjectiveSet;

        /// Whether challenge `c` has goals, and the simulation can judge them all.
        bool offerable(const ObjectiveSet& set, const Challenge& c)
        {
            const auto own = [&](const Condition& g) { return g.challenge == c.challenge_id; };
            // shortcut: a challenge asking for scenery of a kind or escaped animals caught is never offered, as the
            // player cannot let animals out for it; offer it once they can.
            const auto doable = [](save::model::ConditionKind kind) { return judged(kind) && kind != save::model::ConditionKind::EscapedAnimals; };
            return std::ranges::any_of(set.conditions, own) && std::ranges::none_of(set.conditions, [&](const Condition& g) { return own(g) && !doable(g.kind); });
        }

        /// Whether the scenario's level `start_condition` names (1 apprentice to 3 tycoon) has been won; 0 names none.
        bool may_start(const ObjectiveSet& set, const Challenge& c)
        {
            return c.start_condition <= 0 || std::ranges::any_of(set.objectives, [&](const save::model::Objective& o) { return o.difficulty == c.start_condition - 1 && o.objective_id == 0 && o.success; });
        }

        /// The kind part of a symbol, before its tag.
        std::string_view untagged(std::string_view symbol)
        {
            return symbol.substr(0, symbol.find(':'));
        }
    } // namespace

    std::optional<usize> challenge_offered(const ObjectiveSet& set)
    {
        const auto found = std::ranges::find(set.challenges, ChallengeState::Offered, &Challenge::state);
        return found != set.challenges.end() ? std::optional(static_cast<usize>(found - set.challenges.begin())) : std::nullopt;
    }

    bool may_postpone(const Challenge& c) noexcept
    {
        return c.times_postponed == 0;
    }

    void answer_challenge(ObjectiveSet& set, usize which, ChallengeAnswer answer)
    {
        Challenge& c = set.challenges.at(which);
        if (c.state != ChallengeState::Offered)
            return;
        switch (answer)
        {
            case ChallengeAnswer::Accept:
                c.state = ChallengeState::Running;
                c.time_remaining = c.time_limit * kChallengeDay;
                for (Condition& g : set.conditions)
                    if (g.challenge == c.challenge_id && g.items)
                        g.items->bought = 0;
                break;
            case ChallengeAnswer::Decline:
                c.state = ChallengeState::Declined;
                break;
            case ChallengeAnswer::Postpone:
                if (!may_postpone(c))
                    return;
                c.state = ChallengeState::Postponed;
                ++c.times_postponed;
                break;
        }
    }

    std::optional<usize> step_challenges(ObjectiveSet& set, const ParkFigures& figures, f32 elapsed, park::ParkMessages& messages)
    {
        bool busy = std::ranges::any_of(set.challenges, [](const Challenge& c) { return c.state == ChallengeState::Offered || c.state == ChallengeState::Running; });
        for (usize i = 0; i < set.challenges.size(); ++i)
        {
            Challenge& c = set.challenges[i];
            switch (c.state)
            {
                case ChallengeState::Waiting:
                case ChallengeState::Postponed:
                {
                    if (!may_start(set, c) || !offerable(set, c))
                        break;
                    f32& wait = c.state == ChallengeState::Waiting ? c.offer_in : c.postponed_for;
                    wait = std::max(0.0f, wait - elapsed);
                    if (wait <= 0 && !busy)
                    {
                        c.state = ChallengeState::Offered;
                        busy = true;
                    }
                    break;
                }
                case ChallengeState::Running:
                {
                    const bool met = std::ranges::none_of(set.conditions, [&](const Condition& g) { return g.challenge == c.challenge_id && !read_goal(g, figures).met; });
                    if (met)
                    {
                        c.state = ChallengeState::Done;
                        return i;
                    }
                    const f32 before = c.time_remaining;
                    c.time_remaining -= elapsed;
                    if (c.time_remaining <= 0)
                    {
                        c.state = ChallengeState::Failed;
                        park::post(messages, { .text_key = "Challenge_DefaultFailureText" });
                    }
                    else if (before > save::model::GameClock::kUnitsPerWeek && c.time_remaining <= save::model::GameClock::kUnitsPerWeek)
                        park::post(messages, { .text_key = "Challenge_OneWeekRemaining" });
                    break;
                }
                default:
                    break;
            }
        }
        return std::nullopt;
    }

    void count_sale(ObjectiveSet& set, std::string_view stall, std::string_view item)
    {
        using Counts = save::model::ItemsTarget::Counts;
        const auto running = std::ranges::find(set.challenges, ChallengeState::Running, &Challenge::state);
        if (running == set.challenges.end())
            return;
        for (Condition& g : set.conditions)
        {
            if (g.challenge != running->challenge_id || !g.items)
                continue;
            const bool item_matches = !item.empty() && strings::equals_ignoring_case(g.items->item, item);
            const bool stall_matches = strings::equals_ignoring_case(untagged(g.items->stall), untagged(stall));
            if (g.items->counts == Counts::Item ? item_matches : g.items->counts == Counts::AtStall ? stall_matches : item_matches && stall_matches)
                ++g.items->bought;
        }
    }

    void give_challenge_prizes(park::ParkState& park, save::model::Finance& finance, std::optional<i64>& kept, Research& research, ObjectiveSet& set, const Challenge& c)
    {
        if (c.prize_cash > 0)
            park::book(park, finance, kept, units::Money { -c.prize_cash }, park::LedgerLine::PrizeMoney, park::Charge::Always);
        research.boost.value += std::max(c.prize_research_boost, 0);
        if (c.prize_publicity > 0)
            set.publicity_until = std::max(set.publicity_until, park.time) + static_cast<f32>(c.prize_publicity) * kTuning.prizes.publicity.value;
    }

} // namespace rct3::simulation
