// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/tickbox.hpp"

namespace rct3::ui
{

    Tickbox::Tickbox(std::string name, std::string box, std::string tick)
        : Widget(std::move(name))
        , box(std::move(box))
        , tick(std::move(tick))
    {
    }

    bool Tickbox::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    void Tickbox::on_release(Point, bool inside)
    {
        if (!inside)
            return;
        ticked = !ticked;
        ++changes_;
    }

    void Tickbox::draw(DrawContext& context) const
    {
        const Color tint = !enabled ? Color { 128, 128, 128, 160 } : pressed() ? Color { 190, 190, 190, 255 } : kWhite;
        draw_skin(context.list, context.skins.find(box), rect(), tint);
        if (ticked)
            draw_skin(context.list, context.skins.find(tick), rect(), tint);
    }

} // namespace rct3::ui
