// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/window.hpp"

namespace rct3::ui
{

    Window::Window(std::string name, const WindowStyle& style, float width, float height)
        : Widget(std::move(name))
        , style_(style)
    {
        placement = centred(width, height);
        keep_inside = true;

        content_ = &add<Widget>("content");
        const Rect& in = style_.content_inset;
        content_->placement = { { 0, 0, 1, 1 }, { in.x0, in.y0, -in.x1, -in.y1 } };

        close_ = &add<Button>("close", style_.title_button, style_.close_icon);
        close_->placement = top_right(style_.title_button_margin.x, style_.title_button_margin.y, style_.title_button_size, style_.title_button_size);
    }

    void Window::on_press(Point p)
    {
        dragging_ = p.y < rect().y0 + style_.title_height;
        grab_ = p;
    }

    void Window::on_drag(Point p)
    {
        if (!dragging_)
            return;
        Rect& o = placement.offsets;
        const float dx = p.x - grab_.x;
        const float dy = p.y - grab_.y;
        o = { o.x0 + dx, o.y0 + dy, o.x1 + dx, o.y1 + dy };
        grab_ = p;
    }

    void Window::on_release(Point, bool)
    {
        dragging_ = false;
    }

    void Window::update()
    {
        if (close_->take_click())
            close();
    }

    void Window::draw(DrawContext& context) const
    {
        const Rect& r = rect();
        const float in = style_.background_inset;
        draw_skin(context.list, context.skins.find(style_.background), { r.x0 + in, r.y0 + in, r.x1 - in, r.y1 - in });
        draw_skin(context.list, context.skins.find(focused() ? style_.frame : style_.frame_unfocused), r);
    }

} // namespace rct3::ui
