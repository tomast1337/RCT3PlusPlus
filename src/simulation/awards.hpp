// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The awards a park can be given, and the fines: each has a number, as the park file keeps them, a name and
// whether it is a fine. Each month, once the scenario lets awards begin, every award the park has not had in
// the last five months and that the scenario offers is looked at, and given when the park earns it; several
// can come in one month. An award a park has had lately also keeps away its opposite, and others that would
// sit badly with it. Each pays the park, or for a fine costs it, what the scenario sets.

#include "random/generator.hpp"
#include "save/model/park.hpp"

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    /// Kinds of award, numbered as saved.
    inline constexpr std::int32_t kAwardKinds = 31;

    /// Text key of award `kind`'s name; empty for a number that is no award.
    [[nodiscard]] std::string_view award_name(std::int32_t kind);

    /// Whether award `kind` is a fine, which costs the park rather than paying it.
    [[nodiscard]] bool award_is_fine(std::int32_t kind);

    /// Text key of the message telling the player they have award `kind`.
    [[nodiscard]] std::string_view award_message(std::int32_t kind);

    /// What the awards look at in the park this month.
    struct AwardFigures
    {
        std::int32_t guests = 0;
        std::int32_t rating = 0;       ///< 0 to 1000
        bool money = false;            ///< The park counts its money
        bool fee_charged = false;      ///< And the player sets the entrance fee
        std::int64_t entrance_fee = 0; ///< Hundredths
        std::int64_t fair_fee = 0;     ///< What guests think the park is worth getting in, in hundredths
        /// By kind of thought (as ThoughtKind numbers them): how many guests have had it lately.
        std::map<std::int32_t, std::int32_t> thoughts { };
        std::int32_t old_litter = 0; ///< Pieces lying about long
        std::int32_t stalls = 0;
        std::int32_t food_stalls = 0;
        std::int32_t food_kinds = 0; ///< Kinds of food sold
        std::int32_t toilets = 0;
        std::int32_t thrilling = 0; ///< Rides rated over 5 for excitement and intensity both
        std::int32_t coasters = 0;
        std::int32_t water_rides = 0;
        std::int32_t staff = 0;
        bool every_staff_kind = false;    ///< Handymen, mechanics, guards and entertainers, at least one of each
        std::vector<float> downtime { };  ///< Of each ride, 0 to 100 of the time it is out of action
        float reliability = 0;            ///< Of the rides on average, 0 to 100
        std::int32_t running = 0;         ///< Rides open and not broken down
        std::int32_t crashed = 0;         ///< Rides that have ever crashed
        std::int32_t coaster_dangers = 0; ///< Dangerous happenings on coasters lately, as the awards keep them
        std::int32_t pool_dangers = 0;    ///< And at pools
    };

    /// Whether the park earns award `kind` this month. `dice` draws the luck some awards leave to chance.
    [[nodiscard]] bool award_earned(std::int32_t kind, const AwardFigures& figures, std::uint32_t lately, random::RandomState& dice);

    /// Starts a new month of awards: those the park earns join this month's, which shifts the oldest of the last
    /// six out. Returns the awards given, in order of kind; the caller pays them and tells the player.
    std::vector<std::int32_t> award_month(save::Awards& awards, const AwardFigures& figures, random::RandomState& dice);

} // namespace rct3::simulation
