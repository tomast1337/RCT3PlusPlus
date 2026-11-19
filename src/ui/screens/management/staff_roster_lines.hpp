// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the staff window lists: one line per worker of the jobs the player chose to see, in the order the player
// chose to sort them by. Each line holds what its row shows, so the window can tell which rows changed.
#pragma once

#include "core/types.hpp"
#include "save/model/people.hpp"
#include "ui/screens/management/pages.hpp"
#include "ui/string_table.hpp"
#include "ui/thought_text.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui::screens::management
{

    /// What the staff window sorts its rows by, in the order its drop-down offers them.
    enum class RosterSort : u8
    {
        Job,
        Name,
        PatrolArea,
        Tasks,
        Uniform,
        Wage,
        Activity,
        Happiness,
        Warnings,
        Training,
        StartDate,
        Laziness,
        Count_,
    };
    inline constexpr usize kRosterSorts = static_cast<usize>(RosterSort::Count_);

    /// The drop-down's words for each of RosterSort, as string keys.
    inline constexpr std::array<std::string_view, kRosterSorts> kRosterSortNames = {
        "StaffWindow_SortBy_Job", "StaffWindow_SortBy_Name", "StaffWindow_SortBy_PatrolArea", "StaffWindow_SortBy_Tasks", "StaffWindow_SortBy_Uniform", "StaffWindow_SortBy_Wage", "StaffWindow_SortBy_Activity", "StaffWindow_SortBy_Happiness", "StaffWindow_SortBy_Warnings", "StaffWindow_SortBy_Training", "StaffWindow_SortBy_StartDate", "StaffWindow_SortBy_Laziness",
    };

    /// A job the window's switches show or hide: the switch's skin family and help. Their order is also the
    /// order of the jobs when sorted by job.
    struct RosterJob
    {
        save::model::StaffKind kind = save::model::StaffKind::Handyman;
        std::string_view skin;
        std::string_view help;
    };
    inline constexpr std::array<RosterJob, 7> kRosterJobs = { {
        { save::model::StaffKind::Handyman, "Staff_ViewMaintenance", "StaffWindow_ViewMaintenanceHelp" },
        { save::model::StaffKind::Mechanic, "Staff_ViewMechanic", "StaffWindow_ViewMechanicHelp" },
        { save::model::StaffKind::SecurityGuard, "Staff_ViewSecurity", "StaffWindow_ViewSecurityHelp" },
        { save::model::StaffKind::Entertainer, "Staff_ViewEntertainer", "StaffWindow_ViewEntertainerHelp" },
        { save::model::StaffKind::Vendor, "Staff_ViewVendor", "StaffWindow_ViewVendorHelp" },
        { save::model::StaffKind::ParkInspector, "Staff_ViewParkInspector", "StaffWindow_ViewParkInspectorHelp" },
        { save::model::StaffKind::AnimalKeeper, "Staff_ViewAnimalKeeper", "StaffWindow_ViewAnimalKeeperHelp" },
    } };

    /// The jobs shown, one bit per kRosterJobs entry: everyone but the vendors, who stand at their stalls.
    using RosterJobs = u8;
    inline constexpr RosterJobs kAllRosterJobs = (1u << kRosterJobs.size()) - 1;
    inline constexpr RosterJobs kStartRosterJobs = kAllRosterJobs & ~(1u << 4);

    /// Whether `jobs` shows workers of `kind`. Jobs without a switch show always, but lifeguards never.
    [[nodiscard]] bool shows_job(RosterJobs jobs, save::model::StaffKind kind);

    /// A worker's row: who they are, what they do and what the player can change.
    struct RosterLine
    {
        usize index = 0; ///< In the park's staff
        save::model::StaffKind kind = save::model::StaffKind::Handyman;
        std::u16string name;
        std::u16string doing;
        Mood mood = Mood::Neutral;
        f32 happiness = 0;
        f32 laziness = 0;
        i32 warnings = 0;
        i32 training = 0;
        i32 wage_level = 0;
        units::Money wage;          ///< An hour's pay
        units::Money training_cost; ///< Of their next level; nothing once fully trained
        i32 tasks = 0;              ///< Task flags
        usize patrol_tiles = 0;     ///< 0 for none: the whole park
        bool painting_patrol = false;
        std::array<u8, 2> colours { };
        u8 costume = 0; ///< An entertainer's
        f32 hired = 0;  ///< When, in the park's seconds
        std::u16string hired_text;

        friend bool operator==(const RosterLine&, const RosterLine&) = default;
    };

    /// The lines of the workers of `jobs`, sorted by `sort`: the lowest first by name, job, activity, uniform and
    /// start date, the highest first by the others. Ties keep the order of the jobs, then the park's.
    [[nodiscard]] std::vector<RosterLine> roster_lines(const ManagementFacts& facts, const StringTable& strings, RosterJobs jobs, RosterSort sort);

} // namespace rct3::ui::screens::management
