// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tutorial_picker.hpp"

#include "ui/skin_library.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        namespace fs = front_screen;
        namespace tm = tutorial_picker;

        constexpr std::string_view kStartName = "FrontScreen_StartTutorial_TT";
        constexpr std::string_view kStartHelp = "FrontScreen_StartTutorial_Help";
        constexpr float kMissingAlpha = 0.6f; ///< A tutorial without its park, and its name

        Placement placed(const Rect& r, const Rect& parent)
        {
            return top_left(r.x0 - parent.x0, r.y0 - parent.y0, r.width(), r.height());
        }

    } // namespace

    float tutorial_picker::angle(std::size_t index)
    {
        return kFirstAngle + kAngleStep * static_cast<float>(index);
    }

    TutorialPicker::TutorialPicker(std::string name, TutorialPickerStyle style, const StringTable& strings, std::vector<TutorialInfo> tutorials)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
        , tutorials_(std::move(tutorials))
    {
        for (std::size_t i = 0; i < tutorials_.size(); ++i)
        {
            auto& b = add<FrontButton>("tutorial" + std::to_string(i), style_.button, tutorials_[i].icon);
            b.icon_own_size = true;
            b.placement = top_left(0, 0, 0, 0); // Nowhere until the first layout
            buttons_.push_back(&b);
        }
        labels_.resize(tutorials_.size());
        start_ = &add<FrontButton>("start", style_.button, style_.start_icon);
        start_->icon_own_size = true;
        start_->placement = top_left(0, 0, 0, 0);
    }

    void TutorialPicker::pick(std::size_t index)
    {
        if (index < buttons_.size() && buttons_[index]->enabled)
            picked_ = index;
    }

    void TutorialPicker::set_available(std::size_t index, bool available)
    {
        if (index < buttons_.size())
            buttons_[index]->enabled = available;
    }

    std::optional<std::size_t> TutorialPicker::take_start()
    {
        return std::exchange(start_out_, std::nullopt);
    }

    std::optional<std::array<std::u16string, 2>> TutorialPicker::help() const
    {
        if (start_->hovered())
            return std::array<std::u16string, 2> { std::u16string(strings_.get(kStartName)), std::u16string(strings_.get(kStartHelp)) };
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->hovered())
                return std::array<std::u16string, 2> { tutorials_[i].name, std::u16string { } };
        }
        return std::nullopt;
    }

    void TutorialPicker::update(const UpdateContext&)
    {
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->take_click())
                pick(i);
        }
        if (start_->take_click() && picked_ < buttons_.size() && buttons_[picked_]->enabled)
            start_out_ = picked_;
        start_->enabled = picked_ < buttons_.size() && buttons_[picked_]->enabled;

        const fs::Frame frame = fs::frame_for(rect());
        const float size = tm::kButtonSize * frame.round;
        const auto lay = [&](FrontButton& b, float angle) {
            const Rect r = fs::button_rect(frame, fs::on_ring(angle, tm::kRadius), tm::kButtonSize);
            b.placement = placed(r, rect());
            b.swell = size * (fs::kSwellFactor - 1) / 2;
            b.swell_time = (fs::kSwellFactor - 1) / fs::kSwellRate;
            b.icon_scale = frame.round;
            return r;
        };
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            const Rect r = lay(*buttons_[i], tm::angle(i));
            labels_[i] = { r.x0 + tm::kLabelStart * r.width(), r.y0, r.x1 + tm::kLabelReach * frame.sx, r.y1 };
        }
        lay(*start_, tm::kStartAngle);
    }

    void TutorialPicker::draw(DrawContext& context) const
    {
        const fs::Frame frame = fs::frame_for(rect());
        const Point centre = fs::kRingCentre;
        for (std::size_t q = 0; q < style_.quarters.size(); ++q)
        {
            const SkinRef* skin = context.skins.find(style_.quarters[q]);
            if (skin == nullptr)
                continue;
            const auto [w, h] = skin->size();
            const float x = q % 2 == 0 ? centre.x - w : centre.x;
            const float y = q < 2 ? centre.y - h : centre.y;
            draw_skin(context.list, skin, frame.stretched({ x, y, x + w, y + h }));
        }

        const Font* name_font = context.fonts.get(FontRole::ScaledMedium);
        const Font* picked_font = context.fonts.get(FontRole::ScaledLarge);
        for (std::size_t i = 0; i < tutorials_.size(); ++i)
        {
            const Font* f = i == picked_ ? picked_font : name_font;
            if (f == nullptr)
                continue;
            Color c = style_.text_colour;
            if (!buttons_[i]->enabled)
                c.a = static_cast<std::uint8_t>(static_cast<float>(c.a) * kMissingAlpha);
            draw_text(context.list, *f, tutorials_[i].name, labels_[i], { c, Align::Start, Align::Centre, true });
        }
        if (picked_ < tutorials_.size())
        {
            if (const Font* f = context.fonts.get(FontRole::ScaledSmall))
                draw_text(context.list, *f, tutorials_[picked_].description, frame.stretched(tm::kDescription), { style_.text_colour, Align::Start, Align::Centre, true });
        }
    }

} // namespace rct3::ui
