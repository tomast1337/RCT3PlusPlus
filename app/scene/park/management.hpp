// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's management windows in the park view: opened and closed from the management menu, their pages
// written from the park as it stands, and written again when the player picks a tab and every so often while
// they are open, since the park moves on under them. A row activated in a list of people or attractions asks
// for that one's info panel.
#pragma once

#include "ui/desktop.hpp"
#include "ui/screens/management_pages.hpp"
#include "ui/screens/staff_roster.hpp"
#include "ui/screens/tool_menus.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rct3::scene::park
{

    /// Opens the window of `topic`, or closes it when it is open: the menu item works as a switch.
    void toggle_management_window(ui::Desktop& desktop, ui::screens::ManagementTopic topic, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::Palette& palette, const ui::screens::InfoUnits& units);

    /// Opens the window of `topic` at tab `tab`, or turns the open one to it.
    void open_management_window(ui::Desktop& desktop, ui::screens::ManagementTopic topic, std::size_t tab, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::Palette& palette, const ui::screens::InfoUnits& units);

    /// Whether any management window is open.
    [[nodiscard]] bool management_window_open(const ui::Desktop& desktop);

    /// Closes the management window on top of the others, if any is open; returns whether it closed one.
    bool close_top_management_window(ui::Desktop& desktop);

    /// What the player asked of the management windows this frame.
    struct ManagementAsks
    {
        std::optional<ui::screens::ManagementRow> row; ///< The row activated
        bool replace_broken = false;                   ///< Replace every broken path item (ui::screens::kReplaceBrokenButton)
        std::optional<bool> park_open;                 ///< Open the park, or close it (ui::screens::kOpenCloseParkButton)
        bool centre_entrance = false;                  ///< Take the view to the next entrance (ui::screens::kCentreEntranceButton)
        bool buy_land = false;                         ///< Take out the tool buying land (ui::screens::kBuyLandButton)
        bool buy_rights = false;                       ///< Or building rights (ui::screens::kBuyRightsButton)
        std::optional<bool> borrow;                    ///< Borrow a step of loan, or repay one (ui::screens::kBorrowButton, kRepayButton)
        bool export_ledger = false;                    ///< Write the ledger out (ui::screens::kExportLedgerButton)
        /// Put every attraction in `listed` in this state (ui::screens::kOpenCloseAllButton)
        std::optional<save::model::AttractionState> state_all;
        std::vector<ui::screens::ManagementRow> listed;
        ui::screens::RosterAsks staff;               ///< Of the staff window
        std::optional<std::int64_t> research_budget; ///< A month, in hundredths (ui::screens::kResearchBudgetControl)
        /// A research area's priority, from 0 to 1 (ui::screens::kFirstPriorityControl)
        std::optional<std::pair<std::size_t, float>> research_priority;
        /// Focus research on this item, or end the focus when it holds none (ui::screens::kFocusButton, kUnfocusButton)
        std::optional<std::optional<std::uint64_t>> research_focus;
    };

    /// Writes the open windows' pages again where the player picked a tab or where it is time to, and returns
    /// what the player asked.
    [[nodiscard]] ManagementAsks update_management_windows(ui::Desktop& desktop, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::screens::InfoUnits& units);

} // namespace rct3::scene::park
