// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an action works on, and what comes of it. Each kind of action is checked first (can it be done, and what it
// costs) and only then applied; one refused changes nothing.
#pragma once

#include "core/types.hpp"
#include "park/carried_goods.hpp"
#include "save/model/park.hpp"
#include "save/model/rides.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/state.hpp"
#include "simulation/research.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::simulation::actions
{

    /// The parts of a park that actions read and change. Whoever runs the park owns them and lends them for the call.
    struct World
    {
        SimWorld& sim;
        park::ParkState& park;
        save::model::ParkModel& model;
        save::model::RideModel& rides;
        Research& research;
        std::optional<std::vector<ResearchItem>>& research_items; ///< What can be researched; none until the build lists are read
        std::optional<i64>& ledger_month;                         ///< As park::ledger_month keeps it
        DayClock& day_clock;
        const rct3::park::CarriedKinds& carried;               ///< What stalls sell and its extras
        std::vector<save::model::StallIndex>& unjoined_stalls; ///< Stalls with something to sell, waiting to be joined to the paths
    };

    /// Why an action was not done.
    struct Refusal
    {
        enum Kind : u8
        {
            Missing,      ///< It names something the park does not have, or no longer has
            CannotAfford, ///< The park has not the money
            NothingToDo,  ///< The park is already as asked, or can move no further that way
        };

        Refusal(Kind kind, std::string told = { })
            : kind(kind)
            , told(std::move(told))
        {
        }

        Kind kind;
        std::string told; ///< The string key the player is told why by, where the game tells them

        bool operator==(Kind other) const
        {
            return kind == other;
        }
    };

    /// An action that may be done.
    struct Checked
    {
        units::Money cost { }; ///< What doing it takes from the park's cash
    };

    using Outcome = std::expected<Checked, Refusal>;

} // namespace rct3::simulation::actions
