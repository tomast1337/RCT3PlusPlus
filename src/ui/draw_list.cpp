// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list.hpp"

#include "ui/shape.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        float snap(float v)
        {
            return std::floor(v + 0.5f);
        }

        Rect snapped(const Rect& r, float scale)
        {
            return { snap(r.x0 * scale), snap(r.y0 * scale), snap(r.x1 * scale), snap(r.y1 * scale) };
        }

        float lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        Color lerp(Color a, Color b, float t)
        {
            const auto mix = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(lerp(static_cast<float>(x), static_cast<float>(y), t))); };
            return { mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a) };
        }

        // Colour of the gradient at fractions (fx, fy) of the rectangle, measured from the top-left.
        Color sample(const CornerColors& c, float fx, float fy)
        {
            return lerp(lerp(c.top_left, c.top_right, fx), lerp(c.bottom_left, c.bottom_right, fx), fy);
        }

        std::array<std::uint8_t, 4> packed(Color c)
        {
            return { c.r, c.g, c.b, c.a };
        }

        // Two triangles over a quad's corners, clockwise from the top-left.
        constexpr std::array<std::uint32_t, 6> kQuadCorners = { 0, 1, 2, 0, 2, 3 };

    } // namespace

    Rect intersect(const Rect& a, const Rect& b)
    {
        return { std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1) };
    }

    void DrawList::clear()
    {
        vertices_.clear();
        shape_vertices_.clear();
        indices_.clear();
        batches_.clear();
        clips_.clear();
    }

    void DrawList::fill(const Rect& rect, Color color)
    {
        quad(rect, 0, { }, { color, color, color, color });
    }

    void DrawList::gradient(const Rect& rect, const CornerColors& colors)
    {
        quad(rect, 0, { }, colors);
    }

    void DrawList::image(const Rect& rect, TextureId texture, const Rect& uv, Color tint)
    {
        quad(rect, texture, uv, { tint, tint, tint, tint });
    }

    void DrawList::quad(const Rect& rect, TextureId texture, const Rect& uv, const CornerColors& colors)
    {
        const Rect full = snapped(rect, scale_);
        if (full.empty())
            return;
        const Rect visible = clips_.empty() ? full : intersect(full, clips_.back());
        if (visible.empty())
            return;

        // Where the visible part starts and ends as fractions of the whole rectangle; texture coordinates
        // and colours are cut at the same fractions so the visible part looks as it did uncut.
        const float fx0 = (visible.x0 - full.x0) / full.width();
        const float fx1 = (visible.x1 - full.x0) / full.width();
        const float fy0 = (visible.y0 - full.y0) / full.height();
        const float fy1 = (visible.y1 - full.y0) / full.height();
        const float u0 = lerp(uv.x0, uv.x1, fx0);
        const float u1 = lerp(uv.x0, uv.x1, fx1);
        const float v0 = lerp(uv.y0, uv.y1, fy0);
        const float v1 = lerp(uv.y0, uv.y1, fy1);

        const auto base = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back({ visible.x0, visible.y0, packed(sample(colors, fx0, fy0)), u0, v0 });
        vertices_.push_back({ visible.x1, visible.y0, packed(sample(colors, fx1, fy0)), u1, v0 });
        vertices_.push_back({ visible.x1, visible.y1, packed(sample(colors, fx1, fy1)), u1, v1 });
        vertices_.push_back({ visible.x0, visible.y1, packed(sample(colors, fx0, fy1)), u0, v1 });

        add_indices(BatchKind::Quads, texture, base, kQuadCorners);
    }

    void DrawList::disc_image(const Rect& rect, TextureId texture, const Rect& uv, Color tint)
    {
        // Enough sides that the edge looks round at the sizes the interface draws.
        constexpr int kSides = 48;
        const Rect full = snapped(rect, scale_);
        if (full.empty())
            return;
        if (!clips_.empty())
        {
            const Rect& c = clips_.back();
            if (full.x0 < c.x0 || full.y0 < c.y0 || full.x1 > c.x1 || full.y1 > c.y1)
                return;
        }
        const auto colour = packed(tint);
        const auto at = [&](float fx, float fy) { return Vertex { lerp(full.x0, full.x1, fx), lerp(full.y0, full.y1, fy), colour, lerp(uv.x0, uv.x1, fx), lerp(uv.y0, uv.y1, fy) }; };
        const auto base = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(at(0.5f, 0.5f));
        for (int i = 0; i < kSides; ++i)
        {
            const float angle = 2 * std::numbers::pi_v<float> * static_cast<float>(i) / kSides;
            vertices_.push_back(at(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)));
        }
        std::array<std::uint32_t, 3 * kSides> fan { };
        for (std::uint32_t i = 0; i < kSides; ++i)
        {
            fan[3 * i] = 0;
            fan[3 * i + 1] = 1 + i;
            fan[3 * i + 2] = 1 + (i + 1) % kSides;
        }
        add_indices(BatchKind::Quads, texture, base, fan);
    }

    void DrawList::shape(const Shape& shape)
    {
        const ShapeMetrics m = shape_metrics(shape, scale_);
        if (m.half_width <= 0 || m.half_height <= 0)
            return;
        const float cx = (shape.rect.x0 + shape.rect.x1) / 2 * scale_;
        const float cy = (shape.rect.y0 + shape.rect.y1) / 2 * scale_;
        // Out to whole pixels past the soft edge, so no coloured pixel is cut off.
        const float margin = shape_margin(m);
        const Rect full { std::floor(cx - m.half_width - margin), std::floor(cy - m.half_height - margin), std::ceil(cx + m.half_width + margin), std::ceil(cy + m.half_height + margin) };
        const Rect visible = clips_.empty() ? full : intersect(full, clips_.back());
        if (visible.empty())
            return;

        ShapeVertex v;
        v.radii = { m.corners.top_left, m.corners.top_right, m.corners.bottom_right, m.corners.bottom_left };
        v.edge = { m.softness, m.outline_width, m.light_width, m.ring };
        v.top = packed(shape.top);
        v.bottom = packed(shape.bottom);
        v.outline = packed(shape.outline);
        v.light = packed(shape.light);
        v.cut = { m.cut_x, m.cut_y, m.cut_radius, 0 };
        v.fill = { m.fill_from, m.fill_to };
        const auto base = static_cast<std::uint32_t>(shape_vertices_.size());
        for (const auto& [x, y] : { std::pair { visible.x0, visible.y0 }, { visible.x1, visible.y0 }, { visible.x1, visible.y1 }, { visible.x0, visible.y1 } })
        {
            v.x = x;
            v.y = y;
            v.local = { x - cx, y - cy, m.half_width, m.half_height };
            shape_vertices_.push_back(v);
        }
        add_indices(BatchKind::Shapes, 0, base, kQuadCorners);
    }

    void DrawList::polygon(std::span<const std::array<float, 2>> corners, Color color)
    {
        using Corner = std::array<float, 2>;
        std::vector<Corner> kept;
        kept.reserve(corners.size());
        for (const Corner& c : corners)
            kept.push_back({ c[0] * scale_, c[1] * scale_ });
        if (!clips_.empty())
        {
            // Cut by each side of the clip in turn, keeping the part inside it.
            const Rect& clip = clips_.back();
            const auto cut = [&](int axis, float edge, bool keep_below) {
                std::vector<Corner> out;
                const auto inside = [&](const Corner& c) { return keep_below ? c.at(axis) <= edge : c.at(axis) >= edge; };
                for (std::size_t i = 0; i < kept.size(); ++i)
                {
                    const Corner& a = kept[i];
                    const Corner& b = kept[(i + 1) % kept.size()];
                    if (inside(a))
                        out.push_back(a);
                    if (inside(a) != inside(b))
                    {
                        const float t = (edge - a.at(axis)) / (b.at(axis) - a.at(axis));
                        out.push_back({ lerp(a[0], b[0], t), lerp(a[1], b[1], t) });
                    }
                }
                kept = std::move(out);
            };
            cut(0, clip.x0, false);
            cut(0, clip.x1, true);
            cut(1, clip.y0, false);
            cut(1, clip.y1, true);
        }
        if (kept.size() < 3)
            return;
        const auto base = static_cast<std::uint32_t>(vertices_.size());
        for (const Corner& c : kept)
            vertices_.push_back({ c[0], c[1], packed(color), 0, 0 });
        // A fan from the first corner covers a convex polygon.
        std::vector<std::uint32_t> fan;
        for (std::uint32_t i = 1; i + 1 < kept.size(); ++i)
            fan.insert(fan.end(), { 0, i, i + 1 });
        add_indices(BatchKind::Quads, 0, base, fan);
    }

    void DrawList::add_indices(BatchKind kind, TextureId texture, std::uint32_t base, std::span<const std::uint32_t> corners)
    {
        if (batches_.empty() || batches_.back().texture != texture || batches_.back().kind != kind)
            batches_.push_back({ texture, static_cast<std::uint32_t>(indices_.size()), 0, kind });
        for (const std::uint32_t i : corners)
            indices_.push_back(base + i);
        batches_.back().index_count += static_cast<std::uint32_t>(corners.size());
    }

    bool DrawList::push_clip(const Rect& rect)
    {
        const Rect r = snapped(rect, scale_);
        clips_.push_back(clips_.empty() ? r : intersect(r, clips_.back()));
        return !clips_.back().empty();
    }

    void DrawList::pop_clip()
    {
        assert(!clips_.empty() && "pop_clip without push_clip");
        if (!clips_.empty())
            clips_.pop_back();
    }

    const Rect* DrawList::clip() const
    {
        return clips_.empty() ? nullptr : &clips_.back();
    }

} // namespace rct3::ui
