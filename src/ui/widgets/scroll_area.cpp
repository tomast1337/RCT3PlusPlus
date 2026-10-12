// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/scroll_area.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {

        // The lowest edge of anything visible below `w`.
        float bottom_of_children(const Widget& w, float floor)
        {
            float bottom = floor;
            for (const auto& c : w.children())
            {
                if (!c->visible)
                    continue;
                bottom = std::max({ bottom, c->extent().y1, bottom_of_children(*c, floor) });
            }
            return bottom;
        }

    } // namespace

    ScrollArea::ScrollArea(std::string name, ScrollBarStyle bar_style)
        : Widget(std::move(name))
    {
        clip_children = true;
        const float thickness = bar_style.thickness;
        content_ = &add<Widget>("content");
        content_->placement = { { 0, 0, 1, 1 }, { 0, 0, -thickness, 0 } };
        bar_ = &add<ScrollBar>("bar", std::move(bar_style), Orientation::Vertical);
        bar_->placement = { { 1, 0, 1, 1 }, { -thickness, 0, 0, 0 } };
    }

    bool ScrollArea::on_wheel(float notches)
    {
        return bar_->on_wheel(notches);
    }

    void ScrollArea::update(const UpdateContext&)
    {
        // Measured as laid out last time; the new position applies at the next layout.
        const float top = content_->rect().y0;
        bar_->visible = rect().height();
        bar_->content = std::max(bar_->visible, bottom_of_children(*content_, top) - top);
        bar_->clamp();
        const float thickness = bar_->style().thickness;
        content_->placement = { { 0, 0, 1, 1 }, { 0, -bar_->position, -thickness, -bar_->position } };
    }

} // namespace rct3::ui
