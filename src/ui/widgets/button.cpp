// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/button.hpp"

#include "ui/text_layout.hpp"

#include <cmath>

namespace rct3::ui
{

    Button::Button(std::string name, std::string skin, std::string icon)
        : Widget(std::move(name))
        , skin(std::move(skin))
        , icon(std::move(icon))
    {
    }

    bool Button::take_click()
    {
        if (clicks_ == 0)
            return false;
        --clicks_;
        return true;
    }

    SkinState Button::state() const
    {
        if (!enabled)
            return SkinState::Disabled;
        if (pressed())
            return SkinState::Pressed;
        if (toggle && on)
            return hovered() ? SkinState::DownOver : SkinState::Down;
        return hovered() ? SkinState::UpOver : SkinState::Up;
    }

    void Button::on_release(Point, bool inside)
    {
        if (inside)
            click();
    }

    bool Button::on_key(const KeyPress& key)
    {
        if (key.key != Key::Space && key.key != Key::Enter)
            return false;
        click();
        return true;
    }

    void Button::click()
    {
        ++clicks_;
        if (toggle)
            on = !on;
    }

    void Button::draw(DrawContext& context) const
    {
        const SkinState s = state();
        const SkinRef* face = context.skins.find(skin, s);

        // A family without this state falls back to its resting look; tint it so the state still shows.
        Color tint = kWhite;
        const bool fell_back = face == context.skins.find(skin, SkinState::Up);
        if (fell_back && (s == SkinState::Pressed || s == SkinState::Down || s == SkinState::DownOver))
            tint = { 190, 190, 190, 255 };
        else if (fell_back && s == SkinState::Disabled)
            tint = { 128, 128, 128, 160 };
        draw_skin(context.list, face, rect(), tint);

        // The face looks pushed in while held: what is on it moves by one unit.
        const float nudge = s == SkinState::Pressed ? 1.0f : 0.0f;
        if (const SkinRef* glyph = icon.empty() ? nullptr : context.skins.find(icon))
        {
            const auto [w, h] = glyph->size();
            const float x = std::round(rect().x0 + (rect().width() - w) / 2) + nudge;
            const float y = std::round(rect().y0 + (rect().height() - h) / 2) + nudge;
            draw_skin(context.list, glyph, { x, y, x + w, y + h }, s == SkinState::Disabled ? Color { 255, 255, 255, 128 } : kWhite);
        }

        const Font* f = text.empty() ? nullptr : context.fonts.get(font);
        if (f == nullptr)
            return;
        if (context.list.push_clip(rect()))
        {
            const Rect box { rect().x0 + text_inset.x0 + nudge, rect().y0 + text_inset.y0 + nudge, rect().x1 - text_inset.x1 + nudge, rect().y1 - text_inset.y1 + nudge };
            draw_text(context.list, *f, text, box, { enabled ? text_colour : disabled_text_colour, Align::Centre, Align::Centre, false });
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui
