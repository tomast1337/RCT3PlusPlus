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

#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <cstdint>
#include <string>
#include <string_view>
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

        [[nodiscard]] const save::Person* person() const
        {
            return guest != nullptr ? &guest->person : staff != nullptr ? &staff->person : nullptr;
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
        Wage,    ///< The wage level, 0 to 2
        Costume, ///< An entertainer's, by its place in the list
        Task,    ///< The first task's tick; the task of flag bit b is this plus b
    };

    [[nodiscard]] constexpr int person_control(PersonControl control, int bit = 0)
    {
        return static_cast<int>(control) + bit;
    }

    /// Longest name the player may type, as in the game.
    inline constexpr int kLongestName = 34;
    /// Warnings after which a member of staff cannot be warned again.
    inline constexpr int kMostWarnings = 4;
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
