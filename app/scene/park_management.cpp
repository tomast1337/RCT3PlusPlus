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

        std::string window_name(ui::ManagementTopic topic)
        {
            return std::string(ui::management_layout(topic).title);
        }

        std::optional<ui::ManagementWindow&> find_window(const ui::Desktop& desktop, ui::ManagementTopic topic)
        {
            return maybe(dynamic_cast<ui::ManagementWindow*>(as_pointer(desktop.root().find(window_name(topic)))));
        }

        void show(ui::ManagementWindow& window, ui::ManagementTopic topic, const ui::ManagementFacts& facts, const ui::StringTable& strings, const ui::InfoUnits& units)
        {
            window.show_page(ui::management_page(topic, window.tab(), facts, strings, units));
        }
    } // namespace

    void toggle_management_window(ui::Desktop& desktop, ui::ManagementTopic topic, const ui::ManagementFacts& facts, const ui::StringTable& strings, const ui::Palette& palette, const ui::InfoUnits& units)
    {
        if (auto open = find_window(desktop, topic))
        {
            open->close();
            return;
        }
        const ui::ManagementLayout layout = ui::management_layout(topic);
        auto& window = desktop.root().add<ui::ManagementWindow>(window_name(topic), ui::game_skins::management_window_style(palette), layout.width, layout.height, ui::management_tabs(topic, strings), 0);
        window.title = strings.get(layout.title);
        window.icon = layout.icon;
        show(window, topic, facts, strings, units);
    }

    bool management_window_open(const ui::Desktop& desktop)
    {
        for (std::size_t t = 0; t < ui::kManagementTopicCount; ++t)
        {
            if (find_window(desktop, static_cast<ui::ManagementTopic>(t)))
                return true;
        }
        return false;
    }

    bool close_top_management_window(ui::Desktop& desktop)
    {
        const auto children = desktop.root().children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (auto* window = dynamic_cast<ui::ManagementWindow*>(it->get()); window != nullptr && !window->closing())
            {
                window->close();
                return true;
            }
        }
        return false;
    }

    ManagementAsks update_management_windows(ui::Desktop& desktop, const ui::ManagementFacts& facts, const ui::StringTable& strings, const ui::InfoUnits& units)
    {
        ManagementAsks asks;
        for (std::size_t t = 0; t < ui::kManagementTopicCount; ++t)
        {
            const auto topic = static_cast<ui::ManagementTopic>(t);
            auto window = find_window(desktop, topic);
            if (!window || window->closing())
                continue;
            const bool picked = window->take_tab().has_value();
            if (picked || window->page_age() >= kRefresh)
                show(*window, topic, facts, strings, units);
            // Hiring and the other buttons do nothing yet.
            if (const auto button = window->take_button(); button && topic == ui::ManagementTopic::ParkOperations && window->tab() == 0 && *button == ui::kReplaceBrokenButton)
                asks.replace_broken = true;
            if (const auto row = window->take_row())
                asks.row = ui::management_row(*row);
        }
        return asks;
    }

} // namespace rct3::scene
