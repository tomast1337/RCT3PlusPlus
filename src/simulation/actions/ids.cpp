// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/ids.hpp"

#include <algorithm>

namespace rct3::simulation::actions
{

    std::optional<save::model::Ride&> ride_of(save::model::RideModel& rides, save::model::ObjectId id)
    {
        const auto found = std::ranges::find_if(rides.rides, [&](const save::model::Ride& r) { return r.base.id == id && !r.base.demolished; });
        return found != rides.rides.end() ? std::optional<save::model::Ride&>(*found) : std::nullopt;
    }

    std::optional<save::model::Stall&> stall_of(save::model::RideModel& rides, save::model::ObjectId id)
    {
        const auto found = std::ranges::find_if(rides.stalls, [&](const save::model::Stall& s) { return s.base.id == id && !s.base.demolished; });
        return found != rides.stalls.end() ? std::optional<save::model::Stall&>(*found) : std::nullopt;
    }

} // namespace rct3::simulation::actions
