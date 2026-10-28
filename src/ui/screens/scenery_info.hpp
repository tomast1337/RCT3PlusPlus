// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a prop placed in the park: a tab that pulls it down at once, one that picks
// its colours when it takes any, and tabs for what some props do, such as a litter bin's state or a sign's
// words. A plain prop has no page open; the strip then only names it. A changing room's window is laid out as
// an attraction's instead, about the pool complex it lets guests into.
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
        // A changing room's, laid out as an attraction's window about its pool complex:
        RoomDetails,     ///< The complex's name, pulling down the room or the complex, and finding it
        RoomMaintenance, ///< Its upkeep
        RoomMode,        ///< The complex opened or closed
        RoomFinances,    ///< The ticket price
        RoomCustomers,   ///< The people in the complex
    };

    /// The ids of a changing room's controls.
    inline constexpr int kPoolStateControl = 1;  ///< A choice: 0 open, 1 closed
    inline constexpr int kPoolTicketControl = 2; ///< The ticket price, hundredths
    inline constexpr int kPoolNameControl = 3;   ///< The complex's name, typed
    inline constexpr int kPoolDeleteRoomControl = 4;
    inline constexpr int kPoolDeleteComplexControl = 5;
    inline constexpr int kPoolLocateControl = 6;
    inline constexpr int kPoolMechanicControl = 7;
    inline constexpr int kPoolFluidControl = 8; ///< A choice: the cleaning fluid, mildest 0 to harshest 2
    inline constexpr int kMostPoolTicket = 2000;

    /// The pool complex a changing room lets guests into.
    struct PoolComplexFacts
    {
        std::u16string name { }; ///< Empty for the default
        std::int64_t ticket = 0; ///< Hundredths
        bool open = false;
        std::int64_t people = 0; ///< In the complex
        std::size_t dirt = 0;    ///< How dirty its water is, in six steps, cleanest 0
        std::uint8_t fluid = 0;  ///< As kPoolFluidControl's value
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
