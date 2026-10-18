// SPDX-License-Identifier: GPL-3.0-or-later
//
// The lists the park's build menus open: rides, shops and scenery read from the install, offered as the
// park allows. The install is read once, away from the frame, when the park loads; a list asked for before
// that is done waits for it.
//
// Picking a ride placed whole in a list starts placing it (ride_placing.hpp); other picks build nothing yet.
#pragma once

#include "config/game_install.hpp"
#include "park/build_catalogue.hpp"
#include "save/model/park.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/build_rows.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/string_table.hpp"
#include "ui/widget.hpp"

#include <future>
#include <optional>
#include <vector>

namespace rct3::scene
{

    struct BuildMenus
    {
        std::shared_future<std::vector<park::BuildItem>> items;
        park::BuildOffer offer;
        bool large = true;             ///< Rows of every list are shown large; the lists share it
        bool grid = true;              ///< Items of every list are shown as a grid of icons
        std::optional<ui::Point> size; ///< Of the last list resized, for the next one opened
    };

    /// Starts reading the install's items. `content`: what the park records of the items it offers, if
    /// it was read.
    [[nodiscard]] BuildMenus start_build_menus(const config::GameInstall& install, const save::RequiredContent* content);

    /// Opens the list of `item` beside `button`, the menu button that asked for it, in place of any list
    /// open before, titled with the button's help. False for an item that has no list.
    bool open_build_list(BuildMenus& menus, ui::Widget& root, ui::ToolItem item, const ui::Widget& button, const ui::StringTable& strings, const ui::Currency& currency);

    /// Each frame: keeps what the open list changed and shades its items dearer than `cash`; returns the item
    /// picked in it, if one was.
    [[nodiscard]] std::optional<park::BuildItem> update_build_menus(BuildMenus& menus, ui::Widget& root, std::optional<std::int64_t> cash);

} // namespace rct3::scene
