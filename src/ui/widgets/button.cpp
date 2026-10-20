// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/button.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
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

    Rect Button::face() const
    {
        // Eased in and out, and whole units so the skin's edges stay sharp.
        const float t = grown_ * grown_ * (3 - 2 * grown_);
        const float by = std::round(swell * t);
        const Rect& r = rect();
        return { r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by };
    }

    void Button::update(const UpdateContext& context)
    {
        const float target = hovered() && enabled && swell > 0 ? 1.0f : 0.0f;
        const float step = swell_time > 0 ? context.elapsed / swell_time : 1.0f;
        grown_ = grown_ < target ? std::min(target, grown_ + step) : std::max(target, grown_ - step);
    }

    void Button::draw(DrawContext& context) const
    {
        const SkinState s = state();
        const Rect r = face();
        // A toggle on a face with one picture for every state would look held down, so dimmed, while on: it
        // shows lit while on and dimmed while off instead.
        SkinState look = s;
        if (toggle && s != SkinState::Pressed && s != SkinState::Disabled && as_pointer(context.skins.find(skin, SkinState::Down)) == as_pointer(context.skins.find(skin, SkinState::Up)))
            look = on ? (hovered() ? SkinState::UpOver : SkinState::Up) : SkinState::Down;
        draw_skin_state(context.list, context.skins, skin, look, r);

        // The face looks pushed in while held: what is on it moves by one unit.
        const float nudge = s == SkinState::Pressed ? 1.0f : 0.0f;
        if (const auto glyph = icon.empty() ? std::nullopt : context.skins.find(icon))
        {
            // The icon grows with the face.
            const float grow = (rect().width() > 0 ? r.width() / rect().width() : 1.0f) * icon_scale;
            const float w = std::round(glyph->size()[0] * grow);
            const float h = std::round(glyph->size()[1] * grow);
            const float x = std::round(r.x0 + (r.width() - w) / 2) + nudge;
            const float y = std::round(r.y0 + (r.height() - h) / 2) + nudge;
            draw_skin(context.list, glyph, { x, y, x + w, y + h }, s == SkinState::Disabled ? hex("#ffffff80") : kWhite);
        }

        const auto f = text.empty() ? std::nullopt : context.fonts.get(font);
        if (!f)
            return;
        if (context.list.push_clip(r))
        {
            const Rect box { r.x0 + text_inset.x0 + nudge, r.y0 + text_inset.y0 + nudge, r.x1 - text_inset.x1 + nudge, r.y1 - text_inset.y1 + nudge };
            draw_text(context.list, *f, text, box, { enabled ? text_colour : disabled_text_colour, Align::Centre, Align::Centre, false });
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui
