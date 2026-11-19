// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The awards a park can be given, and the fines: each has a number, as the park file keeps them, a name and
// whether it is a fine. Each month, once the scenario lets awards begin, every award the park has not had in
// the last five months and that the scenario offers is looked at, and given when the park earns it; several
// can come in one month. An award a park has had lately also keeps away its opposite, and others that would
// sit badly with it. Each pays the park, or for a fine costs it, what the scenario sets.

#include "core/types.hpp"
#include "park/build_catalogue.hpp"
#include "random/generator.hpp"
#include "save/model/park.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/components.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    /// Kinds of award, numbered as saved.
    inline constexpr i32 kAwardKinds = 31;

    /// Text key of award `kind`'s name; empty for a number that is no award.
    [[nodiscard]] std::string_view award_name(i32 kind);

    /// Whether award `kind` is a fine, which costs the park rather than paying it.
    [[nodiscard]] bool award_is_fine(i32 kind);

    /// Text key of the message telling the player they have award `kind`.
    [[nodiscard]] std::string_view award_message(i32 kind);

    /// What the awards look at in the park this month.
    struct AwardFigures
    {
        i32 guests = 0;
        i32 rating = 0;           ///< 0 to 1000
        bool money = false;       ///< The park counts its money
        bool fee_charged = false; ///< And the player sets the entrance fee
        units::Money entrance_fee { };
        units::Money fair_fee { }; ///< What guests think the park is worth getting in
        /// By kind of thought: how many guests have had it lately.
        std::map<ThoughtKind, i32> thoughts { };
        i32 old_litter = 0; ///< Pieces lying about long
        i32 stalls = 0;
        i32 food_stalls = 0;
        i32 food_kinds = 0;
        i32 toilets = 0;
        i32 thrilling = 0; ///< Rides rated over 5 for excitement and intensity both
        i32 coasters = 0;
        i32 water_rides = 0;
        i32 staff = 0;
        bool every_staff_kind = false; ///< Handymen, mechanics, guards and entertainers, at least one of each
        std::vector<f32> downtime { }; ///< Of each ride, 0 to 100 of the time it is out of action
        f32 reliability = 0;           ///< Of the rides on average, 0 to 100
        i32 running = 0;               ///< Rides open and not broken down
        i32 crashed = 0;               ///< Rides that have ever crashed
        i32 coaster_dangers = 0;       ///< Dangerous happenings on coasters lately, as the awards keep them
        i32 pool_dangers = 0;          ///< And at pools
        /// By theme, as the themes' overlays name them ("Spooky"): rides and stalls that stand among its scenery.
        std::map<std::string, i32, std::less<>> themed { };
        /// An animal out of its crate, as the animal awards weigh it.
        struct Animal
        {
            f32 rarity = 0; ///< Of its kind, from 0, common, to 1, endangered
            f32 health = 0; ///< 0 to 1
            bool young = false;
        };
        std::vector<Animal> animals { };
    };

    /// Of the park's rides and stalls, how many carry each theme: those with enough scenery of the theme round
    /// them, their own theme counting for some. `catalogue` tells each overlay's theme and each item's size. By
    /// theme name, as AwardFigures::themed.
    [[nodiscard]] std::map<std::string, i32, std::less<>> themed_attractions(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects, std::span<const rct3::park::BuildItem> catalogue);

    /// The saved ids of the rides and stalls whose scenery round them carries a theme other than their own (or
    /// any, for one of none), counted as themed_attractions counts it.
    [[nodiscard]] std::vector<u64> off_theme_attractions(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects, std::span<const rct3::park::BuildItem> catalogue);

    /// Whether the park earns award `kind` this month. `dice` draws the luck some awards leave to chance.
    [[nodiscard]] bool award_earned(i32 kind, const AwardFigures& figures, u32 lately, random::RandomState& dice);

    /// Starts a new month of awards: those the park earns join this month's, which shifts the oldest of the last
    /// six out. Returns the awards given, in order of kind; the caller pays them and tells the player.
    std::vector<i32> award_month(save::model::Awards& awards, const AwardFigures& figures, random::RandomState& dice);

} // namespace rct3::simulation
