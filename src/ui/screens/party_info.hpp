// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a party of guests going round together, opened from one of its guests:
// the rides its members went on, how they feel together, the money they brought and spent between them, and
// who is in it. A member's row opens that guest. Its leader's own pages give what the party shares with them:
// their favourite ride and their time in the park.
#pragma once

#include "ui/screens/person_info.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class PartyTab : std::uint8_t
    {
        Rides,
        Stats,
        Cash,
        Members,
    };

    struct PartyTabInfo
    {
        PartyTab tab = PartyTab::Members;
        int slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
    };

    /// The party's tabs, in the order of their slots.
    inline constexpr std::array<PartyTabInfo, 4> kPartyTabs { {
        { PartyTab::Rides, 1, "Guest_Rides", "GuestDetails_GroupRidesVisitedHelp" },
        { PartyTab::Stats, 2, "Guest_Stats", "GuestDetails_GroupStatsHelp" },
        { PartyTab::Cash, 3, "Guest_Cash", "GuestDetails_GroupCashHelp" },
        { PartyTab::Members, 4, "Guest_Group", "GuestDetails_GroupMembersHelp" },
    } };

    /// The party a page is written about: each member's facts, the leader first.
    struct PartyFacts
    {
        std::vector<PersonFacts> members { };
        /// The kind of party, by the save's numbering of the GuestGroupType_ names; nothing when not known.
        std::optional<std::int32_t> kind { };
    };

    /// The page of `tab`. On the members page, each member is a button whose id is their place in the members.
    [[nodiscard]] InfoPage party_page(PartyTab tab, const PartyFacts& facts, const StringTable& strings, const InfoUnits& units);

} // namespace rct3::ui
