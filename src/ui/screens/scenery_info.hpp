// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a prop placed in the park: a tab that pulls it down at once, one that picks
// its colours when it takes any, and tabs for what some props do, such as a litter bin's state or a sign's
// words, or the pool complex a changing room lets guests into. A plain prop has no page open; the strip then
// only names it.
#pragma once

#include "save/model/world.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class SceneryTab : std::uint8_t
    {
        Destroy, ///< Pulls the prop down; has no page
        Colour,  ///< Picks the colours of the parts that take them
        Bin,     ///< How full a litter bin is and when it was emptied
        Sign,    ///< What a sign says
        Pool,    ///< A changing room's: its pool complex opened or closed, and the ticket price
    };

    /// The ids of the pool tab's controls.
    inline constexpr int kPoolStateControl = 1;  ///< A choice: 0 open, 1 closed
    inline constexpr int kPoolTicketControl = 2; ///< The ticket price, hundredths
    inline constexpr int kMostPoolTicket = 2000;

    /// The pool complex a changing room lets guests into.
    struct PoolComplexFacts
    {
        std::int64_t ticket = 0; ///< Hundredths
        bool open = false;
    };

    struct SceneryTabInfo
    {
        SceneryTab tab = SceneryTab::Destroy;
        int slot = 0;                    ///< Its place on the ring
        std::string_view icon;           ///< Skin
        std::string_view help;           ///< String key
        bool action = false;             ///< Acts at once instead of opening a page
        std::vector<std::size_t> paints; ///< The colour tab's: the colour in each slot it paints, in turn
    };

    /// The prop a page is written about: the asset it is made from and what it does, when it is a bin or a sign.
    struct SceneryFacts
    {
        std::string_view symbol { };
        const save::BinState* bin = nullptr;
        const save::SignState* sign = nullptr;
        std::optional<PoolComplexFacts> pool { }; ///< When it is a changing room
        std::uint32_t colour_slots = 0;           ///< The colour slots its pictures take (bit k: slot k)
        std::array<std::int32_t, 3> colours { };  ///< The colour picked for each slot, of the 32
        float zero_time = 0;                      ///< Where the park's calendar starts, for the dates shown
    };

    /// The colour slots the colour tab paints, in the order of its pots.
    [[nodiscard]] std::vector<std::size_t> scenery_paint_slots(const SceneryFacts& facts);

    /// The tabs of the prop, in the order of their slots.
    [[nodiscard]] std::vector<SceneryTabInfo> scenery_tabs(const SceneryFacts& facts);

    /// The prop's name, from the string named after its asset; empty when there is none.
    [[nodiscard]] std::u16string scenery_name(const SceneryFacts& facts, const StringTable& strings);

    /// The page of `tab`, or the page with no tab open when there is none.
    [[nodiscard]] InfoPage scenery_page(std::optional<SceneryTab> tab, const SceneryFacts& facts, const StringTable& strings);

} // namespace rct3::ui
