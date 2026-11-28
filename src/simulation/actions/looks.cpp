// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/looks.hpp"

#include "park/attraction_objects.hpp"
#include "simulation/actions/ids.hpp"

#include <algorithm>

namespace rct3::simulation::actions
{

    Outcome check(const World& world, const PaintObject& action)
    {
        if (action.slot >= save::model::PlacedObject { }.colours.size() || !std::ranges::contains(world.objects, action.object, &save::model::PlacedObject::id))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const PaintObject& action)
    {
        std::ranges::find(world.objects, action.object, &save::model::PlacedObject::id)->colours[action.slot] = action.colour;
        world.changed.painted_objects.push_back(action.object);
    }

    Outcome check(const World& world, const PaintAttraction& action)
    {
        if (action.slot >= save::model::PlacedObject { }.colours.size() || (!ride_of(world.rides, action.attraction) && !stall_of(world.rides, action.attraction)))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const PaintAttraction& action)
    {
        const auto ride = ride_of(world.rides, action.attraction);
        const rct3::park::AttractionRef painted = ride ? rct3::park::AttractionRef::of(save::model::RideIndex { static_cast<usize>(&*ride - world.rides.rides.data()) }) : rct3::park::AttractionRef::of(save::model::StallIndex { static_cast<usize>(&*stall_of(world.rides, action.attraction) - world.rides.stalls.data()) });
        const rct3::park::AttractionObjects owners = rct3::park::attraction_objects(world.rides, world.objects);
        for (save::model::PlacedObject& o : world.objects)
        {
            const auto owner = owners.find(o.id);
            if (owner == owners.end() || owner->second != painted)
                continue;
            o.colours[action.slot] = action.colour;
            world.changed.painted_objects.push_back(o.id);
        }
    }

} // namespace rct3::simulation::actions
