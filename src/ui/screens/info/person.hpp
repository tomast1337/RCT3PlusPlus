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
#include "core/types.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/components.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/string_table.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ui::screens::info
{

    enum class PersonTab : u8
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
        Party,   ///< Guests: opens the window of the party they go round with
    };

    struct PersonTabInfo
    {
        PersonTab tab = PersonTab::General;
        i32 slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
        usize pots = 0;        ///< Of a paint tab: the clothes it colours, upper first
    };

    /// What a member of staff the simulation walks is about now, which their status line says rather than the state
    /// the park was saved in.
    struct StaffActivity
    {
        simulation::StaffJob job = simulation::StaffJob::None;
        std::optional<bool> ride_broken { }; ///< Of a mechanic bound for a ride or at it: whether it has broken down
        bool showing = false;                ///< An entertainer in the middle of a show
    };

    /// The person a page is written about, the guest or the member of staff they are, and the park's people and
    /// attractions their references lead to.
    struct PersonFacts
    {
        const save::model::Guest* guest = nullptr;
        const save::model::Staff* staff = nullptr;
        const save::model::PeopleModel* people = nullptr;
        const save::model::RideModel* rides = nullptr;
        std::span<const save::model::PlacedObject> objects { }; ///< The park's placed objects, for thoughts about path items
        /// Staff the simulation walks: the tiles of their patrol area (0 for none, the whole park), and whether
        /// clicks on the ground paint it. Empty for others.
        std::optional<usize> patrol_tiles { };
        bool painting_patrol = false;
        i32 patrol_brush = 0;      ///< Of kPatrolBrushes
        u32 patrol_zone = 0;       ///< The shared zone they are on, 0 none
        usize uncovered_paths = 0; ///< Path tiles no one in their job has in a patrol, when any has one
        /// Of a handyman the simulation walks, the broken path items they have mended since the park was opened.
        std::optional<u32> items_mended { };
        std::optional<StaffActivity> activity { };
        f32 zero_time = 0; ///< Where the park's calendar starts, for the day staff were hired
        /// Of staff: an hour of their pay, and what their next level of training costs (nothing once fully trained).
        std::optional<units::Money> wage { };
        units::Money training_cost;
        /// Of a VIP on their visit: their patience and their opinion of the park, each 0..1.
        std::optional<std::pair<f32, f32>> vip { };
        /// Of a guest: the stamps in their passport and whether they have the park's gift for them, with the gift's
        /// name key and the stamps it takes, and whether the park has any passport post.
        struct Passport
        {
            std::vector<InfoStamp> stamps { };
            bool gift = false;
            std::string_view gift_name { };
            usize stamps_for_gift = 0;
            bool posts = false;
        };
        Passport passport { };

        [[nodiscard]] std::optional<const save::model::Person&> person() const
        {
            return guest != nullptr ? core::maybe(&guest->person) : staff != nullptr ? core::maybe(&staff->person) : std::nullopt;
        }
    };

    /// The tabs of the person, in the order of their slots.
    [[nodiscard]] std::vector<PersonTabInfo> person_tabs(const PersonFacts& facts);

    /// Ids of the controls on a person's pages (InfoControl::id).
    enum class PersonControl : u8
    {
        Name,
        Sack, ///< Fires them at once, as the game does
        Warn,
        PickUp,      ///< Hands them to the pointer, to be put down elsewhere
        Wage,        ///< The wage level, 0 to 2
        Train,       ///< Trains them a level up, for its cost
        Costume,     ///< An entertainer's, by its place in the list
        PatrolPaint, ///< Value 1 starts painting their patrol area, 0 stops it (the staff list asks for it)
        PatrolClear, ///< Empties it, and their zone's: the whole park again
        PatrolBrush, ///< How many tiles across a stroke paints, by its place in kPatrolBrushes
        PatrolCopy,  ///< Gives the area to everyone else in their job
        PatrolFill,  ///< Fills the box round the area
        PatrolZone,  ///< The shared zone they are on, by its number; 0 none
        PatrolEdit,  ///< Turns painting their patrol area on or off
        ToConsole,   ///< Turns posting their new thoughts as messages on or off
        Highlight,   ///< Turns their name floating over them on or off, shown while the park bar's highlight is on
        Task,        ///< The first task's tick; the task of flag bit b is this plus b
    };

    [[nodiscard]] constexpr i32 person_control(PersonControl control, i32 bit = 0)
    {
        return static_cast<i32>(control) + bit;
    }

    /// Tiles across the patrol brush paints, by its choice.
    inline constexpr std::array<i32, 3> kPatrolBrushes { 1, 3, 5 };
    /// Shared patrol zones a member of staff can be put on, besides none.
    inline constexpr u32 kPatrolZones = 8;

    /// Longest name the player may type, as in the game.
    inline constexpr i32 kLongestName = 34;
    /// Warnings after which a member of staff cannot be warned again.
    inline constexpr i32 kMostWarnings = 4;
    inline constexpr i32 kMostTraining = 3; ///< Training levels, as the simulation has them
    /// Colours a uniform can be in.
    inline constexpr usize kUniformColours = 16;
    /// Where the clothes' colours are among a person's palettes: the upper's, then the lower's.
    inline constexpr usize kUpperPalette = 1;

    /// The person's name as the player sees it. Staff are saved with a stand-in for the name of their job, put
    /// in here.
    [[nodiscard]] std::u16string person_name(const PersonFacts& facts, const StringTable& strings);

    /// What the person is doing, in words; empty when it has none.
    [[nodiscard]] std::u16string person_doing(const PersonFacts& facts, const StringTable& strings);

    /// The Cash tab's lines about guests `who`, each amount summed over them: one guest's money or a party's.
    void cash_lines(InfoPage& page, std::span<const PersonFacts> who, const StringTable& strings, const InfoUnits& units);

    [[nodiscard]] InfoPage person_page(PersonTab tab, const PersonFacts& facts, const StringTable& strings, const InfoUnits& units);

} // namespace rct3::ui::screens::info
