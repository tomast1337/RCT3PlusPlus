// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a guest or a member of staff: which tabs they have, where each sits on the
// ring, and the lines of each tab's content, written from the person's saved state. Guests have a general
// tab, their thoughts, the rides they went on, what they carry, how they feel and their money; staff have
// the general tab, their thoughts and how they work.
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
    };

    struct PersonTabInfo
    {
        PersonTab tab = PersonTab::General;
        int slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
    };

    /// The tabs of a guest, or of a member of staff when `staff`, in the order of their slots.
    [[nodiscard]] std::vector<PersonTabInfo> person_tabs(bool staff);

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

    /// The person's name as the player sees it. Staff are saved with a stand-in for the name of their job, put
    /// in here.
    [[nodiscard]] std::u16string person_name(const PersonFacts& facts, const StringTable& strings);

    /// What the person is doing, in words; empty when it has none.
    [[nodiscard]] std::u16string person_doing(const PersonFacts& facts, const StringTable& strings);

    /// The page of `tab`.
    [[nodiscard]] InfoPage person_page(PersonTab tab, const PersonFacts& facts, const StringTable& strings, const InfoUnits& units);

} // namespace rct3::ui
