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
        const Font* f = context.fonts != nullptr ? context.fonts->get(font) : nullptr;
        if (f == nullptr)
        {
            text_height_ = 0;
            return;
        }
        const auto lines = break_lines(*f, text, style.wrap ? rect().width() : 0);
        text_height_ = static_cast<float>(lines.size()) * f->line_height();
    }

    Rect Label::extent() const
    {
        Rect r = rect();
        r.y1 = std::max(r.y1, r.y0 + text_height_);
        return r;
    }

    void Label::draw(DrawContext& context) const
    {
        if (const Font* f = context.fonts.get(font))
            draw_text(context.list, *f, text, rect(), style);
    }

} // namespace rct3::ui
