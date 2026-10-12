// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/toolbar.hpp"

#include <cmath>

namespace rct3::ui
{

    namespace
    {

        // Marks room between buttons; the toolbar draws its line.
        class Separator : public Widget
        {
        public:
            using Widget::Widget;
        };

        bool is_separator(const Widget& w)
        {
            return dynamic_cast<const Separator*>(&w) != nullptr;
        }

    } // namespace

    Toolbar::Toolbar(std::string name, ToolbarStyle style, Orientation orientation)
        : Widget(std::move(name))
        , style_(std::move(style))
        , orientation_(orientation)
    {
    }

    Button& Toolbar::add_button(std::string name, std::string icon, std::u16string tooltip)
    {
        Button& b = add<Button>(std::move(name), style_.button, std::move(icon));
        b.tooltip = std::move(tooltip);
        arrange();
        return b;
    }

    void Toolbar::add_separator()
    {
        add<Separator>("separator" + std::to_string(separators_++));
        arrange();
    }

    float Toolbar::length() const
    {
        const bool across = orientation_ == Orientation::Horizontal;
        float along = across ? style_.padding.x0 + style_.padding.x1 : style_.padding.y0 + style_.padding.y1;
        bool first = true;
        for (const auto& c : children())
        {
            if (!c->visible)
                continue;
            if (!first)
                along += style_.gap;
            along += is_separator(*c) ? style_.separator_room : style_.button_size;
            first = false;
        }
        return along;
    }

    void Toolbar::arrange()
    {
        const bool horizontal = orientation_ == Orientation::Horizontal;
        float along = horizontal ? style_.padding.x0 : style_.padding.y0;
        for (const auto& c : children())
        {
            if (!c->visible)
                continue;
            const float size = is_separator(*c) ? style_.separator_room : style_.button_size;
            c->placement = horizontal ? top_left(along, style_.padding.y0, size, style_.button_size) : top_left(style_.padding.x0, along, style_.button_size, size);
            along += size + style_.gap;
        }
    }

    void Toolbar::update(const UpdateContext&)
    {
        arrange(); // Buttons shown or hidden since the last update close up
    }

    void Toolbar::draw(DrawContext& context) const
    {
        if (!style_.background.empty())
            draw_skin(context.list, context.skins.find(style_.background), rect());
        const bool horizontal = orientation_ == Orientation::Horizontal;
        for (const auto& c : children())
        {
            if (!c->visible || !is_separator(*c))
                continue;
            const Rect& r = c->rect();
            if (horizontal)
            {
                const float x = std::round((r.x0 + r.x1) / 2);
                context.list.fill({ x, r.y0 + 2, x + 1, r.y1 - 2 }, style_.separator_colour);
            }
            else
            {
                const float y = std::round((r.y0 + r.y1) / 2);
                context.list.fill({ r.x0 + 2, y, r.x1 - 2, y + 1 }, style_.separator_colour);
            }
        }
    }

} // namespace rct3::ui
