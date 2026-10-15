// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rows of the build lists (build_picker.hpp), made from the items the install offers.
#pragma once

#include "park/build_catalogue.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/build_picker.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/string_table.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// The list a build menu item opens; nothing for items that open none.
    [[nodiscard]] std::optional<park::BuildList> build_list(ToolItem item) noexcept;

    /// What a large row says under a piece of scenery's name: how it sits on the grid, or its footprint.
    [[nodiscard]] std::u16string placement_text(const park::BuildItem& item, const StringTable& strings);

    /// The rows of `list`: the items in it that `offer` lets the park build, those of a set under a heading
    /// named after the set, each row in name order. A row's number is the item's index in `items`.
    [[nodiscard]] std::vector<PickerRow> picker_rows(park::BuildList list, std::span<const park::BuildItem> items, const park::BuildOffer& offer, const StringTable& strings, const Currency& currency);

} // namespace rct3::ui
