// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/world_labels.hpp"

#include "ui/text_layout.hpp"

namespace rct3::ui
{

    namespace
    {
        constexpr float kPad = 2; ///< Round the text. Our own value.
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
            const float half = text_width(*font, l.text) / 2 + kPad;
            const Rect box { l.at.x - half, l.at.y - font->line_height() - 2 * kPad, l.at.x + half, l.at.y };
            context.list.fill(box, back);
            draw_text(context.list, *font, l.text, box, style);
        }
    }

} // namespace rct3::ui
