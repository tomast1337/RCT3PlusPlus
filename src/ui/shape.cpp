// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/shape.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {

        struct Rgba
        {
            float r;
            float g;
            float b;
            float a;
        };

        Rgba unit(Color c)
        {
            return { static_cast<float>(c.r) / 255, static_cast<float>(c.g) / 255, static_cast<float>(c.b) / 255, static_cast<float>(c.a) / 255 };
        }

        Color bytes(const Rgba& c)
        {
            const auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
            return { byte(c.r), byte(c.g), byte(c.b), byte(c.a) };
        }

        Rgba mix(const Rgba& a, const Rgba& b, float t)
        {
            return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
        }

        // `top` laid over `under` at `amount` of its own alpha, straight alpha in and out.
        Rgba over(const Rgba& under, const Rgba& top, float amount)
        {
            const float a = top.a * amount;
            const float out = a + under.a * (1 - a);
            if (out <= 0)
                return { 0, 0, 0, 0 };
            const auto channel = [&](float t, float u) { return (t * a + u * under.a * (1 - a)) / out; };
            return { channel(top.r, under.r), channel(top.g, under.g), channel(top.b, under.b), out };
        }

        float sign(float v)
        {
            return v < 0 ? -1.0f : 1.0f;
        }

        // The radius of the corner on the side of (x, y).
        float corner_radius(const Corners& c, float x, float y)
        {
            if (y < 0)
                return x < 0 ? c.top_left : c.top_right;
            return x < 0 ? c.bottom_left : c.bottom_right;
        }

        // How much the edge nearest (x, y) faces up, 0 to 1: the outward direction of the rounded box, turned
        // round on a ring's inner edge.
        float facing_up(const ShapeMetrics& m, float x, float y)
        {
            const float r = corner_radius(m.corners, x, y);
            const float qx = std::abs(x) - m.half_width + r;
            const float qy = std::abs(y) - m.half_height + r;
            float ny = 0;
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

    ShapeMetrics shape_metrics(const Shape& shape, float scale)
    {
        ShapeMetrics m;
        m.half_width = std::max(shape.rect.width(), 0.0f) * scale / 2;
        m.half_height = std::max(shape.rect.height(), 0.0f) * scale / 2;
        const float most = std::min(m.half_width, m.half_height);
        const auto radius = [&](float r) { return std::clamp(r * scale, 0.0f, most); };
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

    float shape_distance(const ShapeMetrics& m, float x, float y)
    {
        // The rounded box: the distance to a box shrunk by the corner's radius, less the radius.
        const float r = corner_radius(m.corners, x, y);
        const float qx = std::abs(x) - m.half_width + r;
        const float qy = std::abs(y) - m.half_height + r;
        const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
        float d = outside + std::min(std::max(qx, qy), 0.0f) - r;
        // A ring is the band of the box's inside within `ring` of its edge.
        if (m.ring > 0)
            d = std::abs(d + m.ring / 2) - m.ring / 2;
        // Inside the cut is outside the shape.
        if (m.cut_radius > 0)
            d = std::max(d, m.cut_radius - std::hypot(x - m.cut_x, y - m.cut_y));
        return d;
    }

    float shape_coverage(const ShapeMetrics& m, float distance)
    {
        // A one pixel ramp centred on the edge smooths it; a soft edge spreads the ramp and eases its ends.
        const float width = std::max(m.softness, 1.0f);
        const float t = std::clamp(0.5f - distance / width, 0.0f, 1.0f);
        return m.softness > 1 ? t * t * (3 - 2 * t) : t;
    }

    Color shade_shape(const Shape& shape, const ShapeMetrics& m, float x, float y)
    {
        const float d = shape_distance(m, x, y);
        const float coverage = shape_coverage(m, d);
        if (coverage <= 0)
            return { 0, 0, 0, 0 };
        const float share = m.half_height > 0 ? (y + m.half_height) / (2 * m.half_height) : 0.0f;
        const float span = m.fill_to - m.fill_from;
        const float along = std::clamp(span > 0 ? (share - m.fill_from) / span : share < m.fill_from ? 0.0f : 1.0f, 0.0f, 1.0f);
        Rgba c = mix(unit(shape.top), unit(shape.bottom), along);
        const float inside = -d;
        if (m.light_width > 0)
        {
            const float fade = std::clamp(1 - inside / m.light_width, 0.0f, 1.0f);
            c = over(c, unit(shape.light), fade * facing_up(m, x, y));
        }
        if (m.outline_width > 0)
            c = over(c, unit(shape.outline), std::clamp(m.outline_width - inside + 0.5f, 0.0f, 1.0f));
        c.a *= coverage;
        return bytes(c);
    }

    float shape_margin(const ShapeMetrics& m)
    {
        return m.softness / 2 + 1;
    }

} // namespace rct3::ui
