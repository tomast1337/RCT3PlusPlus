// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_row.hpp"

#include <algorithm>
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
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            Button& b = add<Button>(std::format("tool{}", i), std::string { }, style_.icons[i]);
            b.swell = style_.swell;
            b.tooltip = help(strings_, style_.icons[i]);
            buttons_[i] = &b;
        }
        set_career(false);
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
            place(width);
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->take_click())
                pressed_ = static_cast<ParkTool>(i);
        }
    }

} // namespace rct3::ui
