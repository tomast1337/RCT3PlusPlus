// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rows of the build lists (build_picker.hpp), made from the items the install offers.
#pragma once

#include "park/build_catalogue.hpp"
#include "strings/ascii.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/build_picker.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/string_table.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// The list a build menu item opens; nothing for items that open none.
    [[nodiscard]] std::optional<park::BuildList> build_list(ToolItem item) noexcept;

    /// The string key of the word for an animal's rarity, from 0 (common) to 1 (endangered).
    [[nodiscard]] std::string_view rarity_text(float rarity) noexcept;

    /// The object filter `item` falls under, by its kind of ride or category of scenery: the name its
    /// pictures and help are known by ("Coaster_Steel"). Empty for items under none.
    [[nodiscard]] std::string_view object_filter(const park::BuildItem& item) noexcept;

    /// Whether `list` offers a filter for each theme: the lists of rides placed whole, shops and scenery.
    [[nodiscard]] bool has_theme_filters(park::BuildList list) noexcept;

    /// Every theme the items belong to, in the order the install numbers them.
    [[nodiscard]] std::vector<PickerTheme> picker_themes(std::span<const park::BuildItem> items);

    /// What a large row says under a piece of scenery's name: how it sits on the grid, or its footprint.
    [[nodiscard]] std::u16string placement_text(const park::BuildItem& item, const StringTable& strings);

    /// The rows of `list`: the items in it that `offer` lets the park build, those of a set under a heading
    /// named after the set, each row in name order. A row's number is the item's index in `items`. A tracked
    /// ride's row tells how many saved designs `design_counts` gives its kind, by symbol, when it has any.
    [[nodiscard]] std::vector<PickerRow> picker_rows(park::BuildList list, std::span<const park::BuildItem> items, const park::BuildOffer& offer, const StringTable& strings, const Currency& currency, const std::map<std::string, std::size_t, strings::LessIgnoringCase>& design_counts = { });

} // namespace rct3::ui
