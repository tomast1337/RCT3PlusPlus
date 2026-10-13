// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/key_box.hpp"

#include "ui/text_layout.hpp"

#include <utility>

namespace rct3::ui
{

    KeyBox::KeyBox(std::string name, std::vector<KeyCombo> combos)
        : Widget(std::move(name))
        , combos(std::move(combos))
    {
    }

    bool KeyBox::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    void KeyBox::on_release(Point, bool inside)
    {
        if (inside)
            waiting_ = true;
    }

    bool KeyBox::on_key(const KeyPress& key)
    {
        if (!waiting_)
        {
            if (key.key != Key::Space && key.key != Key::Enter)
                return false;
            waiting_ = true;
            return true;
        }
        if (key.key == Key::Escape && !key.control && !key.shift)
        {
            waiting_ = false;
        }
        else if (const auto made = key_combo(key))
        {
            waiting_ = false;
            if (combos != std::vector { *made })
            {
                combos = { *made };
                ++changes_;
            }
        }
        return true;
    }

    void KeyBox::update(const UpdateContext&)
    {
        if (!has_keyboard_focus() || !enabled)
            waiting_ = false;
    }

    void KeyBox::draw(DrawContext& context) const
    {
        const std::string& back = has_keyboard_focus() && !focused_skin.empty() ? focused_skin : skin;
        if (!back.empty())
            draw_skin(context.list, context.skins.find(back), rect());
        const Font* f = context.fonts.get(font);
        if (f == nullptr)
            return;
        const Rect& r = rect();
        const Rect box { r.x0 + padding.x0, r.y0 + padding.y0, r.x1 - padding.x1, r.y1 - padding.y1 };
        std::u16string shown = prompt;
        if (!waiting_)
        {
            std::string text;
            for (const KeyCombo& c : combos)
                text += (text.empty() ? "" : ", ") + to_string(c);
            shown = text.empty() ? none : std::u16string(text.begin(), text.end());
        }
        if (context.list.push_clip(box))
        {
            draw_text(context.list, *f, shown, box, { enabled ? text_colour : disabled_text_colour, Align::Start, Align::Centre, false });
            context.list.pop_clip();
        }
    }

} // namespace rct3::ui
