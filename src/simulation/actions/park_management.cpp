// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/park_management.hpp"

#include "simulation/components.hpp"
#include "simulation/research.hpp"

#include <utility>

namespace rct3::simulation::actions
{

    Outcome check(const World& world, const LoanStep& action)
    {
        if (park::loan_step(world.park, { world.model.finance.max_loan }, action.move) == units::Money { })
            return std::unexpected(Refusal::NothingToDo);
        return Checked { };
    }

    void apply(World& world, const LoanStep& action)
    {
        (void)park::change_loan(world.park, { world.model.finance.max_loan }, action.move);
    }

    Outcome check(const World& world, const SetLoan& action)
    {
        if (action.to == world.park.loan)
            return std::unexpected(Refusal::NothingToDo);
        return check(world, LoanStep { .move = action.to > world.park.loan ? park::LoanMove::Borrow : park::LoanMove::Repay });
    }

    void apply(World& world, const SetLoan& action)
    {
        // A step at a time, as the buttons do, so the steps' limits hold.
        const park::LoanMove move = action.to > world.park.loan ? park::LoanMove::Borrow : park::LoanMove::Repay;
        while (action.to != world.park.loan && (action.to > world.park.loan) == (move == park::LoanMove::Borrow) && park::change_loan(world.park, { world.model.finance.max_loan }, move))
        {
        }
    }

    Outcome check(const World&, const OpenPark&)
    {
        return Checked { };
    }

    void apply(World& world, const OpenPark& action)
    {
        auto& flags = world.model.park.flags.raw;
        flags = action.open ? flags | std::to_underlying(save::model::ParkFlag::Open) : flags & ~std::to_underlying(save::model::ParkFlag::Open);
        for (auto [e, gate] : world.sim.registry.view<ParkGate>().each())
            gate.closed = !action.open;
    }

    Outcome check(const World&, const SetResearchBudget& action)
    {
        if (action.per_month < units::Money { })
            return std::unexpected(Refusal::NothingToDo);
        return Checked { };
    }

    void apply(World& world, const SetResearchBudget& action)
    {
        world.research.budget = action.per_month;
    }

    Outcome check(const World& world, const FocusResearch&)
    {
        // What can be researched is known only once the build lists are read.
        if (!world.research_items)
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const FocusResearch& action)
    {
        (void)focus_research(world.research, action.item, *world.research_items);
    }

    Outcome check(const World&, const SetResearchPriority& action)
    {
        if (action.area >= kResearchAreas)
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const SetResearchPriority& action)
    {
        // Areas past the end of the priorities weigh the most until set.
        if (world.research.priorities.size() <= action.area)
            world.research.priorities.resize(action.area + 1, 1.0f);
        world.research.priorities[action.area] = action.weight;
    }

    Outcome check(const World& world, const StartCampaign& action)
    {
        if (!park::may_start_campaign(world.model.campaigns, action.order))
            return std::unexpected(Refusal::NothingToDo);
        return Checked { };
    }

    void apply(World& world, const StartCampaign& action)
    {
        // Refused there, with the player told, when the park cannot pay all its weeks.
        (void)park::start_campaign(world.park, world.model.finance, world.ledger_month, world.model.campaigns, action.order);
    }

} // namespace rct3::simulation::actions
