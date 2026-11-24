// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions on the park's staff, from the staff window and a worker's page: hiring, sacking, and what the page sets
// of them, from their name to their uniform's colours. A worker goes by the id they are saved under (ids.hpp).
#pragma once

#include "save/model/people.hpp"
#include "simulation/actions/world.hpp"
#include "simulation/components.hpp"

#include <string>

namespace rct3::simulation::actions
{

    /// Hires a worker for job `kind`: on a walkway picked at random, as the game puts them on one of the park's
    /// paths, in their job's uniform (each entertainer the next costume in turn) in colours picked at random. A
    /// keeper goes to an animal house with the fewest keepers, any of those. Vendors come with their booths, and
    /// lifeguards with their chairs, so neither is hired.
    struct HireStaff
    {
        save::model::StaffKind kind = save::model::StaffKind::Handyman;
    };

    /// Sacks a worker: they leave the park at once, as in the game.
    struct FireStaff
    {
        save::model::PeopleObjectId worker = 0;
    };

    enum class StaffSetting : u8
    {
        Name,        ///< To `text`; a blank one keeps the old
        ToConsole,   ///< Turns posting their new thoughts as messages on or off
        Highlight,   ///< Turns their name floating over them on or off
        Warn,        ///< Until they have had kMostWarnings
        Train,       ///< A level up, paid for at once; free in a park without money
        Wage,        ///< The wage level to `value`, 0 to 2
        Costume,     ///< An entertainer's to costume `value`, which they change into where the install has it
        Task,        ///< Turns task flag bit `value` on or off
        ClearPatrol, ///< Empties their patrol area and their zone's
        FillPatrol,  ///< Fills the box round their patrol area
        PatrolZone,  ///< Puts them on shared patrol zone `value`; 0 none
        Colour,      ///< Their clothes' palette `pot` to colour `value`
    };

    /// Changes a worker as their page does.
    struct ChangeStaff
    {
        save::model::PeopleObjectId worker = 0;
        StaffSetting setting = StaffSetting::Name;
        i32 value = 0;
        usize pot = 0;
        std::string text { };
    };

    [[nodiscard]] Outcome check(const World& world, const HireStaff& action);
    void apply(World& world, const HireStaff& action);
    [[nodiscard]] Outcome check(const World& world, const FireStaff& action);
    void apply(World& world, const FireStaff& action);
    [[nodiscard]] Outcome check(const World& world, const ChangeStaff& action);
    void apply(World& world, const ChangeStaff& action);

    /// What the saved worker's window shows of `staff`'s pay, training, mood and work done.
    void copy_mood(const Staff& staff, save::model::Staff& saved);

} // namespace rct3::simulation::actions
