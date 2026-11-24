// SPDX-License-Identifier: GPL-3.0-or-later
//
// How actions name what they change: by ids that stay the same while the park's lists change, never by a place in
// a list. A ride or stall goes by the id it is saved under (AttractionCommon::id), a placed object by its own
// (PlacedObject::id), and a person, guest or worker, by their entity, whose version tells someone gone from whoever
// is given their slot after.
#pragma once

#include "save/model/rides.hpp"

#include <optional>

namespace rct3::simulation::actions
{

    /// The ride saved under `id`, unless the park has none or it is demolished.
    [[nodiscard]] std::optional<save::model::Ride&> ride_of(save::model::RideModel& rides, save::model::ObjectId id);

    /// The stall saved under `id`, unless the park has none or it is demolished.
    [[nodiscard]] std::optional<save::model::Stall&> stall_of(save::model::RideModel& rides, save::model::ObjectId id);

} // namespace rct3::simulation::actions
