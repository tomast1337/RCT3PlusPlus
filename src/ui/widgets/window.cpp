// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/window.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>

namespace rct3::ui
{

    namespace
    {
        constexpr Point kDefaultMinSize { 160, 120 };
    } // namespace

    Window::Window(std::string name, const WindowStyle& style, float width, float height)
        : Widget(std::move(name))
        , style_(style)
    {
        placement = centred(width, height);
        keep_inside = true;
        min_size = { std::min(width, kDefaultMinSize.x), std::min(height, kDefaultMinSize.y) };

        content_ = &add<Widget>("content");
        const Rect& in = style_.content_inset;
        content_->placement = { { 0, 0, 1, 1 }, { in.x0, in.y0, -in.x1, -in.y1 } };

        close_ = &add<Button>("close", style_.title_button, style_.close_icon);
        close_->placement = top_right(style_.title_button_margin.x, style_.title_button_margin.y, style_.title_button_size, style_.title_button_size);
    }

    std::uint8_t Window::edges_at(Point p) const
    {
        if (!resizable)
            return 0;
        const Rect& r = rect();
        const float band = style_.resize_band;
        const float corner = style_.resize_corner;
        // Near a corner, the band widens along both edges so corners are easy to hit.
        const bool near_left = p.x < r.x0 + corner;
        const bool near_right = p.x >= r.x1 - corner;
        const bool near_top = p.y < r.y0 + corner;
        const bool near_bottom = p.y >= r.y1 - corner;
        std::uint8_t edges = 0;
        if (p.x < r.x0 + band || (near_left && (p.y < r.y0 + band || p.y >= r.y1 - band)))
            edges |= Left;
        if (p.x >= r.x1 - band || (near_right && (p.y < r.y0 + band || p.y >= r.y1 - band)))
            edges |= Right;
        if (p.y < r.y0 + band || (near_top && (p.x < r.x0 + band || p.x >= r.x1 - band)))
            edges |= Top;
        if (p.y >= r.y1 - band || (near_bottom && (p.x < r.x0 + band || p.x >= r.x1 - band)))
            edges |= Bottom;
        return edges;
    }

    void Window::resize_to(Point p)
    {
        const Rect& s = start_rect_;
        const float dx = p.x - grab_.x;
        const float dy = p.y - grab_.y;
        const Rect bounds = parent() != nullptr ? parent()->rect() : s;
        const float min_w = std::min(min_size.x, s.width());
        const float min_h = std::min(min_size.y, s.height());

        Rect r = s;
        if ((edges_ & Left) != 0)
            r.x0 = std::clamp(s.x0 + dx, bounds.x0, s.x1 - min_w);
        if ((edges_ & Right) != 0)
            r.x1 = std::clamp(s.x1 + dx, s.x0 + min_w, bounds.x1);
        if ((edges_ & Top) != 0)
            r.y0 = std::clamp(s.y0 + dy, bounds.y0, s.y1 - min_h);
        if ((edges_ & Bottom) != 0)
            r.y1 = std::clamp(s.y1 + dy, s.y0 + min_h, bounds.y1);

        // Move the placement's edges by as much as the rectangle's, whatever they are anchored to.
        const Rect& now = rect();
        Rect& o = placement.offsets;
        o = { o.x0 + r.x0 - now.x0, o.y0 + r.y0 - now.y0, o.x1 + r.x1 - now.x1, o.y1 + r.y1 - now.y1 };
    }

    void Window::on_press(Point p)
    {
        grab_ = p;
        edges_ = edges_at(p);
        start_rect_ = rect();
        dragging_ = edges_ == 0 && p.y < rect().y0 + style_.title_height;
    }

    void Window::on_drag(Point p)
    {
        if (edges_ != 0)
        {
            resize_to(p);
            return;
        }
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
        edges_ = 0;
    }

    void Window::update(const UpdateContext&)
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

        // Three steps of small squares along the diagonal of the bottom-right corner.
        if (resizable)
        {
            constexpr float kDot = 2;
            constexpr float kPitch = 4;
            const float x = r.x1 - style_.resize_band - kDot;
            const float y = r.y1 - style_.resize_band - kDot;
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col <= row; ++col)
                {
                    const float px = x - static_cast<float>(col) * kPitch;
                    const float py = y - static_cast<float>(row - col) * kPitch;
                    context.list.fill({ px, py, px + kDot, py + kDot }, style_.grip_colour);
                }
            }
        }

        const Font* font = context.fonts.get(style_.title_font);
        if (font == nullptr || title.empty())
            return;
        const Rect& t = style_.title_box;
        const Rect box { r.x0 + t.x0, r.y0 + t.y0, r.x1 - t.x1, r.y0 + t.y1 };
        if (context.list.push_clip(box))
            draw_text(context.list, *font, title, box, { style_.title_colour, Align::Start, Align::End, false });
        context.list.pop_clip();
    }

} // namespace rct3::ui
