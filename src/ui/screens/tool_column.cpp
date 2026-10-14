// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tool_column.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kHelpSuffix = "Help"; ///< After an icon's name: its help string
        constexpr std::string_view kQuitHelp = "File_QuitHelp";
        constexpr std::string_view kCareerHelp = "File_CareerMapHelp";

        // Below this height the buttons close up in proportion to it, and further still on very short screens.
        constexpr float kRoomyHeight = 700;
        constexpr float kReferenceHeight = 690;
        constexpr float kCrampedHeight = 500;
        constexpr float kCrampedFactor = 0.9f;
        constexpr float kSmallestStep = 25;
        constexpr std::size_t kGapAfter = static_cast<std::size_t>(ParkTool::Management); ///< Half a step after it

        std::u16string help(const StringTable& strings, std::string_view icon)
        {
            return std::u16string(strings.get(std::format("{}{}", icon, kHelpSuffix)));
        }
    } // namespace

    float tool_column_step(float height)
    {
        float step = tool_column::kButtonSize;
        if (height < kRoomyHeight)
            step *= height / kReferenceHeight;
        if (height < kCrampedHeight)
            step *= kCrampedFactor;
        return std::max(step, kSmallestStep);
    }

    ToolColumn::ToolColumn(std::string name, ToolColumnStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
    {
        // The column itself is only a frame for its buttons: it covers the screen and lets the pointer through.
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

    void ToolColumn::set_career(bool career)
    {
        Button& leave = button(ParkTool::Leave);
        leave.icon = career ? style_.career_leave_icon : style_.icons[static_cast<std::size_t>(ParkTool::Leave)];
        leave.tooltip = strings_.get(career ? kCareerHelp : kQuitHelp);
    }

    void ToolColumn::place(float height)
    {
        const float step = tool_column_step(height);
        const float size = std::min(step, tool_column::kButtonSize);
        float bottom = tool_column::kMargin;
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            buttons_[i]->placement = bottom_left(tool_column::kMargin, bottom, size, size);
            buttons_[i]->icon_scale = size / tool_column::kButtonSize;
            bottom += step;
            if (i == kGapAfter)
                bottom += step / 2;
        }
        placed_for_ = height;
    }

    void ToolColumn::update(const UpdateContext&)
    {
        if (const float height = rect().height(); height != placed_for_)
            place(height);
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->take_click())
                pressed_ = static_cast<ParkTool>(i);
        }
    }

} // namespace rct3::ui
