// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/park_management.hpp"

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

} // namespace rct3::simulation::actions
