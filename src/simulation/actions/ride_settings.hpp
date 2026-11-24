// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions on a ride's or stall's settings, as its window changes them: its name, a ride's ticket price, inspections,
// waits and the way its trains run.
#pragma once

#include "save/model/rides.hpp"
#include "simulation/actions/world.hpp"

#include <optional>
#include <string>

namespace rct3::simulation::actions
{

    enum class RideSetting : u8
    {
        TicketPrice,    ///< In hundredths
        InspectEvery,   ///< As the save keeps the interval
        MinWait,        ///< Whole seconds; none waits no least time
        MaxWait,        ///< Whole seconds; none waits no longest time
        WaitForLoad,    ///< As save::model::RideOptions::wait_for_load
        Circuits,       ///< Times round a go
        BlockSections,  ///< Nought or one
        SyncStations,   ///< Nought or one
        LeaveOnArrival, ///< Nought or one: a train leaves when another comes into the station
    };

    /// Sets one of a ride's settings.
    struct ChangeRide
    {
        save::model::ObjectId ride = 0;
        RideSetting setting = RideSetting::TicketPrice;
        std::optional<i32> value { }; ///< None only for a wait turned off
    };

    /// Calls a mechanic to the ride, to inspect it or mend it.
    struct CallMechanic
    {
        save::model::ObjectId ride = 0;
    };

    /// Names a ride or stall. A blank name is refused, keeping the old one, as in the game.
    struct RenameAttraction
    {
        save::model::ObjectId attraction = 0;
        std::string name { };
    };

    /// Puts a show ride's trick `trick` in place `place` of its show (after the last when past it), or with no
    /// trick takes the one there out; a show keeps one at least. The ride puts it on from its next go.
    struct EditShow
    {
        save::model::ObjectId ride = 0;
        usize place = 0;
        std::optional<u32> trick { };
    };

    /// Sets the speed a ride's trains are launched at (`launch`) or lifted at, in metres a second.
    struct SetRideSpeed
    {
        save::model::ObjectId ride = 0;
        bool launch = true;
        f32 speed = 0;
    };

    /// Has a ride play a file of the game's music folder while open, or none when empty.
    struct SetRideMusic
    {
        save::model::ObjectId ride = 0;
        std::string file { };
    };

    [[nodiscard]] Outcome check(const World& world, const ChangeRide& action);
    void apply(World& world, const ChangeRide& action);
    [[nodiscard]] Outcome check(const World& world, const CallMechanic& action);
    void apply(World& world, const CallMechanic& action);
    [[nodiscard]] Outcome check(const World& world, const RenameAttraction& action);
    void apply(World& world, const RenameAttraction& action);
    [[nodiscard]] Outcome check(const World& world, const EditShow& action);
    void apply(World& world, const EditShow& action);
    [[nodiscard]] Outcome check(const World& world, const SetRideSpeed& action);
    void apply(World& world, const SetRideSpeed& action);
    [[nodiscard]] Outcome check(const World& world, const SetRideMusic& action);
    void apply(World& world, const SetRideMusic& action);

} // namespace rct3::simulation::actions
