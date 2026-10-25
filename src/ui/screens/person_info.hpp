// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a guest or a member of staff: which tabs they have, where each sits on the
// ring, and the lines of each tab's content, written from the person's saved state. Guests have a general
// tab, their thoughts, the rides they went on, what they carry, how they feel, their money and their
// passport; staff have the general tab, their thoughts, how they work, and by their job their tasks, their
// uniform's colours or their costume. What the player can change in the save (names, tasks, wages,
// uniforms, costumes, warnings and firing) comes as the pages' controls; a value the save does not hold is
// shown as missing.
#pragma once

#include "core/maybe.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ui
{

    enum class PersonTab : std::uint8_t
    {
        General, ///< Who they are and what they are doing
        Thoughts,
        Rides,
        Carrying,
        Stats,
        Cash,
        Passport,
        Tasks,   ///< Staff: the jobs they do
        Uniform, ///< Staff: a paint tab, the colours of their uniform or costume
        Costume, ///< Entertainers: which costume they wear
    };

    struct PersonTabInfo
    {
        PersonTab tab = PersonTab::General;
        int slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
        std::size_t pots = 0;  ///< Of a paint tab: the clothes it colours, upper first
    };

    /// The person a page is written about, the guest or the member of staff they are, and the park's people and
    /// attractions their references lead to.
    struct PersonFacts
    {
        const save::Guest* guest = nullptr;
        const save::Staff* staff = nullptr;
        const save::PeopleModel* people = nullptr;
        const save::RideModel* rides = nullptr;
        /// Staff the simulation walks: the tiles of their patrol area (0 for none, the whole park), and whether
        /// clicks on the ground paint it. Empty for others.
        std::optional<std::size_t> patrol_tiles { };
        bool painting_patrol = false;
        int patrol_brush = 0;            ///< Of kPatrolBrushes
        std::uint32_t patrol_zone = 0;   ///< The shared zone they are on, 0 none
        std::size_t uncovered_paths = 0; ///< Path tiles no one in their job has in a patrol, when any has one
        /// Of a handyman the simulation walks, the broken path items they have mended since the park was opened.
        std::optional<std::uint32_t> items_mended { };
        /// Of a VIP on their visit: their patience and their opinion of the park, each 0..1.
        std::optional<std::pair<float, float>> vip { };

        [[nodiscard]] std::optional<const save::Person&> person() const
        {
            return guest != nullptr ? maybe(&guest->person) : staff != nullptr ? maybe(&staff->person) : std::nullopt;
        }
    };

    /// The tabs of the person, in the order of their slots.
    [[nodiscard]] std::vector<PersonTabInfo> person_tabs(const PersonFacts& facts);

    /// Ids of the controls on a person's pages (InfoControl::id).
    enum class PersonControl : std::uint8_t
    {
        Name,
        Sack, ///< Fires them at once, as the game does
        Warn,
        Wage,        ///< The wage level, 0 to 2
        Train,       ///< Trains them a level up, for its cost
        Costume,     ///< An entertainer's, by its place in the list
        PatrolPaint, ///< Ticked while strokes on the ground add or take away tiles of their patrol area
        PatrolClear, ///< Empties it, and their zone's: the whole park again
        PatrolBrush, ///< How many tiles across a stroke paints, by its place in kPatrolBrushes
        PatrolCopy,  ///< Gives the area to everyone else in their job
        PatrolFill,  ///< Fills the box round the area
        PatrolZone,  ///< The shared zone they are on, by its number; 0 none
        Task,        ///< The first task's tick; the task of flag bit b is this plus b
    };

    [[nodiscard]] constexpr int person_control(PersonControl control, int bit = 0)
    {
        return static_cast<int>(control) + bit;
    }

    /// Tiles across the patrol brush paints, by its choice.
    inline constexpr std::array<int, 3> kPatrolBrushes { 1, 3, 5 };
    /// Shared patrol zones a member of staff can be put on, besides none.
    inline constexpr std::uint32_t kPatrolZones = 8;

    /// Longest name the player may type, as in the game.
    inline constexpr int kLongestName = 34;
    /// Warnings after which a member of staff cannot be warned again.
    inline constexpr int kMostWarnings = 4;
    inline constexpr int kMostTraining = 3; ///< Training levels, as the simulation has them
    /// Colours a uniform can be in.
    inline constexpr std::size_t kUniformColours = 16;
    /// Where the clothes' colours are among a person's palettes: the upper's, then the lower's.
    inline constexpr std::size_t kUpperPalette = 1;

    /// The person's name as the player sees it. Staff are saved with a stand-in for the name of their job, put
    /// in here.
    [[nodiscard]] std::u16string person_name(const PersonFacts& facts, const StringTable& strings);

    /// What the person is doing, in words; empty when it has none.
    [[nodiscard]] std::u16string person_doing(const PersonFacts& facts, const StringTable& strings);

    /// The page of `tab`.
    [[nodiscard]] InfoPage person_page(PersonTab tab, const PersonFacts& facts, const StringTable& strings, const InfoUnits& units);

} // namespace rct3::ui
