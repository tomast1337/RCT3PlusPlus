// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's management windows in the park view: opened and closed from the management menu, their pages
// written from the park as it stands, and written again when the player picks a tab and every so often while
// they are open, since the park moves on under them. A row activated in a list of people or attractions asks
// for that one's info panel.
#pragma once

#include "ui/desktop.hpp"
#include "ui/screens/management_pages.hpp"
#include "ui/screens/tool_menus.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace rct3::scene
{

    /// Opens the window of `topic`, or closes it when it is open: the menu item works as a switch.
    void toggle_management_window(ui::Desktop& desktop, ui::ManagementTopic topic, const ui::ManagementFacts& facts, const ui::StringTable& strings, const ui::Palette& palette, const ui::InfoUnits& units);

    /// Whether any management window is open.
    [[nodiscard]] bool management_window_open(const ui::Desktop& desktop);

    /// Closes the management window on top of the others, if any is open; returns whether it closed one.
    bool close_top_management_window(ui::Desktop& desktop);

    /// What the player asked of the management windows this frame.
    struct ManagementAsks
    {
        std::optional<ui::ManagementRow> row; ///< The row activated
        bool replace_broken = false;          ///< Replace every broken path item (ui::kReplaceBrokenButton)
        /// Put every attraction in `listed` in this state (ui::kOpenCloseAllButton)
        std::optional<save::AttractionState> state_all;
        std::vector<ui::ManagementRow> listed;
    };

    /// Writes the open windows' pages again where the player picked a tab or where it is time to, and returns
    /// what the player asked.
    [[nodiscard]] ManagementAsks update_management_windows(ui::Desktop& desktop, const ui::ManagementFacts& facts, const ui::StringTable& strings, const ui::InfoUnits& units);

} // namespace rct3::scene
