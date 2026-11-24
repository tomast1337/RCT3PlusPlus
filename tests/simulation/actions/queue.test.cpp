// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/queue.hpp"

#include "core/types.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::actions;

namespace
{
    /// A park with cash to spare, a loan and room to borrow more.
    struct Park
    {
        simulation::SimWorld sim { };
        simulation::park::ParkState state { .cash = units::Money { 500'000 }, .loan = units::Money { 300'000 } };
        save::model::ParkModel model { .finance = { .max_loan = 1'000'000 } };
        save::model::RideModel rides { };
        simulation::Research research { };
        std::optional<std::vector<simulation::ResearchItem>> research_items { };
        std::optional<i64> ledger_month { };
        simulation::DayClock day_clock { };
        rct3::park::CarriedKinds carried { };
        std::vector<save::model::StallIndex> unjoined_stalls { };
        std::vector<rct3::park::track::TrackRules> track_rules { };
        std::vector<std::optional<rct3::park::track::TrackCourse>> courses { };
        rct3::park::pools::PoolMap pools { };
        std::optional<save::model::RideIndex> building { };
        std::vector<save::model::RideIndex> readying { };
        std::vector<save::model::RideShow> shows { };
        World world { .sim = sim, .park = state, .model = model, .rides = rides, .research = research, .research_items = research_items, .ledger_month = ledger_month, .day_clock = day_clock, .carried = carried, .unjoined_stalls = unjoined_stalls, .track_rules = track_rules, .courses = courses, .pools = pools, .building = building, .readying = readying, .shows = shows };
    };
} // namespace

TEST_CASE("an action queued for a tick applies at the start of that tick, not before")
{
    Park p;
    ActionQueue queue;
    p.sim.tick = 3;
    queue.waiting.push_back({ .tick = 5, .action = LoanStep { .move = simulation::park::LoanMove::Borrow } });
    apply_due(queue, p.world);
    CHECK(p.state.loan == units::Money { 300'000 });
    p.sim.tick = 4;
    apply_due(queue, p.world);
    CHECK(p.state.loan == units::Money { 300'000 });
    p.sim.tick = 5;
    apply_due(queue, p.world);
    CHECK(p.state.loan == units::Money { 400'000 });
    CHECK(*p.state.cash == units::Money { 600'000 });
    CHECK(queue.waiting.empty());
}

TEST_CASE("actions asked while paused apply though no tick runs, in the order asked")
{
    Park p;
    ActionQueue queue;
    p.sim.tick = 7;
    ask(queue, p.sim, SetLoan { .to = units::Money { 0 } });
    ask(queue, p.sim, LoanStep { .move = simulation::park::LoanMove::Borrow });
    // A paused turn: the tick stays where it is, and the actions apply all the same.
    apply_due(queue, p.world);
    CHECK(p.sim.tick == 7);
    CHECK(p.state.loan == units::Money { 100'000 });
    CHECK(*p.state.cash == units::Money { 300'000 });
    CHECK(queue.waiting.empty());
}

TEST_CASE("an action refused by its check changes nothing")
{
    Park p;
    SUBCASE("repaying more than the cash in hand")
    {
        p.state.cash = units::Money { 50 };
        CHECK(simulation::actions::apply(p.world, Action { LoanStep { .move = simulation::park::LoanMove::Repay } }).error() == Refusal::NothingToDo);
        CHECK(p.state.loan == units::Money { 300'000 });
        CHECK(*p.state.cash == units::Money { 50 });
    }
    SUBCASE("borrowing past the most the park may")
    {
        p.state.loan = units::Money { 1'000'000 };
        CHECK(simulation::actions::apply(p.world, Action { SetLoan { .to = units::Money { 2'000'000 } } }).error() == Refusal::NothingToDo);
        CHECK(p.state.loan == units::Money { 1'000'000 });
        CHECK(*p.state.cash == units::Money { 500'000 });
    }
}
