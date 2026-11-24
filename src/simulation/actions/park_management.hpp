// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions that run the park as a business: its loan.
#pragma once

#include "simulation/actions/world.hpp"
#include "simulation/park/finance.hpp"

namespace rct3::simulation::actions
{

    /// Borrow or repay one step of the loan (park::loan_step).
    struct LoanStep
    {
        park::LoanMove move { };
    };

    /// Borrow or repay a step at a time until the loan is `to`, or can move no further.
    struct SetLoan
    {
        units::Money to { };
    };

    [[nodiscard]] Outcome check(const World& world, const LoanStep& action);
    void apply(World& world, const LoanStep& action);
    [[nodiscard]] Outcome check(const World& world, const SetLoan& action);
    void apply(World& world, const SetLoan& action);

} // namespace rct3::simulation::actions
