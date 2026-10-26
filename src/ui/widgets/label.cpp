// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/label.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    Label::Label(std::string name, std::u16string text, FontRole font)
        : Widget(std::move(name))
        , text(std::move(text))
        , font(font)
    {
    }

    bool Label::on_key(const KeyPress& key)
    {
        if (!copyable)
            return false;
        if (key.key == Key::Copy)
        {
            copied_ = text;
            return true;
        }
        return key.key == Key::Shortcut && key.character == U'a'; // All of it is selected already
    }

    std::optional<std::u16string> Label::take_copied()
    {
        return std::exchange(copied_, std::nullopt);
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
        const auto f = context.fonts.get(font);
        if (!f)
            return;
        if (has_keyboard_focus() && !text.empty())
        {
            // Where the text sits in the box, aligned as it is drawn.
            const Rect& r = rect();
            const auto offset = [](Align a, float room) { return a == Align::Start ? 0 : a == Align::Centre ? room / 2 : room; };
            const float x = r.x0 + offset(style.horizontal, r.width() - text_size_.x);
            const float y = r.y0 + offset(style.vertical, r.height() - text_size_.y);
            context.list.fill({ x, y, x + text_size_.x, y + text_size_.y }, selection_colour);
        }
        draw_text(context.list, *f, text, rect(), style);
    }

} // namespace rct3::ui
