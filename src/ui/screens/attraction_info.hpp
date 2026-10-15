// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about an attraction: which tabs it has, where each sits on the ring, and the
// lines of each tab's content, written from the attraction's saved state. Every kind of attraction has the
// view, finances, thoughts, open-or-close and customers tabs; rides add vehicles, test results, operating
// mode and maintenance, tracked rides construction and graphs, and stalls their items and extras.
//
// A tab whose content is not built yet shows only its title.
#pragma once

#include "save/model/rides.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class AttractionKind : std::uint8_t
    {
        Tracked,
        Flat,
        Facility, ///< Toilets, cash machines and the like
        Stall,
    };

    enum class AttractionTab : std::uint8_t
    {
        View,
        Finances,
        Thoughts,
        State, ///< Open, closed or testing
        Customers,
        Vehicles,
        Tests,
        Mode,
        Maintenance,
        Construction,
        Graphs,
        Items,
        Extras,
    };

    struct AttractionTabInfo
    {
        AttractionTab tab = AttractionTab::View;
        int slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
    };

    /// The tabs of an attraction of `kind` in the state `state`, in the order of their slots.
    [[nodiscard]] std::vector<AttractionTabInfo> attraction_tabs(AttractionKind kind, save::AttractionState state);

    /// The kind of `ride`.
    [[nodiscard]] AttractionKind attraction_kind(const save::Ride& ride);

    /// The attraction's name as the player sees it: the name they gave it, a named string, or the name of its
    /// kind and its number.
    [[nodiscard]] std::u16string attraction_name(const save::AttractionCommon& attraction, const StringTable& strings);

    /// The word for a rating of `value` (excitement, intensity or nausea): low, medium, high and so on.
    [[nodiscard]] std::u16string rating_word(float value, const StringTable& strings);

    enum class DistanceUnits : std::uint8_t
    {
        Metric,   ///< km/h and metres
        Imperial, ///< mph and feet
        SI,       ///< m/s and metres
    };

    struct InfoUnits
    {
        Currency currency;
        DistanceUnits distance = DistanceUnits::Metric;
    };

    /// The attraction a page is written about: its common part, and the ride or the stall it is.
    struct AttractionFacts
    {
        const save::AttractionCommon* common = nullptr;
        const save::Ride* ride = nullptr;
        const save::Stall* stall = nullptr;
    };

    [[nodiscard]] InfoPage attraction_page(AttractionTab tab, const AttractionFacts& facts, const StringTable& strings, const InfoUnits& units);

} // namespace rct3::ui
