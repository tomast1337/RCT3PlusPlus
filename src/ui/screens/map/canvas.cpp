// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/map/canvas.hpp"

#include "core/types.hpp"
#include "ui/shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rct3::ui::screens::map
{

    namespace map_view
    {

        namespace
        {

            /// The size of the whole map at zoom 1: as large as fits in `area`, keeping its shape.
            Point fitted(const Rect& area, f32 aspect)
            {
                const f32 a = aspect > 0 ? aspect : 1.0f;
                if (area.width() / std::max(area.height(), 1e-3f) > a)
                    return { area.height() * a, area.height() };
                return { area.width(), area.width() / a };
            }

            Point middle(const Rect& r)
            {
                return { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
            }

        } // namespace

        Rect map_rect(const Rect& area, f32 aspect, const View& view)
        {
            const Point size = fitted(area, aspect);
            const f32 w = size.x * view.zoom;
            const f32 h = size.y * view.zoom;
            const Point c = middle(area);
            const f32 x0 = c.x - view.centre.x * w;
            const f32 y0 = c.y - view.centre.y * h;
            return { x0, y0, x0 + w, y0 + h };
        }

        Point to_map(const Rect& map, Point screen)
        {
            return { (screen.x - map.x0) / std::max(map.width(), 1e-3f), (screen.y - map.y0) / std::max(map.height(), 1e-3f) };
        }

        Point to_screen(const Rect& map, Point at)
        {
            return { map.x0 + at.x * map.width(), map.y0 + at.y * map.height() };
        }

        View clamped(const Rect& area, f32 aspect, View view)
        {
            view.zoom = std::clamp(view.zoom, 1.0f, kMaxZoom);
            const Rect map = map_rect(area, aspect, view);
            const auto keep = [](f32 centre, f32 shown, f32 whole) {
                if (whole <= shown)
                    return 0.5f;
                const f32 half = shown / (2 * whole);
                return std::clamp(centre, half, 1 - half);
            };
            view.centre = { keep(view.centre.x, area.width(), map.width()), keep(view.centre.y, area.height(), map.height()) };
            return view;
        }

        View zoomed(const Rect& area, f32 aspect, View view, f32 factor, Point pointer)
        {
            const Point at = to_map(map_rect(area, aspect, view), pointer);
            view.zoom = std::clamp(view.zoom * factor, 1.0f, kMaxZoom);
            const Point size = fitted(area, aspect);
            const Point c = middle(area);
            view.centre = { (c.x - pointer.x) / (size.x * view.zoom) + at.x, (c.y - pointer.y) / (size.y * view.zoom) + at.y };
            return clamped(area, aspect, view);
        }

        View following(const Rect& area, f32 aspect, View view, Point at)
        {
            const Rect map = map_rect(area, aspect, view);
            const auto follow = [](f32 centre, f32 point, f32 shown, f32 whole) {
                const f32 half = shown / (2 * whole);
                const f32 margin = kFollowMargin * shown / whole;
                if (half <= margin)
                    return centre;
                return std::clamp(centre, point - half + margin, point + half - margin);
            };
            view.centre = { follow(view.centre.x, at.x, area.width(), map.width()), follow(view.centre.y, at.y, area.height(), map.height()) };
            return clamped(area, aspect, view);
        }

    } // namespace map_view

    namespace
    {

        using Corner = std::array<f32, 2>;

        /// A straight line from `a` to `b`, `width` across.
        void line(DrawList& list, Point a, Point b, f32 width, Color colour)
        {
            const f32 dx = b.x - a.x;
            const f32 dy = b.y - a.y;
            const f32 length = std::hypot(dx, dy);
            if (length < 1e-3f)
                return;
            const f32 nx = -dy / length * width / 2;
            const f32 ny = dx / length * width / 2;
            const std::array<Corner, 4> corners { { { a.x + nx, a.y + ny }, { b.x + nx, b.y + ny }, { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny } } };
            list.polygon(corners, colour);
        }

    } // namespace

    MapCanvas::MapCanvas(std::string name, MapCanvasStyle style)
        : Widget(std::move(name))
        , style_(style)
    {
    }

    std::optional<Point> MapCanvas::take_pick()
    {
        return std::exchange(pick_, std::nullopt);
    }

    void MapCanvas::pick(Point p)
    {
        const Point at = map_view::to_map(map_view::map_rect(rect(), aspect, view_), p);
        pick_ = Point { std::clamp(at.x, 0.0f, 1.0f), std::clamp(at.y, 0.0f, 1.0f) };
    }

    void MapCanvas::on_press(Point p)
    {
        pick(p);
    }

    void MapCanvas::on_drag(Point p)
    {
        pick(p);
    }

    void MapCanvas::on_hover(Point p)
    {
        pointer_ = p;
    }

    bool MapCanvas::on_wheel(Point notches)
    {
        if (notches.y == 0)
            return false;
        view_ = map_view::zoomed(rect(), aspect, view_, std::pow(map_view::kZoomPerNotch, notches.y), pointer_);
        return true;
    }

    void MapCanvas::update(const UpdateContext&)
    {
        // Not while dragging: the view would slide under the pointer and carry the camera on with it.
        view_ = pressed() ? map_view::clamped(rect(), aspect, view_) : map_view::following(rect(), aspect, view_, camera.target);
    }

    void MapCanvas::draw(DrawContext& context) const
    {
        DrawList& list = context.list;
        Shape back;
        back.rect = rect();
        back.corners = round_corners(style_.corner);
        back.top = style_.back;
        back.bottom = style_.back;
        list.shape(back);
        if (!list.push_clip(rect()))
        {
            list.pop_clip();
            return;
        }
        const Rect map = map_view::map_rect(rect(), aspect, view_);
        if (picture != 0)
            list.image(map, picture, { 0, 0, 1, 1 });
        if (people != 0)
            list.image(map, people, { 0, 0, 1, 1 });
        // The camera only over the map, not over the bands beside it.
        list.push_clip(map);

        if (camera.sees.size() >= 3)
        {
            std::vector<Corner> corners;
            for (const Point& p : camera.sees)
            {
                const Point s = map_view::to_screen(map, p);
                corners.push_back({ s.x, s.y });
            }
            list.polygon(corners, style_.view_fill);
            for (usize i = 0; i < camera.sees.size(); ++i)
                line(list, map_view::to_screen(map, camera.sees[i]), map_view::to_screen(map, camera.sees[(i + 1) % camera.sees.size()]), style_.view_line_width, style_.view_line);
        }

        const Point at = map_view::to_screen(map, camera.target);
        const f32 length = std::hypot(camera.facing.x * map.width(), camera.facing.y * map.height());
        if (length > 1e-6f)
        {
            // A wedge from the marker the way the camera looks.
            const f32 fx = camera.facing.x * map.width() / length;
            const f32 fy = camera.facing.y * map.height() / length;
            const f32 half = style_.marker_size / 2;
            const Point tip { at.x + fx * style_.pointer_length, at.y + fy * style_.pointer_length };
            const std::array<Corner, 3> wedge { { { tip.x, tip.y }, { at.x - fy * half, at.y + fx * half }, { at.x + fy * half, at.y - fx * half } } };
            list.polygon(wedge, style_.marker_line);
        }
        Shape marker;
        const f32 half = style_.marker_size / 2;
        marker.rect = { at.x - half, at.y - half, at.x + half, at.y + half };
        marker.corners = round_corners(half);
        marker.top = style_.marker;
        marker.bottom = style_.marker;
        marker.outline = style_.marker_line;
        marker.outline_width = style_.marker_outline;
        list.shape(marker);
        list.pop_clip();
        list.pop_clip();
    }

} // namespace rct3::ui::screens::map
