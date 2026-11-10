// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_management.hpp"

#include "ui/game_style.hpp"

#include <array>
#include <format>
#include <string>

namespace rct3::scene
{

    namespace
    {
        /// Seconds between rewrites of an open window's page: often enough for the date and the money to
        /// follow the park.
        constexpr float kRefresh = 1;

        std::string window_name(ui::screens::ManagementTopic topic)
        {
            return std::string(ui::screens::management_layout(topic).title);
        }

        std::optional<ui::screens::ManagementWindow&> find_window(const ui::Desktop& desktop, ui::screens::ManagementTopic topic)
        {
            return maybe(dynamic_cast<ui::screens::ManagementWindow*>(as_pointer(desktop.root().find(window_name(topic)))));
        }

        /// The staff window, which keeps a list of its own rather than tabs of pages.
        std::optional<ui::screens::StaffRoster&> find_roster(const ui::Desktop& desktop)
        {
            return maybe(dynamic_cast<ui::screens::StaffRoster*>(as_pointer(desktop.root().find(window_name(ui::screens::ManagementTopic::Staff)))));
        }

        void show(ui::screens::ManagementWindow& window, ui::screens::ManagementTopic topic, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::screens::InfoUnits& units)
        {
            window.show_page(ui::screens::management_page(topic, window.tab(), facts, strings, units));
        }
    } // namespace

    void toggle_management_window(ui::Desktop& desktop, ui::screens::ManagementTopic topic, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::Palette& palette, const ui::screens::InfoUnits& units)
    {
        if (auto open = find_window(desktop, topic))
        {
            open->close();
            return;
        }
        if (auto open = find_roster(desktop); open && topic == ui::screens::ManagementTopic::Staff)
        {
            open->close();
            return;
        }
        open_management_window(desktop, topic, 0, facts, strings, palette, units);
    }

    void open_management_window(ui::Desktop& desktop, ui::screens::ManagementTopic topic, std::size_t tab, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::Palette& palette, const ui::screens::InfoUnits& units)
    {
        if (auto open = find_window(desktop, topic))
        {
            open->open_tab(tab);
            show(*open, topic, facts, strings, units);
            return;
        }
        const ui::screens::ManagementLayout layout = ui::screens::management_layout(topic);
        if (topic == ui::screens::ManagementTopic::Staff)
        {
            if (find_roster(desktop))
                return;
            auto& roster = desktop.root().add<ui::screens::StaffRoster>(window_name(topic), ui::game_skins::staff_roster_style(palette), strings, layout.width, layout.height);
            roster.title = strings.get(layout.title);
            roster.icon = layout.icon;
            roster.show(facts, strings, units);
            return;
        }
        auto& window = desktop.root().add<ui::screens::ManagementWindow>(window_name(topic), ui::game_skins::management_window_style(palette), layout.width, layout.height, ui::screens::management_tabs(topic, strings), tab);
        window.title = strings.get(layout.title);
        window.icon = layout.icon;
        show(window, topic, facts, strings, units);
    }

    bool management_window_open(const ui::Desktop& desktop)
    {
        for (std::size_t t = 0; t < ui::screens::kManagementTopicCount; ++t)
        {
            if (find_window(desktop, static_cast<ui::screens::ManagementTopic>(t)))
                return true;
        }
        return find_roster(desktop).has_value();
    }

    bool close_top_management_window(ui::Desktop& desktop)
    {
        const auto children = desktop.root().children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (auto* window = dynamic_cast<ui::Window*>(it->get()); window != nullptr && !window->closing() && (dynamic_cast<ui::screens::ManagementWindow*>(window) != nullptr || dynamic_cast<ui::screens::StaffRoster*>(window) != nullptr))
            {
                window->close();
                return true;
            }
        }
        return false;
    }

    ManagementAsks update_management_windows(ui::Desktop& desktop, const ui::screens::ManagementFacts& facts, const ui::StringTable& strings, const ui::screens::InfoUnits& units)
    {
        ManagementAsks asks;
        for (std::size_t t = 0; t < ui::screens::kManagementTopicCount; ++t)
        {
            const auto topic = static_cast<ui::screens::ManagementTopic>(t);
            auto window = find_window(desktop, topic);
            if (!window || window->closing())
                continue;
            const bool picked = window->take_tab().has_value();
            if (picked || window->page_age() >= kRefresh)
                show(*window, topic, facts, strings, units);
            const bool park_page = topic == ui::screens::ManagementTopic::ParkOperations && window->tab() == 0;
            const auto button = window->take_button();
            if (button && park_page)
            {
                asks.replace_broken = *button == ui::screens::kReplaceBrokenButton;
                asks.centre_entrance = *button == ui::screens::kCentreEntranceButton;
                asks.buy_land = *button == ui::screens::kBuyLandButton;
                asks.buy_rights = *button == ui::screens::kBuyRightsButton;
            }
            const auto choice = window->take_choice();
            if (choice && park_page && choice->first == ui::screens::kOpenCloseParkButton)
                asks.park_open = choice->second == 0;
            if (choice && topic == ui::screens::ManagementTopic::Attractions && choice->first == ui::screens::kOpenCloseAllButton && choice->second < ui::screens::kOpenCloseAllStates.size())
            {
                asks.state_all = ui::screens::kOpenCloseAllStates[choice->second];
                for (const ui::TableRow& r : ui::screens::management_page(topic, window->tab(), facts, strings, units).rows)
                    asks.listed.push_back(ui::screens::management_row(r.id));
            }
            if (const auto row = window->take_row())
                asks.row = ui::screens::management_row(*row);
        }
        if (auto roster = find_roster(desktop); roster && !roster->closing())
        {
            if (roster->page_age() >= kRefresh)
                roster->show(facts, strings, units);
            asks.staff = roster->take_asks();
        }
        return asks;
    }

} // namespace rct3::scene
