// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a prop placed in the park: a tab that pulls it down at once, one that picks
// its colours when it takes any, and tabs for what some props do, such as a litter bin's state or a sign's
// words. A plain prop has no page open; the strip then only names it. A changing room's window is laid out as
// an attraction's instead, about the pool complex it lets guests into.
#pragma once

#include "park/pools/pools.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui::screens
{

    enum class SceneryTab : std::uint8_t
    {
        Destroy, ///< Pulls the prop down; has no page
        Colour,  ///< Picks the colours of the parts that take them
        Bin,     ///< How full a litter bin is and when it was emptied
        Sign,    ///< What a sign says
        // A changing room's, laid out as an attraction's window about the room and its pool complex:
        RoomDetails,      ///< The room's and the complex's names, its state, pulling them down and finding the room
        RoomFinances,     ///< The ticket price and the room's takings
        RoomThoughts,     ///< What guests think of it
        RoomState,        ///< Has no page: offers opening and closing the complex
        RoomMode,         ///< How the complex's wave machines run
        RoomConstruction, ///< Has no page: opens the pool panel
        RoomCustomers,    ///< The room's customers
        RoomMaintenance,  ///< Its upkeep and its pool's water
    };

    /// The ids of a changing room's controls.
    inline constexpr int kPoolTicketControl = 2; ///< The ticket price, hundredths
    inline constexpr int kPoolNameControl = 3;   ///< The complex's name, typed
    inline constexpr int kPoolDeleteRoomControl = 4;
    inline constexpr int kPoolDeleteComplexControl = 5;
    inline constexpr int kPoolLocateControl = 6;
    inline constexpr int kPoolMechanicControl = 7;
    inline constexpr int kPoolFluidControl = 8;       ///< A choice: the cleaning fluid, mildest 0 to harshest 2
    inline constexpr int kPoolWaveHeightControl = 9;  ///< The wave machines' wave height setting
    inline constexpr int kPoolWaveLengthControl = 10; ///< Their wave length: kLeastWaveFrequency + kMostWaveFrequency less the frequency
    inline constexpr int kPoolWaveOnControl = 11;     ///< Seconds they run for
    inline constexpr int kPoolWaveEveryControl = 12;  ///< Minutes from one start of theirs to the next
    inline constexpr int kPoolInspectionControl = 13; ///< A choice: the inspection interval, in the maintenance page's order
    inline constexpr int kPoolSaveControl = 14;
    inline constexpr int kMostWaveOn = 600;   ///< Chosen by eye
    inline constexpr int kWaveOnStep = 10;    ///< Chosen by eye
    inline constexpr int kMostWaveEvery = 30; ///< Chosen by eye: minutes
    inline constexpr int kMostPoolTicket = 2000;
    /// The inspection interval a room shows before one is picked for it: every 30 minutes.
    inline constexpr int kPoolInspection = 2;

    /// The pool complex a changing room lets guests into, and the room itself.
    struct PoolComplexFacts
    {
        std::u16string name { }; ///< Empty for the default
        std::size_t number = 1;  ///< Of the room among the park's rooms of its kind, from 1, for its name
        std::int64_t ticket = 0; ///< Hundredths
        bool open = false;
        std::int64_t people = 0; ///< In the complex
        std::size_t dirt = 0;    ///< How dirty its water is, in six steps, cleanest 0
        std::uint8_t fluid = 0;  ///< As kPoolFluidControl's value
        park::pools::PoolWaves waves { };
        bool machines = false; ///< It has wave machines; without, their times cannot be set
        Currency currency { };
        std::int64_t customers = 0;                          ///< The room's, so far
        std::int64_t income = 0;                             ///< Its takings so far, hundredths
        std::int64_t refill = 0;                             ///< A full tank of its fluid, which a mechanic's inspection tops up, hundredths
        float reliability = 1;                               ///< 0 to 1
        float since_inspection = 0;                          ///< Seconds
        int inspection = kPoolInspection;                    ///< As kPoolInspectionControl's value
        std::vector<save::model::ThoughtEntry> thoughts { }; ///< Guests' thoughts about the room
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
        const save::model::BinState* bin = nullptr;
        const save::model::SignState* sign = nullptr;
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

} // namespace rct3::ui::screens
