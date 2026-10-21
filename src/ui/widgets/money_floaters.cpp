// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/money_floaters.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rct3::ui
{

    namespace
    {
        constexpr float kRiseSpeed = 24;    ///< Interface units a second
        constexpr float kFade = 0.8f;       ///< The last seconds, over which it fades
        constexpr float kBobHeight = 0.18f; ///< Of the line height
        constexpr float kBobStep = 1.1f;    ///< Phase between one letter and the next, in radians
        constexpr float kBobSpeed = 9;      ///< Radians a second
    } // namespace

    float money_float_rise(float age) noexcept
    {
        return std::max(age, 0.0f) * kRiseSpeed;
    }

    float money_float_alpha(float age) noexcept
    {
        return std::clamp((kMoneyFloatLife - age) / kFade, 0.0f, 1.0f);
    }

    float money_float_bob(std::size_t letter, float age, float line_height) noexcept
    {
        return (std::sin(static_cast<float>(letter) * kBobStep - age * kBobSpeed) + 1) / 2 * kBobHeight * line_height;
    }

    MoneyFloaters::MoneyFloaters(std::string name)
        : Widget(std::move(name))
    {
    }

    void MoneyFloaters::draw(DrawContext& context) const
    {
        const auto font = context.fonts.get(FontRole::Medium);
        if (!font)
            return;
        const float height = font->line_height();
        constexpr std::array<Point, 4> kEdges { { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } } };
        for (const MoneyFloat& f : floats)
        {
            const auto alpha = static_cast<std::uint8_t>(std::lround(money_float_alpha(f.age) * 255));
            Color colour = f.gain ? gain : spend;
            colour.a = static_cast<std::uint8_t>(colour.a * alpha / 255);
            Color outline = edge;
            outline.a = static_cast<std::uint8_t>(outline.a * alpha / 255);
            float x = f.at.x - text_width(*font, f.text) / 2;
            const float bottom = f.at.y - money_float_rise(f.age);
            for (std::size_t i = 0; i < f.text.size(); ++i)
            {
                const std::u16string_view letter = std::u16string_view(f.text).substr(i, 1);
                const float width = text_width(*font, letter);
                const float y = bottom - height - money_float_bob(i, f.age, height);
                for (const Point d : kEdges)
                    draw_text(context.list, *font, letter, { x + d.x, y + d.y, x + d.x + width, y + d.y + height }, { .colour = outline });
                draw_text(context.list, *font, letter, { x, y, x + width, y + height }, { .colour = colour });
                x += width;
            }
        }
    }

} // namespace rct3::ui
