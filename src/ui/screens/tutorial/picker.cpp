// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tutorial/picker.hpp"

#include "core/types.hpp"
#include "ui/skin/library.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui::screens::tutorial
{

    namespace
    {

        namespace fs = front::front_screen;
        namespace tm = tutorial_picker;

        constexpr std::string_view kStartName = "FrontScreen_StartTutorial_TT";
        constexpr std::string_view kStartHelp = "FrontScreen_StartTutorial_Help";
        constexpr f32 kMissingAlpha = 0.6f; ///< A tutorial without its park, and its name

        Placement placed(const Rect& r, const Rect& parent)
        {
            return top_left(r.x0 - parent.x0, r.y0 - parent.y0, r.width(), r.height());
        }

    } // namespace

    tutorial_picker::DiscPlace tutorial_picker::disc_place(const fs::Frame& frame)
    {
        const Rect area = frame.stretched(kDiscArea);
        const f32 radius = std::max(0.0f, std::min(area.width(), area.height()) / 2);
        const Point wanted = frame.at(fs::kRingCentre);
        // A box narrower than the disc (it cannot be, but for safety) centres it rather than clamping past.
        const auto inside = [radius](f32 v, f32 lo, f32 hi) { return lo + radius <= hi - radius ? std::clamp(v, lo + radius, hi - radius) : (lo + hi) / 2; };
        return { { inside(wanted.x, area.x0, area.x1), inside(wanted.y, area.y0, area.y1) }, radius / kDiscRadius };
    }

    f32 tutorial_picker::angle(usize index)
    {
        return kFirstAngle + kAngleStep * static_cast<f32>(index);
    }

    TutorialPicker::TutorialPicker(std::string name, TutorialPickerStyle style, const StringTable& strings, std::vector<TutorialInfo> tutorials)
        : Widget(std::move(name))
        , style_(std::move(style))
        , strings_(strings)
        , tutorials_(std::move(tutorials))
    {
        for (usize i = 0; i < tutorials_.size(); ++i)
        {
            auto& b = add<front::FrontButton>(tutorials_[i].icon, style_.button, tutorials_[i].icon);
            b.icon_own_size = true;
            b.placement = top_left(0, 0, 0, 0); // Nowhere until the first layout
            buttons_.push_back(&b);
        }
        labels_.resize(tutorials_.size());
        start_ = &add<front::FrontButton>("start", style_.button, style_.start_icon);
        start_->icon_own_size = true;
        start_->placement = top_left(0, 0, 0, 0);
    }

    void TutorialPicker::pick(usize index)
    {
        if (index < buttons_.size() && buttons_[index]->enabled)
            picked_ = index;
    }

    void TutorialPicker::set_available(usize index, bool available)
    {
        if (index < buttons_.size())
            buttons_[index]->enabled = available;
    }

    std::optional<usize> TutorialPicker::take_start()
    {
        return std::exchange(start_out_, std::nullopt);
    }

    std::optional<std::array<std::u16string, 2>> TutorialPicker::help() const
    {
        if (start_->hovered())
            return std::array<std::u16string, 2> { std::u16string(strings_.get(kStartName)), std::u16string(strings_.get(kStartHelp)) };
        for (usize i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->hovered())
                return std::array<std::u16string, 2> { tutorials_[i].name, std::u16string { } };
        }
        return std::nullopt;
    }

    void TutorialPicker::update(const UpdateContext&)
    {
        for (usize i = 0; i < buttons_.size(); ++i)
        {
            if (buttons_[i]->take_click())
                pick(i);
        }
        if (start_->take_click() && picked_ < buttons_.size() && buttons_[picked_]->enabled)
            start_out_ = picked_;
        start_->enabled = picked_ < buttons_.size() && buttons_[picked_]->enabled;

        const tm::DiscPlace disc = tm::disc_place(fs::frame_for(rect(), style_.round));
        const f32 size = tm::kButtonSize * disc.scale;
        const auto lay = [&](front::FrontButton& b, f32 angle) {
            const Point c = disc.at(fs::on_ring(angle, tm::kRadius));
            const Rect r { c.x - size / 2, c.y - size / 2, c.x + size / 2, c.y + size / 2 };
            b.placement = placed(r, rect());
            b.swell = { size * (fs::kSwellFactor - 1) / 2 };
            b.swell_time = units::Seconds { (fs::kSwellFactor - 1) / fs::kSwellRate };
            b.icon_scale = disc.scale;
            return r;
        };
        for (usize i = 0; i < buttons_.size(); ++i)
        {
            const Rect r = lay(*buttons_[i], tm::angle(i));
            labels_[i] = { r.x0 + tm::kLabelStart * r.width(), r.y0, r.x1 + tm::kLabelReach * disc.scale, r.y1 };
        }
        lay(*start_, tm::kStartAngle);
    }

    void TutorialPicker::draw(DrawContext& context) const
    {
        // Each quarter at its own size times the disc's one scale, its inner corner on the disc's middle.
        const tm::DiscPlace disc = tm::disc_place(fs::frame_for(rect(), style_.round));
        for (usize q = 0; q < style_.quarters.size(); ++q)
        {
            const auto skin = context.skins.find(style_.quarters[q]);
            if (!skin)
                continue;
            const auto [w, h] = skin->size();
            const f32 x = q % 2 == 0 ? disc.centre.x - w * disc.scale : disc.centre.x;
            const f32 y = q < 2 ? disc.centre.y - h * disc.scale : disc.centre.y;
            skin::draw_skin(context.list, skin, { x, y, x + w * disc.scale, y + h * disc.scale });
        }

        const auto name_font = context.fonts.get(font::FontRole::ScaledMedium);
        const auto picked_font = context.fonts.get(font::FontRole::ScaledLarge);
        for (usize i = 0; i < tutorials_.size(); ++i)
        {
            const auto f = i == picked_ ? picked_font : name_font;
            if (!f)
                continue;
            Color c = style_.text_colour;
            if (!buttons_[i]->enabled)
                c.a = static_cast<u8>(static_cast<f32>(c.a) * kMissingAlpha);
            draw_text(context.list, *f, tutorials_[i].name, labels_[i], { c, Align::Start, Align::Centre, true });
        }
        if (picked_ < tutorials_.size())
        {
            if (const auto f = context.fonts.get(font::FontRole::ScaledSmall))
                draw_text(context.list, *f, tutorials_[picked_].description, disc.rect(tm::kDescription), { style_.text_colour, Align::Start, Align::Centre, true });
        }
    }

} // namespace rct3::ui::screens::tutorial
