// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/money_floaters.hpp"

#include "core/types.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::ui::widgets
{

    namespace
    {
        constexpr units::PixelPace kRiseSpeed { 24 };
        constexpr units::Seconds kFade { 0.8f };    ///< The last of its life, over which it fades
        constexpr f32 kBobHeight = 0.18f;           ///< Of the line height
        constexpr units::Radians kBobStep { 1.1f }; ///< Phase between one letter and the next
        constexpr units::TurnRate kBobSpeed { 9 };
    } // namespace

    f32 money_float_rise(units::Seconds age) noexcept
    {
        return std::max(age.value, 0.0f) * kRiseSpeed.value;
    }

    f32 money_float_alpha(units::Seconds age) noexcept
    {
        return std::clamp((kMoneyFloatLife - age).value / kFade.value, 0.0f, 1.0f);
    }

    f32 money_float_bob(usize letter, units::Seconds age, f32 line_height) noexcept
    {
        return (std::sin(static_cast<f32>(letter) * kBobStep.value - age.value * kBobSpeed.value) + 1) / 2 * kBobHeight * line_height;
    }

    MoneyFloaters::MoneyFloaters(std::string name)
        : Widget(std::move(name))
    {
    }

    void MoneyFloaters::draw(DrawContext& context) const
    {
        const auto font = context.fonts.get(font::FontRole::Medium);
        if (!font)
            return;
        const f32 height = font->line_height();
        constexpr std::array<Point, 4> kEdges { { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } } };
        for (const MoneyFloat& f : floats)
        {
            const auto alpha = static_cast<u8>(std::lround(money_float_alpha(f.age) * 255));
            Color colour = f.gain ? gain : spend;
            colour.a = static_cast<u8>(colour.a * alpha / 255);
            Color outline = edge;
            outline.a = static_cast<u8>(outline.a * alpha / 255);
            f32 x = f.at.x - text_width(*font, f.text) / 2;
            const f32 bottom = f.at.y - money_float_rise(f.age);
            for (usize i = 0; i < f.text.size(); ++i)
            {
                const std::u16string_view letter = std::u16string_view(f.text).substr(i, 1);
                const f32 width = text_width(*font, letter);
                const f32 y = bottom - height - money_float_bob(i, f.age, height);
                for (const Point d : kEdges)
                    draw_text(context.list, *font, letter, { x + d.x, y + d.y, x + d.x + width, y + d.y + height }, { .colour = outline });
                draw_text(context.list, *font, letter, { x, y, x + width, y + height }, { .colour = colour });
                x += width;
            }
        }
    }

} // namespace rct3::ui::widgets
