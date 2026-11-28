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

    Outcome check(const World& world, const SetRideFence& action)
    {
        if (!ride_of(world.rides, action.ride))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const SetRideFence& action)
    {
        const std::optional<save::model::Ride&> found = ride_of(world.rides, action.ride);
        save::model::Ride& ride = *found;
        ride.fence_symbol = action.symbol;
        ride.fence_overlay = action.overlay;
        ride.fence_colours = action.colours;
        world.changed.fenced_rides.push_back(action.ride);
    }

    Outcome check(const World& world, const SetRideDoors& action)
    {
        if (!ride_of(world.rides, action.ride) || action.symbol.empty())
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const SetRideDoors& action)
    {
        for (const save::model::Station& station : ride_of(world.rides, action.ride)->stations)
        {
            const u64 id = action.exits ? station.exit : station.entrance;
            if (const auto door = id == 0 ? world.objects.end() : std::ranges::find(world.objects, id, &save::model::PlacedObject::id); door != world.objects.end())
            {
                door->definition_ref = 0; // The park file's entry names the old look
                door->symbol = action.symbol;
                door->overlay = action.overlay;
            }
        }
        world.changed.redoored_rides.push_back(action.ride);
    }

} // namespace rct3::simulation::actions
