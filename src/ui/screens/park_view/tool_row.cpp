// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/park_view/tool_row.hpp"

#include "core/types.hpp"
#include "ui/screens/park_view/tool_menus.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace rct3::ui::screens::park_view
{

    namespace
    {
        constexpr std::string_view kHelpSuffix = "Help"; ///< After an icon's name: its help string
        constexpr std::string_view kQuitHelp = "File_QuitHelp";
        constexpr std::string_view kCareerHelp = "File_CareerMapHelp";

        constexpr f32 kSmallestStep = 16;
        constexpr usize kGapAfter = static_cast<usize>(ParkTool::Management); ///< Half a step after it

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

    f32 tool_row_step(f32 width)
    {
        // The buttons and the half step's gap, inside the margins.
        const f32 steps = static_cast<f32>(kParkToolCount) + 0.5f;
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
        for (usize i = 0; i < buttons_.size(); ++i)
        {
            // Covers the row and lets the pointer through, as the row does.
            Widget& group = add<Widget>(std::string(kToolNames[i]));
            group.placement = fill();
            widgets::Button& b = group.add<widgets::Button>(style_.icons[i], std::string { }, style_.icons[i]);
            b.swell = style_.swell;
            b.tooltip = help(strings_, style_.icons[i]);
            buttons_[i] = &b;
            const auto tool = static_cast<ParkTool>(i);
            for (const ToolMenuItem& entry : tool_menu(tool))
            {
                widgets::Button& item = group.add<widgets::Button>(std::string(entry.icon), style_.item_face, std::string(entry.icon));
                item.swell = style_.item_swell;
                item.icon_scale = tool_row::kItemIconScale;
                item.tooltip = help(strings_, entry);
                item.visible = false;
                items_.push_back({ entry.item, &item, tool, entry.shown });
                show_switch(items_.back());
                for (const ToolMenuItem& sub : tool_submenu(entry.item))
                {
                    widgets::Button& below = group.add<widgets::Button>(std::string(sub.icon), style_.item_face, std::string(sub.icon));
                    below.swell = style_.item_swell;
                    below.icon_scale = tool_row::kItemIconScale;
                    below.tooltip = help(strings_, sub);
                    below.visible = false;
                    items_.push_back({ .item = sub.item, .button = &below, .tool = tool, .under = entry.item });
                }
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

    widgets::Button& ToolRow::item(ToolItem item) const
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
        // A switch with one picture shows its state as a toggle does.
        item.button->toggle = entry.shown_icon == entry.icon;
        item.button->on = item.on;
    }

    void ToolRow::close_menu()
    {
        open_.reset();
        sub_open_.reset();
        for (MenuItem& m : items_)
            m.button->visible = false;
    }

    void ToolRow::place_menu(ParkTool tool)
    {
        const widgets::Button& under = button(tool);
        const f32 step = under.placement.offsets.x1 - under.placement.offsets.x0;
        const f32 middle = (under.placement.offsets.x0 + under.placement.offsets.x1) / 2;
        const f32 first = tool_row::kMargin + step + tool_row::kItemGap;
        const f32 width = rect().width();
        const auto count = static_cast<usize>(std::ranges::count_if(items_, [&](const MenuItem& m) { return m.tool == tool && !m.under; }));
        // A row centred over the button and kept on the screen; one too wide for it goes on in another row above.
        const f32 room = width - 2 * tool_row::kMargin - (tool_row::kItemStep - tool_row::kItemWidth);
        const usize fit = std::max<usize>(1, static_cast<usize>(std::floor(room / tool_row::kItemStep)));
        usize i = 0;
        for (MenuItem& m : items_)
        {
            if (m.tool != tool || m.under)
                continue;
            const usize line = i / fit;
            const usize in_line = std::min(fit, count - line * fit);
            const f32 span = tool_row::kItemStep * static_cast<f32>(in_line - 1) + tool_row::kItemWidth;
            const f32 left = std::clamp(middle - span / 2, tool_row::kMargin, std::max(tool_row::kMargin, width - tool_row::kMargin - span));
            const f32 x = left + tool_row::kItemStep * static_cast<f32>(i % fit);
            const f32 y = first + tool_row::kItemStep * static_cast<f32>(line);
            m.x = std::floor(x);
            m.bottom = y;
            m.button->placement = bottom_left(m.x, y, tool_row::kItemWidth, tool_row::kItemHeight);
            m.button->visible = true;
            ++i;
        }
    }

    void ToolRow::place_submenu(ToolItem item)
    {
        // A row centred over the item, a step above it, kept on the screen.
        const MenuItem& over = find(item);
        const f32 middle = over.x + tool_row::kItemWidth / 2;
        const auto count = static_cast<usize>(std::ranges::count(items_, std::optional(item), &MenuItem::under));
        const f32 span = tool_row::kItemStep * static_cast<f32>(count - 1) + tool_row::kItemWidth;
        const f32 left = std::clamp(middle - span / 2, tool_row::kMargin, std::max(tool_row::kMargin, rect().width() - tool_row::kMargin - span));
        usize i = 0;
        for (MenuItem& m : items_)
        {
            if (m.under != item)
                continue;
            m.button->placement = bottom_left(std::floor(left + tool_row::kItemStep * static_cast<f32>(i++)), over.bottom + tool_row::kItemStep, tool_row::kItemWidth, tool_row::kItemHeight);
            m.button->visible = true;
        }
    }

    void ToolRow::set_career(bool career)
    {
        widgets::Button& leave = button(ParkTool::Leave);
        leave.icon = career ? style_.career_leave_icon : style_.icons[static_cast<usize>(ParkTool::Leave)];
        leave.tooltip = strings_.get(career ? kCareerHelp : kQuitHelp);
    }

    void ToolRow::place(f32 width)
    {
        const f32 step = tool_row_step(width);
        const f32 span = step * (static_cast<f32>(kParkToolCount) + 0.5f);
        f32 x = std::floor((width - span) / 2);
        for (usize i = 0; i < buttons_.size(); ++i)
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
        if (const f32 width = rect().width(); width != placed_for_)
        {
            place(width);
            if (open_)
                place_menu(*open_);
            if (sub_open_)
                place_submenu(*sub_open_);
        }
        for (usize i = 0; i < buttons_.size(); ++i)
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
            if (!tool_submenu(m.item).empty())
            {
                const bool again = sub_open_ == m.item;
                for (MenuItem& sub : items_)
                    if (sub.under)
                        sub.button->visible = false;
                sub_open_.reset();
                if (!again)
                {
                    sub_open_ = m.item;
                    place_submenu(m.item);
                }
                continue;
            }
            if (is_switch(m.item))
            {
                m.on = !m.on;
                show_switch(m);
            }
            picked_ = m.item;
        }
    }

} // namespace rct3::ui::screens::park_view
