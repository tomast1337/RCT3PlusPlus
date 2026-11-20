// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a prop placed in the park: a tab that pulls it down at once, one that picks
// its colours when it takes any, and tabs for what some props do, such as a litter bin's state or a sign's
// words. A plain prop has no page open; the strip then only names it. A changing room's window is laid out as
// an attraction's instead, about the pool complex it lets guests into. An animal house's adds the animals that
// live in it, its keepers and what guests think of its animals.
#pragma once

#include "core/types.hpp"
#include "park/pools/pools.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/animals.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/string_table.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui::screens::info
{

    enum class SceneryTab : u8
    {
        Destroy,  ///< Pulls the prop down; has no page
        Colour,   ///< Picks the colours of the parts that take them
        Bin,      ///< How full a litter bin is and when it was emptied
        Sign,     ///< What a sign says
        Passport, ///< On a passport post: the park's passport gift and the stamps it takes
        // An animal house's:
        HouseDetails,  ///< The species that live in it, and a button that turns the view to it
        HouseFeeding,  ///< How often its keepers fill its troughs
        HouseKeepers,  ///< The keepers who look after it
        HouseThoughts, ///< What guests think of its animals
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
    inline constexpr i32 kPoolTicketControl = 2; ///< The ticket price, hundredths
    inline constexpr i32 kPoolNameControl = 3;   ///< The complex's name, typed
    inline constexpr i32 kPoolDeleteRoomControl = 4;
    inline constexpr i32 kPoolDeleteComplexControl = 5;
    inline constexpr i32 kPoolLocateControl = 6;
    inline constexpr i32 kPoolMechanicControl = 7;
    inline constexpr i32 kPoolFluidControl = 8;       ///< A choice: the cleaning fluid, mildest 0 to harshest 2
    inline constexpr i32 kPoolWaveHeightControl = 9;  ///< The wave machines' wave height setting
    inline constexpr i32 kPoolWaveLengthControl = 10; ///< Their wave length: kLeastWaveFrequency + kMostWaveFrequency less the frequency
    inline constexpr i32 kPoolWaveOnControl = 11;     ///< Seconds they run for
    inline constexpr i32 kPoolWaveEveryControl = 12;  ///< Minutes from one start of theirs to the next
    inline constexpr i32 kPoolInspectionControl = 13; ///< A choice: the inspection interval, in the maintenance page's order
    inline constexpr i32 kPoolSaveControl = 14;
    /// Stamps a passport needs for the gift.
    inline constexpr i32 kPassportStampsControl = 15;
    /// A choice: the gift, by its place in PassportFacts::gifts.
    inline constexpr i32 kPassportGiftControl = 16;
    /// Turns the park's view to an animal house.
    inline constexpr i32 kHouseLocateControl = 17;
    inline constexpr i32 kHouseScheduleControl = 18;
    inline constexpr i32 kMostWaveOn = 600;   ///< Chosen by eye
    inline constexpr i32 kWaveOnStep = 10;    ///< Chosen by eye
    inline constexpr i32 kMostWaveEvery = 30; ///< Chosen by eye: minutes
    inline constexpr i32 kMostPoolTicket = 2000;
    /// The inspection interval a room shows before one is picked for it: every 30 minutes.
    inline constexpr i32 kPoolInspection = 2;

    /// The pool complex a changing room lets guests into, and the room itself.
    struct PoolComplexFacts
    {
        std::u16string name { }; ///< Empty for the default
        usize number = 1;        ///< Of the room among the park's rooms of its kind, from 1, for its name
        units::Money ticket { };
        bool open = false;
        i64 people = 0; ///< In the complex
        usize dirt = 0; ///< How dirty its water is, in six steps, cleanest 0
        u8 fluid = 0;   ///< As kPoolFluidControl's value
        park::pools::PoolWaves waves { };
        bool machines = false; ///< It has wave machines; without, their times cannot be set
        Currency currency { };
        i64 customers = 0;       ///< The room's, so far
        units::Money income { }; ///< Its takings so far
        units::Money refill { }; ///< A full tank of its fluid, which a mechanic's inspection tops up
        f32 reliability = 1;     ///< 0 to 1
        units::Seconds since_inspection { };
        i32 inspection = kPoolInspection;                    ///< As kPoolInspectionControl's value
        std::vector<save::model::ThoughtEntry> thoughts { }; ///< Guests' thoughts about the room
    };

    /// The park's passport rules, which every passport post's window sets.
    struct PassportFacts
    {
        i32 stamps_for_gift = 0;
        i32 most_stamps = 0;
        std::vector<std::string_view> gifts { }; ///< What it may give, by the string keys of their names
        i32 gift = 0;                            ///< Of `gifts`
    };

    /// An animal house, and those who live in it and look after it.
    struct AnimalHouseFacts
    {
        std::vector<std::string_view> species { }; ///< String keys of the names of the species that live in it
        std::vector<std::u16string> keepers { };
        simulation::FeedingSchedule schedule { simulation::FeedingSchedule::Normal };
        Currency currency { }; ///< Of its food's cost
    };

    struct SceneryTabInfo
    {
        SceneryTab tab = SceneryTab::Destroy;
        i32 slot = 0;              ///< Its place on the ring
        std::string_view icon;     ///< Skin
        std::string_view help;     ///< String key
        bool action = false;       ///< Acts at once instead of opening a page
        std::vector<usize> paints; ///< The colour tab's: the colour in each slot it paints, in turn
    };

    /// The prop a page is written about: the asset it is made from and what it does, when it is a bin or a sign.
    struct SceneryFacts
    {
        std::string_view symbol { };
        const save::model::BinState* bin = nullptr;
        const save::model::SignState* sign = nullptr;
        std::optional<PoolComplexFacts> pool { };  ///< When it is a changing room
        std::optional<PassportFacts> passport { }; ///< When it is a passport post
        std::optional<AnimalHouseFacts> house { }; ///< When it is an animal house
        u32 colour_slots = 0;                      ///< The colour slots its pictures take (bit k: slot k)
        std::array<i32, 3> colours { };            ///< The colour picked for each slot, of the 32
        f32 zero_time = 0;                         ///< Where the park's calendar starts, for the dates shown
    };

    /// The colour slots the colour tab paints, in the order of its pots.
    [[nodiscard]] std::vector<usize> scenery_paint_slots(const SceneryFacts& facts);

    /// The tabs of the prop, in the order of their slots.
    [[nodiscard]] std::vector<SceneryTabInfo> scenery_tabs(const SceneryFacts& facts);

    /// The prop's name, from the string named after its asset; empty when there is none.
    [[nodiscard]] std::u16string scenery_name(const SceneryFacts& facts, const StringTable& strings);

    /// The page of `tab`, or the page with no tab open when there is none.
    [[nodiscard]] InfoPage scenery_page(std::optional<SceneryTab> tab, const SceneryFacts& facts, const StringTable& strings);

} // namespace rct3::ui::screens::info
