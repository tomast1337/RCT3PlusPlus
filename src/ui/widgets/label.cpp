// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/label.hpp"

#include <algorithm>

namespace rct3::ui
{

    Label::Label(std::string name, std::u16string text, FontRole font)
        : Widget(std::move(name))
        , text(std::move(text))
        , font(font)
    {
    }

    void Label::update(const UpdateContext& context)
    {
        const auto f = context.fonts ? context.fonts->get(font) : std::nullopt;
        text_size_ = { };
        if (!f)
            return;
        const auto lines = break_lines(*f, text, style.wrap ? rect().width() : 0);
        for (const TextLine& line : lines)
            text_size_.x = std::max(text_size_.x, line.width);
        text_size_.y = static_cast<float>(lines.size()) * f->line_height();
    }

    Rect Label::extent() const
    {
        // Measured from the top-left corner: text aligned elsewhere spills the other way too, but an area
        // scrolling it only needs to know how far it reaches.
        Rect r = rect();
        r.x1 = std::max(r.x1, r.x0 + text_size_.x);
        r.y1 = std::max(r.y1, r.y0 + text_size_.y);
        return r;
    }

    void Label::draw(DrawContext& context) const
    {
        if (const auto f = context.fonts.get(font))
            draw_text(context.list, *f, text, rect(), style);
    }

} // namespace rct3::ui
