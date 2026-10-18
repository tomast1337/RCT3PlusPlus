// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_row.hpp"

#include "ui/screens/tool_menus.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kHelpSuffix = "Help"; ///< After an icon's name: its help string
        constexpr std::string_view kQuitHelp = "File_QuitHelp";
        constexpr std::string_view kCareerHelp = "File_CareerMapHelp";

        constexpr float kSmallestStep = 16;
        constexpr std::size_t kGapAfter = static_cast<std::size_t>(ParkTool::Management); ///< Half a step after it

        std::u16string help(const StringTable& strings, std::string_view icon)
        {
            return std::u16string(strings.get(std::format("{}{}", icon, kHelpSuffix)));
        }

        std::u16string help(const StringTable& strings, const ToolMenuItem& item)
        {
            return item.help.empty() ? help(strings, item.icon) : std::u16string(strings.get(item.help));
        }

        const ToolMenuItem& menu_entry(ParkTool tool, ToolItem item)
        {
            const auto menu = tool_menu(tool);
            return *std::ranges::find(menu, item, &ToolMenuItem::item);
        }
    } // namespace

    float tool_row_step(float width)
    {
        // The buttons and the half step's gap, inside the margins.
        const float steps = static_cast<float>(kParkToolCount) + 0.5f;
        return std::clamp((width - 2 * tool_row::kMargin) / steps, kSmallestStep, tool_row::kButtonSize);
    }

    ToolRow::ToolRow(std::string name, ToolRowStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
    {
        // The row itself is only a frame for its buttons: it covers the screen and lets the pointer through.
        placement = fill();
        // Each tool holds its button and its menu's items, named by their icons, so a widget path such as
        // "tools/View/Land_People" leads to the tool and to the item's line in its menu table.
        static constexpr std::array<std::string_view, kParkToolCount> kToolNames = { "Delete", "Land", "Paths", "Scenery", "Shops", "Rides", "Management", "Leave", "Files", "View", "Map" };
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            // Covers the row and lets the pointer through, as the row does.
            Widget& group = add<Widget>(std::string(kToolNames[i]));
            group.placement = fill();
            Button& b = group.add<Button>(style_.icons[i], std::string { }, style_.icons[i]);
            b.swell = style_.swell;
            b.tooltip = help(strings_, style_.icons[i]);
            buttons_[i] = &b;
            const auto tool = static_cast<ParkTool>(i);
            for (const ToolMenuItem& entry : tool_menu(tool))
            {
                Button& item = group.add<Button>(std::string(entry.icon), style_.item_face, std::string(entry.icon));
                item.swell = style_.item_swell;
                item.icon_scale = tool_row::kItemIconScale;
                item.tooltip = help(strings_, entry);
                item.visible = false;
                items_.push_back({ entry.item, &item, tool, entry.shown });
                show_switch(items_.back());
            }
        }
        set_career(false);
    }

    ToolRow::MenuItem& ToolRow::find(ToolItem item)
    {
        return *std::ranges::find(items_, item, &MenuItem::item);
    }

    const ToolRow::MenuItem& ToolRow::find(ToolItem item) const
    {
        return *std::ranges::find(items_, item, &MenuItem::item);
    }

    Button& ToolRow::item(ToolItem item) const
    {
        return *find(item).button;
    }

    void ToolRow::set_available(ToolItem item, bool available)
    {
        find(item).button->enabled = available;
    }

    bool ToolRow::switched_on(ToolItem item) const
    {
        return find(item).on;
    }

    void ToolRow::set_switch(ToolItem item, bool shown)
    {
        MenuItem& m = find(item);
        m.on = shown;
        show_switch(m);
    }

    void ToolRow::show_switch(MenuItem& item)
    {
        if (!is_switch(item.item))
            return;
        const ToolMenuItem& entry = menu_entry(item.tool, item.item);
        item.button->icon = std::string(item.on ? entry.shown_icon : entry.icon);
    }

    void ToolRow::close_menu()
    {
        open_.reset();
        for (MenuItem& m : items_)
            m.button->visible = false;
    }

    void ToolRow::place_menu(ParkTool tool)
    {
        const Button& under = button(tool);
        const float step = under.placement.offsets.x1 - under.placement.offsets.x0;
        const float middle = (under.placement.offsets.x0 + under.placement.offsets.x1) / 2;
        const float first = tool_row::kMargin + step + tool_row::kItemGap;
        const float width = rect().width();
        const auto count = static_cast<std::size_t>(std::ranges::count(items_, tool, &MenuItem::tool));
        // A row centred over the button and kept on the screen; one too wide for it goes on in another row above.
        const float room = width - 2 * tool_row::kMargin - (tool_row::kItemStep - tool_row::kItemWidth);
        const std::size_t fit = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(room / tool_row::kItemStep)));
        std::size_t i = 0;
        for (MenuItem& m : items_)
        {
            if (m.tool != tool)
                continue;
            const std::size_t line = i / fit;
            const std::size_t in_line = std::min(fit, count - line * fit);
            const float span = tool_row::kItemStep * static_cast<float>(in_line - 1) + tool_row::kItemWidth;
            const float left = std::clamp(middle - span / 2, tool_row::kMargin, std::max(tool_row::kMargin, width - tool_row::kMargin - span));
            const float x = left + tool_row::kItemStep * static_cast<float>(i % fit);
            const float y = first + tool_row::kItemStep * static_cast<float>(line);
            m.button->placement = bottom_left(std::floor(x), y, tool_row::kItemWidth, tool_row::kItemHeight);
            m.button->visible = true;
            ++i;
        }
    }

    void ToolRow::set_career(bool career)
    {
        Button& leave = button(ParkTool::Leave);
        leave.icon = career ? style_.career_leave_icon : style_.icons[static_cast<std::size_t>(ParkTool::Leave)];
        leave.tooltip = strings_.get(career ? kCareerHelp : kQuitHelp);
    }

    void ToolRow::place(float width)
    {
        const float step = tool_row_step(width);
        const float span = step * (static_cast<float>(kParkToolCount) + 0.5f);
        float x = std::floor((width - span) / 2);
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            buttons_[i]->placement = bottom_left(x, tool_row::kMargin, step, step);
            buttons_[i]->icon_scale = step / tool_row::kButtonSize;
            x += step;
            if (i == kGapAfter)
                x += step / 2;
        }
        placed_for_ = width;
    }

    void ToolRow::update(const UpdateContext&)
    {
        if (const float width = rect().width(); width != placed_for_)
        {
            place(width);
            if (open_)
                place_menu(*open_);
        }
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (!buttons_[i]->take_click())
                continue;
            const auto tool = static_cast<ParkTool>(i);
            if (tool_menu(tool).empty())
            {
                pressed_ = tool;
                continue;
            }
            const bool again = open_ == tool;
            close_menu();
            if (!again)
            {
                open_ = tool;
                place_menu(tool);
            }
        }
        for (MenuItem& m : items_)
        {
            if (!m.button->take_click())
                continue;
            if (is_switch(m.item))
            {
                m.on = !m.on;
                show_switch(m);
            }
            picked_ = m.item;
        }
    }

} // namespace rct3::ui
