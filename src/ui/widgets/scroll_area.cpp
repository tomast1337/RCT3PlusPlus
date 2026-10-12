// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/scroll_area.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {

        // The right and bottom edges of anything visible below `w`, no less than `floor`.
        Point reach_of_children(const Widget& w, Point floor)
        {
            Point reach = floor;
            for (const auto& c : w.children())
            {
                if (!c->visible)
                    continue;
                const Rect e = c->extent();
                const Point inner = reach_of_children(*c, floor);
                reach = { std::max({ reach.x, e.x1, inner.x }), std::max({ reach.y, e.y1, inner.y }) };
            }
            return reach;
        }

        float thickness(const ScrollBar* bar)
        {
            return bar != nullptr ? bar->style().thickness : 0;
        }

    } // namespace

    ScrollArea::ScrollArea(std::string name, std::optional<ScrollBarStyle> vertical, std::optional<ScrollBarStyle> horizontal)
        : Widget(std::move(name))
    {
        clip_children = true;
        view_ = &add<Widget>("view");
        view_->clip_children = true;
        content_ = &view_->add<Widget>("content");
        if (vertical)
            vertical_ = &add<ScrollBar>("vertical", std::move(*vertical), Orientation::Vertical);
        if (horizontal)
            horizontal_ = &add<ScrollBar>("horizontal", std::move(*horizontal), Orientation::Horizontal);

        // The bars leave the bottom-right corner to each other.
        const float across_v = thickness(vertical_);
        const float across_h = thickness(horizontal_);
        if (vertical_ != nullptr)
            vertical_->placement = { { 1, 0, 1, 1 }, { -across_v, 0, 0, -across_h } };
        if (horizontal_ != nullptr)
            horizontal_->placement = { { 0, 1, 1, 1 }, { 0, -across_h, -across_v, 0 } };
        view_->placement = { { 0, 0, 1, 1 }, { 0, 0, -across_v, -across_h } };
        place_content();
    }

    bool ScrollArea::on_wheel(Point notches)
    {
        if (vertical_ != nullptr && vertical_->on_wheel(notches))
            return true;
        if (horizontal_ == nullptr)
            return false;
        // With nothing to scroll vertically, the usual wheel scrolls sideways.
        const Point sideways = vertical_ == nullptr ? Point { notches.x + notches.y, 0 } : notches;
        return horizontal_->on_wheel(sideways);
    }

    bool ScrollArea::on_key(const KeyPress& key)
    {
        ScrollBar* along = vertical_ != nullptr ? vertical_ : horizontal_;
        if (key.key == Key::Left || key.key == Key::Right)
            along = horizontal_;
        if (along == nullptr || along->range() <= 0)
            return false;
        const float step = along->style().step;
        const float page = std::max(step, along->visible - step);
        switch (key.key)
        {
            case Key::Up:
            case Key::Left:
                along->scroll_by(-step);
                return true;
            case Key::Down:
            case Key::Right:
                along->scroll_by(step);
                return true;
            case Key::PageUp:
                along->scroll_by(-page);
                return true;
            case Key::PageDown:
                along->scroll_by(page);
                return true;
            case Key::Home:
                along->scroll_by(-along->range());
                return true;
            case Key::End:
                along->scroll_by(along->range());
                return true;
            default:
                return false;
        }
    }

    void ScrollArea::update(const UpdateContext&)
    {
        // Measured as laid out last time; the new position applies at the next layout.
        const Rect& c = content_->rect();
        const Point reach = reach_of_children(*content_, { c.x0, c.y0 });
        if (vertical_ != nullptr)
        {
            vertical_->visible = view_->rect().height();
            vertical_->content = std::max(vertical_->visible, reach.y - c.y0);
            vertical_->clamp();
        }
        if (horizontal_ != nullptr)
        {
            horizontal_->visible = view_->rect().width();
            horizontal_->content = std::max(horizontal_->visible, reach.x - c.x0);
            horizontal_->clamp();
        }
        place_content();
    }

    void ScrollArea::place_content()
    {
        Placement& p = content_->placement;
        if (horizontal_ != nullptr)
        {
            const float x = -horizontal_->position;
            p.anchors.x0 = 0;
            p.anchors.x1 = 0;
            p.offsets.x0 = x;
            p.offsets.x1 = x + std::max(horizontal_->content, horizontal_->visible);
        }
        else
        {
            p.anchors.x0 = 0;
            p.anchors.x1 = 1;
            p.offsets.x0 = 0;
            p.offsets.x1 = 0;
        }
        // Vertically the content keeps the view's height; children below it are what scrolls.
        const float y = vertical_ != nullptr ? -vertical_->position : 0;
        p.anchors.y0 = 0;
        p.anchors.y1 = 1;
        p.offsets.y0 = y;
        p.offsets.y1 = y;
    }

} // namespace rct3::ui
