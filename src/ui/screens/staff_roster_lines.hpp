// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the staff window lists: one line per worker of the jobs the player chose to see, in the order the player
// chose to sort them by. Each line holds what its row shows, so the window can tell which rows changed.
#pragma once

#include "save/model/people.hpp"
#include "ui/screens/management_pages.hpp"
#include "ui/string_table.hpp"
#include "ui/thought_text.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    /// What the staff window sorts its rows by, in the order its drop-down offers them.
    enum class RosterSort : std::uint8_t
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
    inline constexpr std::size_t kRosterSorts = static_cast<std::size_t>(RosterSort::Count_);

    /// The drop-down's words for each of RosterSort, as string keys.
    inline constexpr std::array<std::string_view, kRosterSorts> kRosterSortNames = {
        "StaffWindow_SortBy_Job", "StaffWindow_SortBy_Name", "StaffWindow_SortBy_PatrolArea", "StaffWindow_SortBy_Tasks", "StaffWindow_SortBy_Uniform", "StaffWindow_SortBy_Wage", "StaffWindow_SortBy_Activity", "StaffWindow_SortBy_Happiness", "StaffWindow_SortBy_Warnings", "StaffWindow_SortBy_Training", "StaffWindow_SortBy_StartDate", "StaffWindow_SortBy_Laziness",
    };

    /// A job the window's switches show or hide: the switch's skin family and help. Their order is also the
    /// order of the jobs when sorted by job.
    struct RosterJob
    {
        save::StaffKind kind = save::StaffKind::Handyman;
        std::string_view skin;
        std::string_view help;
    };
    inline constexpr std::array<RosterJob, 7> kRosterJobs = { {
        { save::StaffKind::Handyman, "Staff_ViewMaintenance", "StaffWindow_ViewMaintenanceHelp" },
        { save::StaffKind::Mechanic, "Staff_ViewMechanic", "StaffWindow_ViewMechanicHelp" },
        { save::StaffKind::SecurityGuard, "Staff_ViewSecurity", "StaffWindow_ViewSecurityHelp" },
        { save::StaffKind::Entertainer, "Staff_ViewEntertainer", "StaffWindow_ViewEntertainerHelp" },
        { save::StaffKind::Vendor, "Staff_ViewVendor", "StaffWindow_ViewVendorHelp" },
        { save::StaffKind::ParkInspector, "Staff_ViewParkInspector", "StaffWindow_ViewParkInspectorHelp" },
        { save::StaffKind::AnimalKeeper, "Staff_ViewAnimalKeeper", "StaffWindow_ViewAnimalKeeperHelp" },
    } };

    /// The jobs shown, one bit per kRosterJobs entry: everyone but the vendors, who stand at their stalls.
    using RosterJobs = std::uint8_t;
    inline constexpr RosterJobs kAllRosterJobs = (1u << kRosterJobs.size()) - 1;
    inline constexpr RosterJobs kStartRosterJobs = kAllRosterJobs & ~(1u << 4);

    /// Whether `jobs` shows workers of `kind`. Jobs without a switch show always.
    [[nodiscard]] bool shows_job(RosterJobs jobs, save::StaffKind kind);

    /// A worker's row: who they are, what they do and what the player can change.
    struct RosterLine
    {
        std::size_t index = 0; ///< In the park's staff
        save::StaffKind kind = save::StaffKind::Handyman;
        std::u16string name;
        std::u16string doing;
        Mood mood = Mood::Neutral;
        float happiness = 0;
        float laziness = 0;
        std::int32_t warnings = 0;
        std::int32_t training = 0;
        std::int32_t wage_level = 0;
        std::int64_t wage = 0;          ///< An hour's pay, hundredths
        std::int64_t training_cost = 0; ///< Of their next level; 0 once fully trained
        std::int32_t tasks = 0;         ///< Task flags
        std::size_t patrol_tiles = 0;   ///< 0 for none: the whole park
        bool painting_patrol = false;   ///< The player is painting their patrol area
        std::array<std::uint8_t, 2> colours { };
        std::uint8_t costume = 0; ///< An entertainer's
        float hired = 0;          ///< When, in the park's seconds
        std::u16string hired_text;

        friend bool operator==(const RosterLine&, const RosterLine&) = default;
    };

    /// The lines of the workers of `jobs`, sorted by `sort`: the lowest first by name, job, activity, uniform and
    /// start date, the highest first by the others. Ties keep the order of the jobs, then the park's.
    [[nodiscard]] std::vector<RosterLine> roster_lines(const ManagementFacts& facts, const StringTable& strings, RosterJobs jobs, RosterSort sort);

} // namespace rct3::ui
