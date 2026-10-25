// SPDX-License-Identifier: GPL-3.0-or-later
//
// The lists the park's build menus open: rides, shops and scenery read from the install, offered as the
// park allows. The install is read once, away from the frame, when the park loads; a list asked for before
// that is done waits for it.
//
// Picking a ride placed whole or scenery in a list starts placing it (ride_placing.hpp); other picks build
// nothing yet.
//
// The saved designs of tracked rides are read the same way, from the install's folder of designs and the
// player's own, once the folders are known; a tracked ride's row tells how many it has.
#pragma once

#include "config/game_install.hpp"
#include "park/build_catalogue.hpp"
#include "save/model/design.hpp"
#include "save/model/park.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/build_rows.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/string_table.hpp"
#include "ui/widget.hpp"

#include <filesystem>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::scene
{

    /// A saved design's file and what it says of itself, without its picture.
    struct DesignFile
    {
        std::filesystem::path path;
        save::DesignSummary summary;
    };

    struct BuildMenus
    {
        std::shared_future<std::vector<park::BuildItem>> items;
        park::BuildOffer offer;
        std::optional<ui::Point> size;                       ///< Of the last list resized, for the next one opened
        std::set<std::string, std::less<>> kinds_off;        ///< Object filters turned off; the lists share them
        std::set<std::string, std::less<>> styles_off;       ///< Theme filters turned off
        std::set<std::uint32_t> packs_only;                  ///< Pack filters turned on
        std::optional<park::BuildItem> asked;                ///< Picked by a script, taken as a pick from a list
        bool asked_designs = false;                          ///< The script asks for the saved designs, as a list does
        bool offer_designs = false;                          ///< The last pick offers the ride's saved designs
        std::shared_future<std::vector<DesignFile>> designs; ///< In the order of their folders, then their names
        bool kinds_named = false;                            ///< Whether the strings know each kind's name by its symbol
    };

    /// Starts reading the install's items. `content`: what the park records of the items it offers, if
    /// it was read.
    [[nodiscard]] BuildMenus start_build_menus(const config::GameInstall& install, const save::RequiredContent* content);

    /// Starts reading the saved designs in `folders`, in their order; those that do not read are left out.
    void start_design_scan(BuildMenus& menus, std::vector<std::filesystem::path> folders);

    /// The saved designs for the tracked ride kind `symbol` (as the build lists name it), waiting for them to
    /// be read; none when no reading was started.
    [[nodiscard]] std::vector<DesignFile> designs_for(const BuildMenus& menus, std::string_view symbol);

    /// Opens the list of `item` above `bar`, the tool bar, in the middle of its shown buttons, in place of any
    /// list open before, titled with the help of `button`, the menu button that asked for it. False for an item
    /// that has no list.
    bool open_build_list(BuildMenus& menus, ui::Widget& root, ui::ToolItem item, const ui::Widget& button, const ui::Widget& bar, const ui::StringTable& strings, const ui::Palette& palette, const ui::Currency& currency);

    /// Closes the open list, if one is.
    void close_build_list(ui::Widget& root);

    /// Whether a list is open.
    [[nodiscard]] bool build_list_open(const ui::Widget& root);

    /// Each frame: keeps what the open list changed and shades its items dearer than `cash`; returns the item
    /// picked in it, if one was.
    [[nodiscard]] std::optional<park::BuildItem> update_build_menus(BuildMenus& menus, ui::Widget& root, std::optional<std::int64_t> cash);

} // namespace rct3::scene
