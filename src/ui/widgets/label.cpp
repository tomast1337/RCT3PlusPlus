// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/label.hpp"

namespace rct3::ui
{

    Label::Label(std::string name, std::u16string text, FontRole font)
        : Widget(std::move(name))
        , text(std::move(text))
        , font(font)
    {
    }

    void Label::draw(DrawContext& context) const
    {
        if (const Font* f = context.fonts.get(font))
            draw_text(context.list, *f, text, rect(), style);
    }

} // namespace rct3::ui
