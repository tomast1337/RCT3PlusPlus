// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/colour_palette.hpp"

#include <algorithm>
#include <format>

namespace rct3::ui
{

    ColourPalette::ColourPalette(std::string name, ColourPaletteStyle style, std::size_t count, std::size_t columns, std::size_t current)
        : Widget(std::move(name))
        , style_(std::move(style))
        , current_(current)
    {
        columns = std::max<std::size_t>(columns, 1);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::string& ring = i == current ? style_.current_ring : style_.ring;
            Button& b = add<Button>(std::format("swatch{}", i), ring, std::format("{}{}", style_.swatch_prefix, i));
            const float x = style_.margin + style_.pitch * static_cast<float>(i % columns);
            const float y = style_.margin + style_.pitch * static_cast<float>(i / columns);
            b.placement = top_left(x, y, style_.swatch, style_.swatch);
            b.swell = style_.swell;
            b.icon_scale = style_.picture_scale;
            swatches_.push_back(&b);
        }
    }

    Point ColourPalette::size(const ColourPaletteStyle& style, std::size_t count, std::size_t columns)
    {
        columns = std::max<std::size_t>(columns, 1);
        const std::size_t rows = (count + columns - 1) / columns;
        const auto span = [&](std::size_t n) { return n == 0 ? 0.0f : style.pitch * static_cast<float>(n - 1) + style.swatch; };
        return { 2 * style.margin + span(std::min(count, columns)), 2 * style.margin + span(rows) };
    }

    std::optional<std::size_t> ColourPalette::take_pick()
    {
        // Read from the swatches when asked, so a pick made this update is not left for the next.
        std::optional<std::size_t> picked;
        for (std::size_t i = 0; i < swatches_.size(); ++i)
        {
            if (swatches_[i]->take_click())
                picked = i;
        }
        return picked;
    }

    void ColourPalette::draw(DrawContext& context) const
    {
        if (!style_.back.empty())
            draw_skin(context.list, context.skins.find(style_.back), rect());
    }

} // namespace rct3::ui
