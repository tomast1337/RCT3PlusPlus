// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/colour_palette.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui::widgets
{

    ColourPalette::ColourPalette(std::string name, ColourPaletteStyle style, usize count, usize columns, usize current)
        : Widget(std::move(name))
        , style_(std::move(style))
        , current_(current)
    {
        columns = std::max<usize>(columns, 1);
        for (usize i = 0; i < count; ++i)
        {
            const std::string& ring = i == current ? style_.current_ring : style_.ring;
            const std::string swatch = std::format("{}{}", style_.swatch_prefix, i);
            Button& b = add<Button>(swatch, ring, swatch);
            const f32 x = style_.margin + style_.pitch * static_cast<f32>(i % columns);
            const f32 y = style_.margin + style_.pitch * static_cast<f32>(i / columns);
            b.placement = top_left(x, y, style_.swatch, style_.swatch);
            b.swell = style_.swell;
            b.icon_scale = style_.picture_scale;
            swatches_.push_back(&b);
        }
    }

    Point ColourPalette::size(const ColourPaletteStyle& style, usize count, usize columns)
    {
        columns = std::max<usize>(columns, 1);
        const usize rows = (count + columns - 1) / columns;
        const auto span = [&](usize n) { return n == 0 ? 0.0f : style.pitch * static_cast<f32>(n - 1) + style.swatch; };
        return { 2 * style.margin + span(std::min(count, columns)), 2 * style.margin + span(rows) };
    }

    std::optional<usize> ColourPalette::take_pick()
    {
        // Read from the swatches when asked, so a pick made this update is not left for the next.
        std::optional<usize> picked;
        for (usize i = 0; i < swatches_.size(); ++i)
        {
            if (swatches_[i]->take_click())
                picked = i;
        }
        return picked;
    }

    void ColourPalette::draw(DrawContext& context) const
    {
        if (!style_.back.empty())
            skin::draw_skin(context.list, context.skins.find(style_.back), rect());
    }

} // namespace rct3::ui::widgets
