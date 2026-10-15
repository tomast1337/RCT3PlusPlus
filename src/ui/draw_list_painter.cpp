// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list_painter.hpp"

#include "ui/shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        struct Point2
        {
            float x;
            float y;
        };

        struct Colour4
        {
            float r = 0;
            float g = 0;
            float b = 0;
            float a = 0;
        };

        Colour4 unit(std::array<std::uint8_t, 4> c)
        {
            return { static_cast<float>(c[0]) / 255, static_cast<float>(c[1]) / 255, static_cast<float>(c[2]) / 255, static_cast<float>(c[3]) / 255 };
        }

        Color colour(std::array<std::uint8_t, 4> c)
        {
            return { c[0], c[1], c[2], c[3] };
        }

        float edge(Point2 p, Point2 q, Point2 r)
        {
            return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        }

        // Whether a pixel centre lying exactly on the edge from `u` to `v` belongs to this triangle. The two
        // triangles sharing an edge run it in opposite directions, so exactly one of them takes such a pixel.
        bool owns(Point2 u, Point2 v)
        {
            const float dy = v.y - u.y;
            return dy > 0 || (dy == 0 && v.x - u.x > 0);
        }

        // Calls `shade(weights, x, y)` for each pixel of `target` whose centre lies in the triangle, with
        // the centre's weights for the corners a, b and c.
        template<class Shade> void fill_triangle(ovl::RgbaImage& target, std::array<Point2, 3> corners, Shade&& shade)
        {
            std::array<int, 3> order { 0, 1, 2 };
            if (edge(corners[0], corners[1], corners[2]) < 0)
                std::swap(order[1], order[2]);
            const Point2 a = corners[order[0]];
            const Point2 b = corners[order[1]];
            const Point2 c = corners[order[2]];
            const float area = edge(a, b, c);
            if (area <= 0)
                return;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
            const int x1 = std::min(static_cast<int>(target.width), static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
            const int y1 = std::min(static_cast<int>(target.height), static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
            for (int y = y0; y < y1; ++y)
            {
                for (int x = x0; x < x1; ++x)
                {
                    const Point2 p { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f };
                    const float wa = edge(b, c, p);
                    const float wb = edge(c, a, p);
                    const float wc = edge(a, b, p);
                    if (wa < 0 || wb < 0 || wc < 0)
                        continue;
                    if ((wa == 0 && !owns(b, c)) || (wb == 0 && !owns(c, a)) || (wc == 0 && !owns(a, b)))
                        continue;
                    std::array<float, 3> w { };
                    w[static_cast<std::size_t>(order[0])] = wa / area;
                    w[static_cast<std::size_t>(order[1])] = wb / area;
                    w[static_cast<std::size_t>(order[2])] = wc / area;
                    shade(w, x, y);
                }
            }
        }

        void blend(ovl::RgbaImage& target, int x, int y, const Colour4& c)
        {
            std::uint8_t* px = target.pixels.data() + (static_cast<std::size_t>(y) * target.width + static_cast<std::size_t>(x)) * 4;
            const float a = std::clamp(c.a, 0.0f, 1.0f);
            const auto mix = [a](float source, std::uint8_t dest) { return static_cast<std::uint8_t>(std::lround(std::clamp(source * a + static_cast<float>(dest) / 255 * (1 - a), 0.0f, 1.0f) * 255)); };
            px[0] = mix(c.r, px[0]);
            px[1] = mix(c.g, px[1]);
            px[2] = mix(c.b, px[2]);
            px[3] = static_cast<std::uint8_t>(std::lround(std::clamp(a + static_cast<float>(px[3]) / 255 * (1 - a), 0.0f, 1.0f) * 255));
        }

        Colour4 texel(const ovl::RgbaImage& image, int x, int y)
        {
            x = std::clamp(x, 0, static_cast<int>(image.width) - 1);
            y = std::clamp(y, 0, static_cast<int>(image.height) - 1);
            const std::uint8_t* px = image.pixels.data() + (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
            return unit({ px[0], px[1], px[2], px[3] });
        }

        // Linear filtering between the four nearest texel centres, clamped at the edges.
        Colour4 sample(const ovl::RgbaImage* image, float u, float v)
        {
            if (image == nullptr || image->width == 0 || image->height == 0 || image->pixels.size() < std::size_t { image->width } * image->height * 4)
                return { 1, 1, 1, 1 };
            const float fx = u * static_cast<float>(image->width) - 0.5f;
            const float fy = v * static_cast<float>(image->height) - 0.5f;
            const auto ix = static_cast<int>(std::floor(fx));
            const auto iy = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(ix);
            const float ty = fy - static_cast<float>(iy);
            const auto lerp = [](const Colour4& p, const Colour4& q, float t) { return Colour4 { p.r + (q.r - p.r) * t, p.g + (q.g - p.g) * t, p.b + (q.b - p.b) * t, p.a + (q.a - p.a) * t }; };
            return lerp(lerp(texel(*image, ix, iy), texel(*image, ix + 1, iy), tx), lerp(texel(*image, ix, iy + 1), texel(*image, ix + 1, iy + 1), tx), ty);
        }

        void paint_quads(const DrawList& list, const Batch& batch, const PainterTextures& textures, ovl::RgbaImage& target)
        {
            const auto found = textures.find(batch.texture);
            const ovl::RgbaImage* image = batch.texture == 0 || found == textures.end() ? nullptr : &found->second;
            const auto vertices = list.vertices();
            const auto indices = list.indices().subspan(batch.first_index, batch.index_count);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::array<Vertex, 3> v { vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]] };
                fill_triangle(target, { { { v[0].x, v[0].y }, { v[1].x, v[1].y }, { v[2].x, v[2].y } } }, [&](const std::array<float, 3>& w, int x, int y) {
                    Colour4 tint;
                    float u = 0;
                    float t = 0;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        const Colour4 c = unit(v[k].color);
                        tint = { tint.r + c.r * w[k], tint.g + c.g * w[k], tint.b + c.b * w[k], tint.a + c.a * w[k] };
                        u += v[k].u * w[k];
                        t += v[k].v * w[k];
                    }
                    const Colour4 s = sample(image, u, t);
                    blend(target, x, y, { s.r * tint.r, s.g * tint.g, s.b * tint.b, s.a * tint.a });
                });
            }
        }

        void paint_shapes(const DrawList& list, const Batch& batch, ovl::RgbaImage& target)
        {
            const auto vertices = list.shape_vertices();
            const auto indices = list.indices().subspan(batch.first_index, batch.index_count);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::array<ShapeVertex, 3> v { vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]] };
                // The measures are the same on every corner of a shape.
                const ShapeVertex& s = v[0];
                Shape shape;
                shape.top = colour(s.top);
                shape.bottom = colour(s.bottom);
                shape.outline = colour(s.outline);
                shape.light = colour(s.light);
                const ShapeMetrics m { s.local[2], s.local[3], { s.radii[0], s.radii[1], s.radii[2], s.radii[3] }, s.edge[0], s.edge[1], s.edge[2], s.edge[3] };
                fill_triangle(target, { { { v[0].x, v[0].y }, { v[1].x, v[1].y }, { v[2].x, v[2].y } } }, [&](const std::array<float, 3>& w, int x, int y) {
                    const float lx = v[0].local[0] * w[0] + v[1].local[0] * w[1] + v[2].local[0] * w[2];
                    const float ly = v[0].local[1] * w[0] + v[1].local[1] * w[1] + v[2].local[1] * w[2];
                    const Color c = shade_shape(shape, m, lx, ly);
                    if (c.a > 0)
                        blend(target, x, y, unit({ c.r, c.g, c.b, c.a }));
                });
            }
        }

    } // namespace

    ovl::RgbaImage blank_picture(std::uint32_t width, std::uint32_t height, Color background)
    {
        ovl::RgbaImage image { width, height, { } };
        image.pixels.reserve(std::size_t { width } * height * 4);
        for (std::size_t i = 0; i < std::size_t { width } * height; ++i)
            image.pixels.insert(image.pixels.end(), { background.r, background.g, background.b, background.a });
        return image;
    }

    void paint_draw_list(const DrawList& list, const PainterTextures& textures, ovl::RgbaImage& target)
    {
        if (target.pixels.size() < std::size_t { target.width } * target.height * 4)
            return;
        for (const Batch& batch : list.batches())
        {
            if (batch.kind == BatchKind::Shapes)
                paint_shapes(list, batch, target);
            else
                paint_quads(list, batch, textures, target);
        }
    }

} // namespace rct3::ui
