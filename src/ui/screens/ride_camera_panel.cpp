// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/ride_camera_panel.hpp"

#include "ui/placement.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {
        // In units of the desktop.
        constexpr float kMargin = 10;
        constexpr float kLineHeight = 18;
        constexpr float kButtonsTop = 32;
        constexpr float kButton = 32;
        constexpr float kPin = 16;
        constexpr float kGap = 6;
        constexpr float kHeight = kButtonsTop + kButton + kMargin;
        constexpr float kWidth = kMargin * 2 + kPin + kGap + 6 * (kButton + kGap) - kGap;

        constexpr std::array<std::string_view, 6> kHelp { "RidecamGUI_Exit", "RidecamGUI_PrevCam", "RidecamGUI_NextCam", "RidecamGUI_PrevTrain", "RidecamGUI_NextTrain", "RidecamGUI_Type" };
        /// Left to right, after the pin: leave, change the kind, then the steps.
        constexpr std::array<std::size_t, 6> kSlot { 0, 2, 3, 4, 5, 1 };
    } // namespace

    RideCameraPanel::RideCameraPanel(std::string name, RideCameraPanelStyle style, const StringTable& strings)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
    {
        placement = bottom_right(0, 0, kWidth, kHeight);
        add<Panel>("back", style_.back);
        info_ = &add<Label>("info", std::u16string { });
        info_->style = { style_.text_colour, Align::Start, Align::Centre, false };
        info_->placement = top_left(kMargin, kMargin, kWidth - kMargin * 2, kLineHeight);
        pin_ = &add<Button>("pin", style_.button, style_.pin);
        pin_->toggle = true;
        pin_->tooltip = strings_.get("RidecamGUI_Stick");
        pin_->placement = top_left(kMargin, kButtonsTop + (kButton - kPin) / 2, kPin, kPin);
        const float x = kMargin + kPin + kGap;
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            buttons_[i] = &add<Button>(std::string(kHelp[i]), style_.button, i == 0 ? style_.leave : i <= 4 ? style_.steps[i - 1] : style_.types[1]);
            buttons_[i]->tooltip = strings_.get(kHelp[i]);
            buttons_[i]->placement = top_left(x + static_cast<float>(kSlot[i]) * (kButton + kGap), kButtonsTop, kButton, kButton);
        }
        buttons_[5]->enabled = false;
    }

    void RideCameraPanel::show(std::u16string info, bool cameras, bool trains, std::u16string_view noun, std::optional<std::size_t> next_type, std::u16string_view next_name)
    {
        // The text of `key` with `with` in place of its mark, as in "Change to # camera".
        const auto marked = [&](std::string_view key, std::u16string_view with) {
            std::u16string help(strings_.get(key));
            if (const auto mark = help.find(u'#'); mark != std::u16string::npos)
                help.replace(mark, 1, with);
            return help;
        };
        info_->text = std::move(info);
        Button& type = *buttons_[5];
        type.enabled = next_type && *next_type < style_.types.size();
        if (type.enabled)
        {
            type.icon = style_.types[*next_type];
            type.tooltip = marked(kHelp[5], next_name);
        }
        for (std::size_t step = 0; step < style_.steps.size(); ++step)
        {
            Button& b = *buttons_[step + 1];
            b.enabled = step < 2 ? cameras : trains;
            b.icon = b.enabled ? style_.steps[step] : style_.no_steps[step];
            if (step < 2)
                b.tooltip = strings_.get(b.enabled ? kHelp[step + 1] : "RidecamGUI_DisabledCamera");
            else
                b.tooltip = marked(b.enabled ? (step == 2 ? "RidecamGUI_PrevRide" : "RidecamGUI_NextRide") : "RidecamGUI_DisabledTrain", noun);
        }
    }

    void RideCameraPanel::pointer_moved(float distance)
    {
        if (distance > ride_camera_panel::kWakeDistance)
            still_ = 0;
    }

    void RideCameraPanel::update(const UpdateContext& context)
    {
        for (std::size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i]->take_click())
                asked_ = static_cast<RideCameraAction>(i);
        if (pin_->take_click())
            pin_->tooltip = strings_.get(pin_->on ? "RidecamGUI_UnStick" : "RidecamGUI_Stick");
        pin_->icon = pin_->on ? style_.unpin : style_.pin;

        still_ += context.elapsed;
        const bool wanted = pin_->on || hovered() || still_ < ride_camera_panel::kStillSeconds;
        const float step = context.elapsed / ride_camera_panel::kSlideSeconds;
        shown_ = std::clamp(shown_ + (wanted ? step : -step), 0.0f, 1.0f);
        // Eased, so it slows as it comes to rest at either end.
        const float eased = shown_ * (2 - shown_);
        placement = bottom_right(0, -(1 - eased) * kHeight, kWidth, kHeight);
    }

} // namespace rct3::ui
