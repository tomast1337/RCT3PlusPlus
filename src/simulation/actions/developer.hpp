// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions from the developer windows (the Debug tab and the editor's park values): one-step changes that put the
// park in a state at once, and live values set outright. Each counts as a cheat used in the park. Each kind here
// has `kHostOnly`: in a shared park only the host may use them.
#pragma once

#include "save/model/park.hpp"
#include "simulation/actions/world.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace rct3::simulation::actions
{

    /// The developer tools that change the park; what `amount` means is given for each, and none for those it
    /// does not.
    enum class DeveloperTool : u8
    {
        AddMoney,           ///< Hundredths; none for a park without money
        BuyAllLand,         ///< Every tile on sale, land and rights, for nothing
        FinishResearch,     ///< Invents all that is left; none before the build lists are read
        AddGuests,          ///< How many, in parties spread over the park's own paths
        RemoveGuests,       ///< Sends everyone home at once
        RefreshGuests,      ///< Meets every guest's needs
        BreakRides,         ///< Breaks down every ride
        ResetRides,         ///< Every ride as new
        RemoveAnimals,      ///< Every zoo animal taken out
        RefreshAnimals,     ///< Meets every animal's needs
        DropLitter,         ///< How many pieces, over the park's own paths
        ClearLitter,        ///< All of it taken away
        ResetItemNames,     ///< Every stall item back to its kind's name
        SlowGuests,         ///< How many times slower guests live; 1 their own pace
        PartiesHuddleOften, ///< 1 on, 0 off
        KnockFences,        ///< How many knocks to random enclosures' fences
        BreedAnimals,       ///< Game seconds till birth; 0 an hour
        AgeAnimals,         ///< Game seconds older; 0 a year
        AdmitParties,       ///< How many, at the first gate, free of the fee
        GiveEveryoneCash,   ///< Hundredths, to each guest's wallet
        CheerEveryone,      ///< Everyone made happy and healthy
    };

    struct UseDeveloperTool
    {
        static constexpr bool kHostOnly = true;
        DeveloperTool tool = DeveloperTool::AddMoney;
        i64 amount = 0;
    };

    /// Sets the park's live values outright: each one given, the rest as they are.
    struct SetParkValues
    {
        static constexpr bool kHostOnly = true;
        std::optional<f32> rating { };
        std::optional<units::Money> cash { };                ///< None for a park without money
        std::optional<units::Money> loan { };                ///< Not below nought; the cash stays as it is
        std::optional<units::Money> entrance_fee { };        ///< Between nought and what the save keeps; none for a park without finances
        std::optional<std::pair<i32, i32>> hours { };        ///< Opening and closing, in minutes of the day
        std::optional<save::model::WeatherType> weather { }; ///< Now and forecast, till the next forecast
        std::optional<f32> time_of_day { };                  ///< As a share of the day, shown at once without a blend
    };

    [[nodiscard]] Outcome check(const World& world, const UseDeveloperTool& action);
    /// Returns how many things it changed: tiles, inventions, guests, rides, animals, pieces, knocks, parties or
    /// stall items, or 1 for money added or a switch turned.
    usize apply(World& world, const UseDeveloperTool& action);
    [[nodiscard]] Outcome check(const World& world, const SetParkValues& action);
    void apply(World& world, const SetParkValues& action);

} // namespace rct3::simulation::actions
