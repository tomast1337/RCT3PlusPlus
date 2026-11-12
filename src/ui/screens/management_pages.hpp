// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the park's management windows show: for each window its icon, name, size and tabs, and the page of each
// tab written from the park as it stands (its saved model, its running state, its people and its attractions).
//
// Nothing is made up. A value the park does not keep, or that only the simulation would work out, shows as a
// dash; a page whose content needs what is not built yet says so in its note.
#pragma once

#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "simulation/objectives.hpp"
#include "simulation/park/state.hpp"
#include "simulation/research.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/management_window.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui::screens
{

    /// The management windows, in the order the menu offers them from its button outwards, most used first.
    enum class ManagementTopic : std::uint8_t
    {
        ParkOperations,
        Finances,
        Staff,
        Guests,
        Attractions,
        Objectives,
        Research,
        Challenges,
        Count_,
    };
    inline constexpr std::size_t kManagementTopicCount = static_cast<std::size_t>(ManagementTopic::Count_);

    /// The window the management menu's `item` opens; none for other items.
    [[nodiscard]] std::optional<ManagementTopic> management_topic(ToolItem item);

    struct ManagementLayout
    {
        std::string_view icon;  ///< Skin in the frame's corner: the menu item's
        std::string_view title; ///< String key
        float width = 0;        ///< With the frame
        float height = 0;
    };

    [[nodiscard]] ManagementLayout management_layout(ManagementTopic topic);

    /// The tabs of `topic`'s window, with their names.
    [[nodiscard]] std::vector<ManagementTab> management_tabs(ManagementTopic topic, const StringTable& strings, const InfoUnits& units);

    /// What the running park shows of a worker that their save does not hold: the colours they are drawn in
    /// (upper then lower clothes), the tiles of their patrol area (0 for none, the whole park) and whether the
    /// player is painting it.
    struct StaffLook
    {
        std::array<std::uint8_t, 2> colours { };
        std::size_t patrol_tiles = 0;
        bool painting_patrol = false;
    };

    /// What an attraction has taken since the park was opened, which its saved totals do not hold yet.
    struct AttractionTakings
    {
        std::int64_t income = 0; ///< Hundredths
        std::int64_t customers = 0;
    };

    /// What the pages are written from. Any of it may be missing; its values then show as dashes.
    struct ManagementFacts
    {
        const save::model::ParkModel* park = nullptr; ///< As saved
        const simulation::park::ParkState* state = nullptr;
        const save::model::PeopleModel* people = nullptr;
        const save::model::RideModel* rides = nullptr;
        std::uint32_t broken_items = 0;                     ///< Path items vandals have broken
        std::int64_t replace_cost = 0;                      ///< To replace them all at once, hundredths
        std::span<const simulation::GoalReading> goals { }; ///< By place in the park's goals, as last checked
        bool animal_houses = false;                         ///< Whether the park has any, which keepers are hired to
        std::span<const StaffLook> staff_looks { };         ///< By place in the park's staff; may be short
        float zero_time = 0;                                ///< Where the park's calendar starts, for hire dates
        const simulation::Research* research = nullptr;     ///< As it runs
        /// What is left to invent, once the build lists say what each item is.
        std::optional<std::span<const simulation::ResearchItem>> research_left { };
        /// The string key of the name of a research item (simulation::ResearchItem::id); empty when unknown.
        std::function<std::string_view(std::uint64_t)> research_name { };
        /// The item picked on the research window's list of what may come next.
        std::optional<std::uint64_t> research_pick { };
        /// The line the finances ledger shows in its last column (kLedgerLineControl).
        std::size_t ledger_line = 0;
        /// What the attraction with saved id `id` (save::model::AttractionCommon::id) has taken since the park was
        /// opened; nothing when not given.
        std::function<AttractionTakings(std::uint64_t)> takings { };
    };

    /// The park operations window's tab of the awards won and fines received.
    inline constexpr std::size_t kAwardsTab = 3;

    /// The park operations window's first page's buttons, by their place among its buttons: opening or closing
    /// the park by the place of its choice (open first), taking the view to the next of its entrances, and
    /// buying land or building rights.
    inline constexpr std::size_t kOpenCloseParkButton = 0;
    inline constexpr std::size_t kCentreEntranceButton = 1;
    inline constexpr std::size_t kBuyLandButton = 2;
    inline constexpr std::size_t kBuyRightsButton = 3;

    /// The button on the park operations window's first page that replaces every broken path item at its full
    /// price, by its place among the page's buttons. Our own: the game has no way to mend them.
    inline constexpr std::size_t kReplaceBrokenButton = 4;

    /// The finances window's pages. Ours are on tabs in this order; the original's, which the player may ask for
    /// (InfoUnits::original_finances), are the summary and the graphs.
    enum class FinancePage : std::uint8_t
    {
        Summary, ///< The money, and the ledger's last months a column each
        Ledger,  ///< Every month the park keeps, a row each
        Graphs,
        Attractions, ///< What each attraction has taken
    };

    /// The page on tab `tab` of the finances window, ours or the `original` layout's.
    [[nodiscard]] FinancePage finance_page(std::size_t tab, bool original);

    /// The finances ledger's control picking the line its last column shows, by index of the summary's lines.
    inline constexpr int kLedgerLineControl = 0;

    /// The finances window's summary's buttons, by their place among its buttons: borrowing a step more and
    /// repaying a step of the loan (see simulation::park::change_loan).
    inline constexpr std::size_t kBorrowButton = 0;
    inline constexpr std::size_t kRepayButton = 1;

    /// The research window's controls (InfoControl::id): the monthly budget, then the priority of each research
    /// area (simulation::ResearchArea) in order, from 0 to 100.
    inline constexpr int kResearchBudgetControl = 0;
    inline constexpr int kFirstPriorityControl = 1;
    /// The research budget's step and most, in hundredths. Chosen by eye: steps of 50.00, up to five times the
    /// 200.00 a month scenarios usually set aside.
    inline constexpr int kResearchBudgetStep = 5000;
    inline constexpr int kMostResearchBudget = 100000;
    /// The research window's tabs after the priorities: what has been invented and when, and what may come next.
    inline constexpr std::size_t kInventedTab = 1;
    inline constexpr std::size_t kComingTab = 2;
    /// The buttons under the list of what may come next: focusing research on the item picked, and ending the
    /// focus.
    inline constexpr std::size_t kFocusButton = 0;
    inline constexpr std::size_t kUnfocusButton = 1;
    /// Items listed of each area as what may come next. Chosen by eye: a few to pick from without a long list.
    inline constexpr std::size_t kComingPerArea = 3;

    /// The button on each page of the attractions window that puts every listed attraction in the state of the
    /// choice picked, by its place among the page's buttons, and those states by the place of their choice.
    inline constexpr std::size_t kOpenCloseAllButton = 0;
    inline constexpr std::array<save::model::AttractionState, 2> kOpenCloseAllStates = { save::model::AttractionState::Open, save::model::AttractionState::Closed };

    /// The page of tab `tab` of `topic`'s window.
    [[nodiscard]] ManagementPage management_page(ManagementTopic topic, std::size_t tab, const ManagementFacts& facts, const StringTable& strings, const InfoUnits& units);

    /// What a table row is about: a guest, a member of staff, a ride or a stall, by its index in the park's
    /// people or attractions. Rows carry it packed in their id.
    struct ManagementRow
    {
        enum class Kind : std::uint8_t
        {
            Guest,
            Staff,
            Ride,
            Stall,
            None, ///< A row about nothing that has an info panel
        };
        Kind kind = Kind::None;
        std::size_t index = 0;

        friend bool operator==(const ManagementRow&, const ManagementRow&) = default;
    };

    /// The park's name as shown: its text when the name is a string key, else the name with each "#ME#" standing
    /// for the scenario's name; the scenario's name when the park has none.
    [[nodiscard]] std::u16string park_name(const save::model::Park& park, const StringTable& strings);

    [[nodiscard]] std::size_t management_row_id(ManagementRow row);
    [[nodiscard]] ManagementRow management_row(std::size_t id);

} // namespace rct3::ui::screens
