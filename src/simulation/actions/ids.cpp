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

    std::optional<save::model::Staff&> staff_of(save::model::PeopleModel& people, save::model::PeopleObjectId id)
    {
        const auto found = std::ranges::find(people.staff, id, [](const save::model::Staff& s) { return s.person.id; });
        return found != people.staff.end() ? std::optional<save::model::Staff&>(*found) : std::nullopt;
    }

    save::model::PeopleObjectId new_person_id(const save::model::PeopleModel& people)
    {
        save::model::PeopleObjectId next = save::model::PeopleObjectId { 1 } << 32;
        for (const save::model::Guest& g : people.guests)
            next = std::max(next, g.person.id + 1);
        for (const save::model::Staff& s : people.staff)
            next = std::max(next, s.person.id + 1);
        for (const save::model::PartGroup& g : people.part_groups)
            next = std::max(next, g.id + 1);
        return next;
    }

} // namespace rct3::simulation::actions
