// SPDX-License-Identifier: GPL-3.0-or-later
//
// Carried items: things a guest holds or wears (food, drinks, balloons, hats ...), and the extras that can
// be added to some of them (sauces, toppings ...).
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// An optional addition to a carried item. Structure of 28 bytes.
    struct CarriedItemExtra
    {
        std::uint32_t unknown_00 = 0; ///< Zero in every file seen.
        std::string_view name_text;   ///< "txt" item
        std::string_view icon;        ///< "gsi" item
        std::uint32_t cost = 0;       ///< In hundredths, to the stall for each item it goes on at the usual amount
        /// What it adds to the item's food, drink and nausea at the usual amount (values around +-0.1 and 0.3).
        std::array<float, 3> effects { };
    };

    struct CarriedItemExtraEntry
    {
        std::string_view name; ///< Symbol name without the tag
        std::optional<CarriedItemExtra> extra;
    };

    /// A carried item. Structure of 156 bytes followed by the extras it allows. Most scalar fields are
    /// unconfirmed: they are exposed as the values found, with the value range seen noted.
    struct CarriedItem
    {
        std::uint32_t unknown_00 = 0; ///< 1 in every file seen
        std::uint32_t unknown_04 = 0; ///< 0 to 2
        std::uint32_t unknown_08 = 0; ///< 0 or 2

        std::string_view name_text;   ///< "txt" item, singular
        std::string_view plural_text; ///< "txt" item
        std::string_view icon;        ///< "gsi" item
        /// Two "shs" items. Both name the same shape in nearly every item; both are empty in some (worn items).
        std::array<std::string_view, 2> shapes;

        float unknown_20 = 0; ///< 0, 40, 80, 200 or 500 seen
        /// "cid" item that is left over after the item is used up, empty when nothing is.
        std::string_view remains;

        std::uint32_t flags = 0;      ///< Bit mask, 21 distinct values seen, none of them explained
        std::int32_t unknown_2c = 0;  ///< -1 in most items, otherwise 3, 12 or 48
        std::uint32_t unknown_30 = 0; ///< 1, 2, 4, 6, 8, 16 or 36 seen
        /// How much it feeds, quenches and sickens: food and drink -0.8 to 0.8 seen, nausea 0 in nearly every
        /// item and up to 50 in a few.
        std::array<float, 3> effects { };
        std::uint32_t cost = 0; ///< In hundredths, to the stall for each one sold
        /// What guests will pay for it, in hundredths: in mild weather, in the cold and in the heat (ice creams
        /// are worth most in the third, hot drinks in the second).
        std::array<std::uint32_t, 3> worth { };
        float unknown_50 = 0;         ///< 0, or 0.01
        std::int32_t unknown_54 = 0;  ///< -1 in most items, otherwise 1 or 2
        std::uint32_t unknown_58 = 0; ///< 0, or 7 in one item

        /// "ced" items this item can be combined with, in order.
        std::vector<std::string_view> extras;

        std::uint32_t unknown_70 = 0;   ///< 0 or 1
        std::string_view texture;       ///< "ftx" item, empty in most items
        std::array<float, 2> scale { }; ///< 0.6 to 1.5 seen, 1.0 in nearly every item. Unconfirmed as a scale.
        std::uint32_t unknown_80 = 0;   ///< 0, 16 or 32
        /// Four optional strings. They look like names of model parts or texture variants for worn items.
        std::array<std::string_view, 4> part_names;
        std::uint32_t unknown_94 = 0; ///< 0 or 1
        std::string_view style_name;  ///< Optional string, such as the name of a colour set
    };

    struct CarriedItemEntry
    {
        std::string_view name;
        std::optional<CarriedItem> item; ///< Nothing when the structure or its extras are unreadable
    };

    /// Carried items of one part of an overlay: pointer symbols tagged "cid". Views point into the overlay.
    [[nodiscard]] std::vector<CarriedItemEntry> read_carried_items(const Overlay& overlay, const Part& part);

    /// Carried item extras of one part of an overlay: pointer symbols tagged "ced".
    [[nodiscard]] std::vector<CarriedItemExtraEntry> read_carried_item_extras(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
