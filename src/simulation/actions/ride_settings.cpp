// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/ride_settings.hpp"

#include "simulation/actions/ids.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/rides.hpp"

#include <algorithm>

namespace rct3::simulation::actions
{

    Outcome check(const World& world, const ChangeRide& action)
    {
        if (!ride_of(world.rides, action.ride))
            return std::unexpected(Refusal::Missing);
        const bool wait = action.setting == RideSetting::MinWait || action.setting == RideSetting::MaxWait;
        if (!action.value && !wait)
            return std::unexpected(Refusal::NothingToDo);
        return Checked { };
    }

    void apply(World& world, const ChangeRide& action)
    {
        const std::optional<save::model::Ride&> found = ride_of(world.rides, action.ride);
        save::model::Ride& ride = *found;
        const i32 value = action.value.value_or(0);
        auto& r = world.sim.registry;
        // What the simulation runs of the ride: the attraction saved under its id.
        const auto each_entity = [&](auto&& change) {
            for (const auto [e, a] : r.view<Attraction>().each())
                if (a.saved == action.ride && a.kind != AttractionKind::Pool)
                    change(e, a);
        };
        switch (action.setting)
        {
            case RideSetting::TicketPrice:
                ride.ticket_price = { value };
                // Guests pay the new price from now on; a facility run as a shop takes nothing below nought.
                each_entity([&](entt::entity, Attraction& a) { a.price = a.kind == AttractionKind::Shop ? units::Money { std::max(value, 0) } : ride.ticket_price; });
                break;
            case RideSetting::InspectEvery:
                ride.breakdown.inspection_interval = value;
                each_entity([&](entt::entity e, Attraction&) {
                    if (auto* upkeep = r.try_get<Upkeep>(e))
                        upkeep->inspect_every = upkeep_of(ride).inspect_every;
                });
                break;
            case RideSetting::MinWait:
                ride.options.min_wait = action.value;
                break;
            case RideSetting::MaxWait:
                ride.options.max_wait = action.value;
                break;
            case RideSetting::WaitForLoad:
                ride.options.wait_for_load = value;
                break;
            case RideSetting::Circuits:
                // A go under way runs as many times round as the new count, or stops if it is past it.
                // shortcut: a tracked ride keeps the count but its trains go round once; run the laps once the
                // trains count them.
                ride.loop_count = value;
                each_entity([&](entt::entity e, Attraction&) {
                    if (auto* cycle = r.try_get<CycleRide>(e))
                        cycle->loops = static_cast<u32>(std::max(value, 1));
                });
                break;
            case RideSetting::BlockSections:
                // shortcut: the game also caps the trains at one fewer than the blocks; upgrade with the trains'
                // limits counting blocks.
                ride.options.block_sectioned = value != 0;
                each_entity([&](entt::entity e, Attraction&) {
                    if (auto* fleet = r.try_get<Trains>(e))
                        for (Coaster& c : fleet->trains)
                            c.blocks = ride.options.block_sectioned;
                });
                break;
            case RideSetting::SyncStations:
                ride.options.sync_trains = value != 0; // shortcut: stored, stations do not wait for each other yet
                break;
            case RideSetting::LeaveOnArrival:
                ride.options.leave_if_other_train_enters_station = value != 0;
                break;
        }
        // The running ride leaves as its settings now say.
        each_entity([&](entt::entity e, Attraction&) {
            if (auto* departure = r.try_get<Departure>(e))
                *departure = departure_of(ride.options);
        });
    }

    Outcome check(const World& world, const CallMechanic& action)
    {
        if (!ride_of(world.rides, action.ride))
            return std::unexpected(Refusal::Missing);
        for (const auto [e, a] : world.sim.registry.view<const Attraction>().each())
            if (a.saved == action.ride && world.sim.registry.all_of<Upkeep>(e))
                return Checked { };
        return std::unexpected(Refusal::Missing);
    }

    void apply(World& world, const CallMechanic& action)
    {
        // shortcut: the game also opens the mechanic's window and puts this job first on a busy one's list; add
        // both with the staff window's job list.
        for (const auto [e, a] : world.sim.registry.view<const Attraction>().each())
            if (auto* upkeep = a.saved == action.ride ? world.sim.registry.try_get<Upkeep>(e) : nullptr)
                upkeep->called = true;
    }

    Outcome check(const World& world, const RenameAttraction& action)
    {
        if (!ride_of(world.rides, action.attraction) && !stall_of(world.rides, action.attraction))
            return std::unexpected(Refusal::Missing);
        if (action.name.find_first_not_of(' ') == std::string::npos)
            return std::unexpected(Refusal::NothingToDo);
        return Checked { };
    }

    void apply(World& world, const RenameAttraction& action)
    {
        if (auto ride = ride_of(world.rides, action.attraction))
            ride->base.name = action.name;
        else if (auto stall = stall_of(world.rides, action.attraction))
            stall->base.name = action.name;
    }

    Outcome check(const World& world, const EditShow& action)
    {
        const auto show = std::ranges::find(world.shows, action.ride, &save::model::RideShow::ride);
        if (show == world.shows.end() || (!action.trick && (action.place >= show->order.size() || show->order.size() < 2)))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const EditShow& action)
    {
        save::model::RideShow& show = *std::ranges::find(world.shows, action.ride, &save::model::RideShow::ride);
        if (!action.trick)
            show.order.erase(show.order.begin() + static_cast<std::ptrdiff_t>(action.place));
        else if (action.place < show.order.size())
            show.order[action.place] = *action.trick;
        else
            show.order.push_back(*action.trick);
        for (auto [e, a, ride] : world.sim.registry.view<Attraction, CycleRide>().each())
            if (a.saved == action.ride)
                stage_show(ride, show);
    }

    Outcome check(const World& world, const SetRideSpeed& action)
    {
        if (!ride_of(world.rides, action.ride))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const SetRideSpeed& action)
    {
        const std::optional<save::model::Ride&> found = ride_of(world.rides, action.ride);
        save::model::Ride& ride = *found;
        (action.launch ? ride.options.launch_speed : ride.options.lift_speed) = { action.speed };
        for (auto [e, a, fleet] : world.sim.registry.view<Attraction, Trains>().each())
        {
            if (a.saved != action.ride || a.kind == AttractionKind::Pool)
                continue;
            for (Coaster& c : fleet.trains)
            {
                if (action.launch)
                    c.launch_speed = launch_of(ride);
                else
                    c.lift_speed = c.station_speed = action.speed;
            }
        }
    }

    Outcome check(const World& world, const SetRideMusic& action)
    {
        if (!ride_of(world.rides, action.ride))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const SetRideMusic& action)
    {
        const std::optional<save::model::Ride&> found = ride_of(world.rides, action.ride);
        found->music = action.file;
    }

} // namespace rct3::simulation::actions
