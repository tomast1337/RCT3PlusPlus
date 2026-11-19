// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/tooltip.hpp"

#include "core/types.hpp"
#include "ui/placement.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    std::u16string_view tooltip_text(const Widget* w)
    {
        for (; w; w = w->parent())
        {
            if (!w->tooltip.empty())
                return w->tooltip;
        }
        return { };
    }

    void Tooltip::update(const Widget* under, Point pointer, bool pressed, units::Seconds elapsed, const TooltipStyle& style)
    {
        const std::u16string_view text = tooltip_text(under);
        if (text != text_)
        {
            text_ = text;
            shown_ = false;
            blocked_ = false;
            rested_ = { };
        }
        if (pressed)
        {
            shown_ = false;
            blocked_ = true;
        }

        const f32 dx = pointer.x - last_.x;
        const f32 dy = pointer.y - last_.y;
        last_ = pointer;
        if (dx * dx + dy * dy > style.still_distance * style.still_distance)
            rested_ = { };
        else
            rested_ += elapsed;

        if (!shown_ && !blocked_ && !text_.empty() && rested_ >= style.delay)
        {
            shown_ = true;
            anchor_ = pointer;
        }
    }

    Rect Tooltip::box(const TooltipStyle& style, const font::FontSet& fonts, const Rect& view) const
    {
        const auto font = fonts.get(style.font);
        if (!font)
            return { };
        f32 width = 0;
        const auto lines = break_lines(*font, text_, style.max_width);
        for (const TextLine& line : lines)
            width = std::max(width, line.width);
        const f32 w = std::ceil(width) + style.padding.x0 + style.padding.x1;
        const f32 h = std::ceil(static_cast<f32>(lines.size()) * font->line_height()) + style.padding.y0 + style.padding.y1;
        const f32 x = std::round(anchor_.x + style.offset.x);
        const f32 y = std::round(anchor_.y + style.offset.y);
        return moved_inside({ x, y, x + w, y + h }, view);
    }

    void Tooltip::draw(DrawContext& context, const TooltipStyle& style, const Rect& view) const
    {
        if (!shown_)
            return;
        const auto font = context.fonts.get(style.font);
        const Rect r = box(style, context.fonts, view);
        if (!font || r.empty())
            return;
        skin::draw_skin(context.list, context.skins.find(style.skin), r);
        const Rect inner { r.x0 + style.padding.x0, r.y0 + style.padding.y0, r.x1 - style.padding.x1, r.y1 - style.padding.y1 };
        draw_text(context.list, *font, text_, inner, { style.colour, Align::Start, Align::Start, true });
    }

} // namespace rct3::ui
