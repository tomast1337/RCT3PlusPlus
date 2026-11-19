// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/slider.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui::widgets
{

    namespace
    {

        constexpr f32 kKeyShare = 1.0f / 20; ///< Of the range, per key without a step
        constexpr f32 kPageShare = 1.0f / 10;

    } // namespace

    Slider::Slider(std::string name, SliderStyle style)
        : Widget(std::move(name))
        , style(std::move(style))
    {
    }

    f32 Slider::snapped(f32 v) const
    {
        const f32 low = std::min(minimum, maximum);
        const f32 high = std::max(minimum, maximum);
        if (step > 0)
            v = minimum + std::round((v - minimum) / step) * step;
        return std::clamp(v, low, high);
    }

    void Slider::set_value(f32 v)
    {
        value_ = snapped(v);
    }

    void Slider::move_to(f32 v)
    {
        const f32 before = value_;
        set_value(v);
        if (value_ != before)
            ++changes_;
    }

    bool Slider::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    f32 Slider::key_step() const
    {
        return step > 0 ? step : (maximum - minimum) * kKeyShare;
    }

    Rect Slider::thumb() const
    {
        const Rect& r = rect();
        const f32 travel = std::max(0.0f, r.width() - style.thumb_width);
        const f32 share = maximum != minimum ? (value_ - minimum) / (maximum - minimum) : 0;
        const f32 x = std::round(r.x0 + share * travel);
        const f32 y = std::round(r.y0 + (r.height() - style.thumb_height) / 2);
        return { x, y, x + style.thumb_width, y + style.thumb_height };
    }

    CursorKind Slider::cursor(Point p) const
    {
        return !style.bar && contains(thumb(), p) ? CursorKind::Grab : CursorKind::Hand;
    }

    void Slider::on_press(Point p)
    {
        const Rect t = thumb();
        const f32 centre = (t.x0 + t.x1) / 2;
        grab_ = contains(t, p) ? p.x - centre : 0;
        on_drag(p);
    }

    void Slider::on_drag(Point p)
    {
        const Rect& r = rect();
        const f32 travel = r.width() - style.thumb_width;
        if (travel <= 0)
            return;
        const f32 share = (p.x - grab_ - r.x0 - style.thumb_width / 2) / travel;
        move_to(minimum + std::clamp(share, 0.0f, 1.0f) * (maximum - minimum));
    }

    bool Slider::on_wheel(Point notches)
    {
        if (notches.y == 0)
            return false;
        move_to(value_ + notches.y * key_step());
        return true;
    }

    bool Slider::on_key(const KeyPress& key)
    {
        const f32 page = std::max(key_step(), (maximum - minimum) * kPageShare);
        switch (key.key)
        {
            case Key::Left:
            case Key::Down:
                move_to(value_ - key_step());
                return true;
            case Key::Right:
            case Key::Up:
                move_to(value_ + key_step());
                return true;
            case Key::PageDown:
                move_to(value_ - page);
                return true;
            case Key::PageUp:
                move_to(value_ + page);
                return true;
            case Key::Home:
                move_to(minimum);
                return true;
            case Key::End:
                move_to(maximum);
                return true;
            default:
                return false;
        }
    }

    void Slider::draw(DrawContext& context) const
    {
        const Rect& r = rect();
        if (style.bar)
        {
            const f32 share = maximum > minimum ? std::clamp((value_ - minimum) / (maximum - minimum), 0.0f, 1.0f) : 0.0f;
            const Color fade = enabled ? kWhite : skin::kDisabledTint;
            const auto tinted = [&](Color c) { return Color { static_cast<u8>(c.r * fade.r / 255), static_cast<u8>(c.g * fade.g / 255), static_cast<u8>(c.b * fade.b / 255), static_cast<u8>(c.a * fade.a / 255) }; };
            context.list.fill(r, tinted(style.bar_back_colour));
            context.list.fill({ r.x0, r.y0, r.x0 + r.width() * share, r.y1 }, tinted(style.bar_colour));
            return;
        }
        const f32 half = style.thumb_width / 2;
        const f32 y = std::round(r.y0 + (r.height() - style.rail_thickness) / 2);
        const Rect rail { r.x0 + half, y, r.x1 - half, y + style.rail_thickness };
        if (const auto skin = style.rail.empty() ? std::nullopt : context.skins.find(style.rail))
            skin::draw_skin(context.list, skin, rail, enabled ? kWhite : skin::kDisabledTint);
        else
            context.list.fill(rail, style.rail_colour);

        const skin::SkinState state = !enabled ? skin::SkinState::Disabled : pressed() ? skin::SkinState::Pressed : hovered() ? skin::SkinState::UpOver : skin::SkinState::Up;
        if (!style.thumb.empty() && context.skins.find(style.thumb, state))
            skin::draw_skin_state(context.list, context.skins, style.thumb, state, thumb());
        else
            context.list.fill(thumb(), style.thumb_colour);
    }

} // namespace rct3::ui::widgets
