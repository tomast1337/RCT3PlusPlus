// SPDX-License-Identifier: GPL-3.0-or-later
//
// How actions name what they change: by ids that stay the same while the park's lists change, never by a place in
// a list. A ride or stall goes by the id it is saved under (AttractionCommon::id), a placed object by its own
// (PlacedObject::id), and a guest by their entity, whose version tells someone gone from whoever is given their slot
// after. A worker goes by the id they are saved under (Person::id), as some have no walker: a park seen without its
// simulation, or one the save left off the paths.
#pragma once

#include "save/model/people.hpp"
#include "save/model/rides.hpp"

#include <optional>

namespace rct3::simulation::actions
{

    /// The ride saved under `id`, unless the park has none or it is demolished.
    [[nodiscard]] std::optional<save::model::Ride&> ride_of(save::model::RideModel& rides, save::model::ObjectId id);

    /// The stall saved under `id`, unless the park has none or it is demolished.
    [[nodiscard]] std::optional<save::model::Stall&> stall_of(save::model::RideModel& rides, save::model::ObjectId id);

    /// The worker saved under `id`, unless the park has none.
    [[nodiscard]] std::optional<save::model::Staff&> staff_of(save::model::PeopleModel& people, save::model::PeopleObjectId id);

    /// An id for a new person that none of `people`'s people or part groups has: past those the game saves (32
    /// bits), as the part groups rct3++ adds are.
    [[nodiscard]] save::model::PeopleObjectId new_person_id(const save::model::PeopleModel& people);

} // namespace rct3::simulation::actions
