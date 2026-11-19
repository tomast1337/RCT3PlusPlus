// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/paint_button.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace rct3::ui::widgets
{

    PaintButton::PaintButton(std::string name, PaintButtonStyle style, std::vector<usize> colours)
        : Button(std::move(name), style.face)
        , colours(std::move(colours))
        , style_(std::move(style))
    {
        this->colours.resize(std::clamp<usize>(this->colours.size(), 1, kMaxPots));
    }

    std::optional<usize> PaintButton::take_pot()
    {
        if (!take_click())
            return std::nullopt;
        // A click from the keys has no pot: the first.
        return std::exchange(clicked_on_, std::nullopt).value_or(0);
    }

    Rect PaintButton::spot(usize i) const
    {
        const std::vector<PaintSpot>& spots = style_.spots[colours.size() - 1];
        if (i >= spots.size())
            return { };
        // As the icon is drawn: centred on the face, growing with it.
        const Rect f = face();
        const f32 scale = (rect().width() > 0 ? f.width() / rect().width() : 1.0f) * icon_scale;
        const f32 x0 = (f.x0 + f.x1) / 2 - style_.icon_size * scale / 2;
        const f32 y0 = (f.y0 + f.y1) / 2 - style_.icon_size * scale / 2;
        const f32 half = spots[i].size * scale / 2;
        const f32 cx = x0 + spots[i].centre.x * scale;
        const f32 cy = y0 + spots[i].centre.y * scale;
        return { cx - half, cy - half, cx + half, cy + half };
    }

    usize PaintButton::pot_at(Point p) const
    {
        usize best = 0;
        f32 nearest = 0;
        for (usize i = 0; i < colours.size(); ++i)
        {
            const Rect s = spot(i);
            const f32 dx = p.x - (s.x0 + s.x1) / 2;
            const f32 dy = p.y - (s.y0 + s.y1) / 2;
            const f32 d = dx * dx + dy * dy;
            if (i == 0 || d < nearest)
            {
                best = i;
                nearest = d;
            }
        }
        return best;
    }

    std::optional<usize> PaintButton::lit() const
    {
        if (held)
            return held;
        return hovered() && enabled ? under_ : std::nullopt;
    }

    void PaintButton::on_hover(Point p)
    {
        under_ = pot_at(p);
    }

    void PaintButton::on_press(Point p)
    {
        pressed_on_ = pot_at(p);
    }

    void PaintButton::on_release(Point p, bool inside)
    {
        clicked_on_ = pressed_on_;
        Button::on_release(p, inside);
    }

    void PaintButton::update(const UpdateContext& context)
    {
        Button::update(context);
        const usize n = colours.size() - 1;
        const std::optional<usize> l = lit();
        icon = l && *l < kMaxPots ? style_.lit[n][*l] : style_.pots[n];
    }

    void PaintButton::draw(DrawContext& context) const
    {
        Button::draw(context);
        for (usize i = 0; i < colours.size(); ++i)
        {
            const Rect s = spot(i);
            if (s.width() > 0)
                skin::draw_skin(context.list, context.skins.find(std::format("{}{}", style_.paint_prefix, colours[i])), s);
        }
    }

} // namespace rct3::ui::widgets
