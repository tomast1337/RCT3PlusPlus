// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

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

    } // namespace

    Rect intersect(const Rect& a, const Rect& b)
    {
        return { std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1) };
    }

    void DrawList::clear()
    {
        vertices_.clear();
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

        if (batches_.empty() || batches_.back().texture != texture)
            batches_.push_back({ texture, static_cast<std::uint32_t>(indices_.size()), 0 });
        for (const std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
            indices_.push_back(base + i);
        batches_.back().index_count += 6;
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
        if (batches_.empty() || batches_.back().texture != texture)
            batches_.push_back({ texture, static_cast<std::uint32_t>(indices_.size()), 0 });
        for (std::uint32_t i = 0; i < kSides; ++i)
        {
            indices_.push_back(base);
            indices_.push_back(base + 1 + i);
            indices_.push_back(base + 1 + (i + 1) % kSides);
        }
        batches_.back().index_count += 3 * kSides;
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
