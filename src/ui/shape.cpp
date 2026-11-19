// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/shape.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {

        struct Rgba
        {
            f32 r;
            f32 g;
            f32 b;
            f32 a;
        };

        Rgba unit(Color c)
        {
            return { static_cast<f32>(c.r) / 255, static_cast<f32>(c.g) / 255, static_cast<f32>(c.b) / 255, static_cast<f32>(c.a) / 255 };
        }

        Color bytes(const Rgba& c)
        {
            const auto byte = [](f32 v) { return static_cast<u8>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
            return { byte(c.r), byte(c.g), byte(c.b), byte(c.a) };
        }

        Rgba mix(const Rgba& a, const Rgba& b, f32 t)
        {
            return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
        }

        // `top` laid over `under` at `amount` of its own alpha, straight alpha in and out.
        Rgba over(const Rgba& under, const Rgba& top, f32 amount)
        {
            const f32 a = top.a * amount;
            const f32 out = a + under.a * (1 - a);
            if (out <= 0)
                return { 0, 0, 0, 0 };
            const auto channel = [&](f32 t, f32 u) { return (t * a + u * under.a * (1 - a)) / out; };
            return { channel(top.r, under.r), channel(top.g, under.g), channel(top.b, under.b), out };
        }

        f32 sign(f32 v)
        {
            return v < 0 ? -1.0f : 1.0f;
        }

        // The radius of the corner on the side of (x, y).
        f32 corner_radius(const Corners& c, f32 x, f32 y)
        {
            if (y < 0)
                return x < 0 ? c.top_left : c.top_right;
            return x < 0 ? c.bottom_left : c.bottom_right;
        }

        // How much the edge nearest (x, y) faces up, 0 to 1: the outward direction of the rounded box, turned
        // round on a ring's inner edge.
        f32 facing_up(const ShapeMetrics& m, f32 x, f32 y)
        {
            const f32 r = corner_radius(m.corners, x, y);
            const f32 qx = std::abs(x) - m.half_width + r;
            const f32 qy = std::abs(y) - m.half_height + r;
            f32 ny = 0;
            if (qx > 0 && qy > 0)
                ny = sign(y) * qy / std::hypot(qx, qy);
            else if (qy >= qx)
                ny = sign(y);
            if (m.ring > 0)
            {
                ShapeMetrics outline = m;
                outline.ring = 0;
                if (shape_distance(outline, x, y) < -m.ring / 2)
                    ny = -ny;
            }
            return std::clamp(-ny, 0.0f, 1.0f);
        }

    } // namespace

    ShapeMetrics shape_metrics(const Shape& shape, f32 scale)
    {
        ShapeMetrics m;
        m.half_width = std::max(shape.rect.width(), 0.0f) * scale / 2;
        m.half_height = std::max(shape.rect.height(), 0.0f) * scale / 2;
        const f32 most = std::min(m.half_width, m.half_height);
        const auto radius = [&](f32 r) { return std::clamp(r * scale, 0.0f, most); };
        m.corners = { radius(shape.corners.top_left), radius(shape.corners.top_right), radius(shape.corners.bottom_right), radius(shape.corners.bottom_left) };
        m.ring = std::max(shape.ring * scale, 0.0f);
        m.softness = std::max(shape.softness * scale, 0.0f);
        m.outline_width = std::max(shape.outline_width * scale, 0.0f);
        m.light_width = std::max(shape.light_width * scale, 0.0f);
        m.fill_from = shape.fill_from;
        m.fill_to = shape.fill_to;
        if (shape.cut_radius > 0)
        {
            m.cut_x = (shape.cut_x - (shape.rect.x0 + shape.rect.x1) / 2) * scale;
            m.cut_y = (shape.cut_y - (shape.rect.y0 + shape.rect.y1) / 2) * scale;
            m.cut_radius = shape.cut_radius * scale;
        }
        return m;
    }

    f32 shape_distance(const ShapeMetrics& m, f32 x, f32 y)
    {
        // The rounded box: the distance to a box shrunk by the corner's radius, less the radius.
        const f32 r = corner_radius(m.corners, x, y);
        const f32 qx = std::abs(x) - m.half_width + r;
        const f32 qy = std::abs(y) - m.half_height + r;
        const f32 outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
        f32 d = outside + std::min(std::max(qx, qy), 0.0f) - r;
        // A ring is the band of the box's inside within `ring` of its edge.
        if (m.ring > 0)
            d = std::abs(d + m.ring / 2) - m.ring / 2;
        // Inside the cut is outside the shape.
        if (m.cut_radius > 0)
            d = std::max(d, m.cut_radius - std::hypot(x - m.cut_x, y - m.cut_y));
        return d;
    }

    f32 shape_coverage(const ShapeMetrics& m, f32 distance)
    {
        // A one pixel ramp centred on the edge smooths it; a soft edge spreads the ramp and eases its ends.
        const f32 width = std::max(m.softness, 1.0f);
        const f32 t = std::clamp(0.5f - distance / width, 0.0f, 1.0f);
        return m.softness > 1 ? t * t * (3 - 2 * t) : t;
    }

    Color shade_shape(const Shape& shape, const ShapeMetrics& m, f32 x, f32 y)
    {
        const f32 d = shape_distance(m, x, y);
        const f32 coverage = shape_coverage(m, d);
        if (coverage <= 0)
            return { 0, 0, 0, 0 };
        const f32 share = m.half_height > 0 ? (y + m.half_height) / (2 * m.half_height) : 0.0f;
        const f32 span = m.fill_to - m.fill_from;
        const f32 along = std::clamp(span > 0 ? (share - m.fill_from) / span : share < m.fill_from ? 0.0f : 1.0f, 0.0f, 1.0f);
        Rgba c = mix(unit(shape.top), unit(shape.bottom), along);
        const f32 inside = -d;
        if (m.light_width > 0)
        {
            const f32 fade = std::clamp(1 - inside / m.light_width, 0.0f, 1.0f);
            c = over(c, unit(shape.light), fade * facing_up(m, x, y));
        }
        if (m.outline_width > 0)
            c = over(c, unit(shape.outline), std::clamp(m.outline_width - inside + 0.5f, 0.0f, 1.0f));
        c.a *= coverage;
        return bytes(c);
    }

    f32 shape_margin(const ShapeMetrics& m)
    {
        return m.softness / 2 + 1;
    }

} // namespace rct3::ui
