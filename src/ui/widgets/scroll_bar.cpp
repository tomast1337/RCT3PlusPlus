// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/scroll_bar.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {

        // Parts are worked out along the bar: `start` and `end` on the long axis, `across` the short one.
        Rect span(Orientation o, float start, float end, float across0, float across1)
        {
            return o == Orientation::Vertical ? Rect { across0, start, across1, end } : Rect { start, across0, end, across1 };
        }

        constexpr Color kHeldTint = hex("#bebebe");
        constexpr Color kIdleTint = hex("#ffffff80");

    } // namespace

    ScrollBar::ScrollBar(std::string name, ScrollBarStyle style, Orientation orientation)
        : Widget(std::move(name))
        , style_(std::move(style))
        , orientation_(orientation)
    {
    }

    float ScrollBar::range() const
    {
        return std::max(0.0f, content - visible);
    }

    void ScrollBar::scroll_by(float delta)
    {
        position += delta;
        clamp();
    }

    void ScrollBar::clamp()
    {
        position = std::clamp(position, 0.0f, range());
    }

    float ScrollBar::along(Point p) const
    {
        return orientation_ == Orientation::Vertical ? p.y : p.x;
    }

    ScrollBarParts ScrollBar::parts() const
    {
        const Rect& r = rect();
        const bool vertical = orientation_ == Orientation::Vertical;
        const float start = vertical ? r.y0 : r.x0;
        const float end = vertical ? r.y1 : r.x1;
        const float a0 = vertical ? r.x0 : r.y0;
        const float a1 = vertical ? r.x1 : r.y1;
        // The arrows share what there is when the bar is shorter than both.
        const float arrow = std::min(style_.thickness, (end - start) / 2);

        ScrollBarParts p;
        p.back = span(orientation_, start, start + arrow, a0, a1);
        p.forward = span(orientation_, end - arrow, end, a0, a1);
        const float t0 = start + arrow;
        const float t1 = end - arrow;
        p.track = span(orientation_, t0, t1, a0, a1);
        const float track = t1 - t0;
        if (range() <= 0 || content <= 0 || track <= 0)
            return p;
        const float length = std::clamp(track * visible / content, std::min(style_.min_rider, track), track);
        const float offset = (track - length) * position / range();
        p.rider = span(orientation_, t0 + offset, t0 + offset + length, a0, a1);
        return p;
    }

    void ScrollBar::drag_to(float pointer)
    {
        const ScrollBarParts p = parts();
        const bool vertical = orientation_ == Orientation::Vertical;
        const float t0 = vertical ? p.track.y0 : p.track.x0;
        const float track = vertical ? p.track.height() : p.track.width();
        const float length = vertical ? p.rider.height() : p.rider.width();
        if (track <= length)
            return;
        const float t = std::clamp((pointer - grab_ - t0) / (track - length), 0.0f, 1.0f);
        position = t * range();
    }

    void ScrollBar::on_press(Point p)
    {
        if (range() <= 0)
            return;
        const ScrollBarParts parts_now = parts();
        held_time_ = 0;
        next_repeat_ = style_.repeat_delay;
        if (contains(parts_now.back, p))
        {
            held_ = Part::Back;
            scroll_by(-style_.step);
        }
        else if (contains(parts_now.forward, p))
        {
            held_ = Part::Forward;
            scroll_by(style_.step);
        }
        else if (contains(parts_now.track, p))
        {
            held_ = Part::Rider;
            const bool vertical = orientation_ == Orientation::Vertical;
            const float rider_start = vertical ? parts_now.rider.y0 : parts_now.rider.x0;
            const float length = vertical ? parts_now.rider.height() : parts_now.rider.width();
            if (contains(parts_now.rider, p))
            {
                grab_ = along(p) - rider_start;
            }
            else
            {
                grab_ = length / 2;
                drag_to(along(p));
            }
        }
    }

    void ScrollBar::on_drag(Point p)
    {
        if (held_ == Part::Rider)
            drag_to(along(p));
    }

    void ScrollBar::on_release(Point, bool)
    {
        held_ = Part::None;
    }

    bool ScrollBar::on_wheel(Point notches)
    {
        const float along = orientation_ == Orientation::Vertical ? notches.y : notches.x;
        if (range() <= 0 || along == 0)
            return false;
        scroll_by(-along * style_.wheel_step);
        return true;
    }

    void ScrollBar::update(const UpdateContext& context)
    {
        clamp();
        // Arrows repeat while held with the pointer still on them.
        if ((held_ != Part::Back && held_ != Part::Forward) || !pressed())
            return;
        held_time_ += context.elapsed;
        while (held_time_ >= next_repeat_ && style_.repeat_interval > 0)
        {
            scroll_by(held_ == Part::Back ? -style_.step : style_.step);
            next_repeat_ += style_.repeat_interval;
        }
    }

    void ScrollBar::draw(DrawContext& context) const
    {
        const ScrollBarParts p = parts();
        const bool idle = range() <= 0 || !enabled;
        draw_skin(context.list, context.skins.find(style_.rail), p.track);
        const auto arrow_tint = [&](Part part) {
            if (idle)
                return kIdleTint;
            return held_ == part && pressed() ? kHeldTint : kWhite;
        };
        draw_skin(context.list, context.skins.find(style_.back_arrow), p.back, arrow_tint(Part::Back));
        draw_skin(context.list, context.skins.find(style_.forward_arrow), p.forward, arrow_tint(Part::Forward));
        if (p.rider.empty())
            return;
        SkinState state = hovered() ? SkinState::UpOver : SkinState::Up;
        if (held_ == Part::Rider)
            state = SkinState::Pressed;
        draw_skin(context.list, context.skins.find(style_.rider, state), p.rider);
    }

} // namespace rct3::ui
