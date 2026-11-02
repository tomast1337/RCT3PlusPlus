// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/world_labels.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <ranges>

namespace rct3::ui
{

    namespace
    {
        constexpr float kPad = 2; ///< Round the text.
    } // namespace

    WorldLabels::WorldLabels(std::string name)
        : Widget(std::move(name))
    {
    }

    void WorldLabels::draw(DrawContext& context) const
    {
        const auto font = context.fonts.get(FontRole::Small);
        if (!font)
            return;
        const TextStyle style { .colour = text, .horizontal = Align::Centre, .vertical = Align::Centre };
        for (const WorldLabel& l : labels)
        {
            // A line of text each, centred over one another on one box.
            float widest = 0;
            std::size_t lines = 0;
            for (const auto line : std::views::split(l.text, u'\n'))
            {
                widest = std::max(widest, text_width(*font, std::u16string_view(line)));
                ++lines;
            }
            const float half = widest / 2 + kPad;
            const float tall = font->line_height();
            const Rect box { l.at.x - half, l.at.y - static_cast<float>(lines) * tall - 2 * kPad, l.at.x + half, l.at.y };
            context.list.fill(box, back);
            float top = box.y0 + kPad;
            for (const auto line : std::views::split(l.text, u'\n'))
            {
                draw_text(context.list, *font, std::u16string_view(line), { box.x0, top, box.x1, top + tall }, style);
                top += tall;
            }
        }
    }

} // namespace rct3::ui
