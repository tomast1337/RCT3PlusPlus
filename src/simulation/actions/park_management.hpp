// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions that run the park as a business: its loan, its gates, its research and its marketing.
#pragma once

#include "simulation/actions/world.hpp"
#include "simulation/park/campaigns.hpp"
#include "simulation/park/finance.hpp"

#include <cstdint>
#include <optional>

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

    /// Opens the park to guests, or closes it.
    struct OpenPark
    {
        bool open = true;
    };

    /// Sets what the park spends on research a month.
    struct SetResearchBudget
    {
        units::Money per_month { };
    };

    /// Focuses research on one item (ResearchItem::id), or ends the focus.
    struct FocusResearch
    {
        std::optional<u64> item { };
    };

    /// Weighs a research area (in ResearchArea order) from 0 to 1.
    struct SetResearchPriority
    {
        usize area = 0;
        f32 weight = 1;
    };

    /// Starts a marketing campaign (park::start_campaign).
    struct StartCampaign
    {
        park::CampaignOrder order { };
    };

    [[nodiscard]] Outcome check(const World& world, const LoanStep& action);
    void apply(World& world, const LoanStep& action);
    [[nodiscard]] Outcome check(const World& world, const SetLoan& action);
    void apply(World& world, const SetLoan& action);
    [[nodiscard]] Outcome check(const World& world, const OpenPark& action);
    void apply(World& world, const OpenPark& action);
    [[nodiscard]] Outcome check(const World& world, const SetResearchBudget& action);
    void apply(World& world, const SetResearchBudget& action);
    [[nodiscard]] Outcome check(const World& world, const FocusResearch& action);
    void apply(World& world, const FocusResearch& action);
    [[nodiscard]] Outcome check(const World& world, const SetResearchPriority& action);
    void apply(World& world, const SetResearchPriority& action);
    [[nodiscard]] Outcome check(const World& world, const StartCampaign& action);
    void apply(World& world, const StartCampaign& action);

} // namespace rct3::simulation::actions
