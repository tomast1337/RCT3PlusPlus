// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/panel.hpp"

namespace rct3::ui
{

    Panel::Panel(std::string name, std::string skin)
        : Widget(std::move(name))
        , skin(std::move(skin))
    {
    }

    void Panel::draw(DrawContext& context) const
    {
        if (colour)
            context.list.fill(rect(), *colour);
        if (!skin.empty())
            draw_skin(context.list, context.skins.find(skin), rect(), tint);
    }

} // namespace rct3::ui
