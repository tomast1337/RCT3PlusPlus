// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/draw_list_painter.hpp"

#include "core/types.hpp"
#include "ui/shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        struct Point2
        {
            f32 x;
            f32 y;
        };

        struct Colour4
        {
            f32 r = 0;
            f32 g = 0;
            f32 b = 0;
            f32 a = 0;
        };

        Colour4 unit(std::array<u8, 4> c)
        {
            return { static_cast<f32>(c[0]) / 255, static_cast<f32>(c[1]) / 255, static_cast<f32>(c[2]) / 255, static_cast<f32>(c[3]) / 255 };
        }

        Color colour(std::array<u8, 4> c)
        {
            return { c[0], c[1], c[2], c[3] };
        }

        f32 edge(Point2 p, Point2 q, Point2 r)
        {
            return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        }

        // Whether a pixel centre lying exactly on the edge from `u` to `v` belongs to this triangle. The two
        // triangles sharing an edge run it in opposite directions, so exactly one of them takes such a pixel.
        bool owns(Point2 u, Point2 v)
        {
            const f32 dy = v.y - u.y;
            return dy > 0 || (dy == 0 && v.x - u.x > 0);
        }

        // Calls `shade(weights, x, y)` for each pixel of `target` whose centre lies in the triangle, with
        // the centre's weights for the corners a, b and c.
        template<class Shade> void fill_triangle(ovl::texture::RgbaImage& target, std::array<Point2, 3> corners, Shade&& shade)
        {
            std::array<i32, 3> order { 0, 1, 2 };
            if (edge(corners[0], corners[1], corners[2]) < 0)
                std::swap(order[1], order[2]);
            const Point2 a = corners[order[0]];
            const Point2 b = corners[order[1]];
            const Point2 c = corners[order[2]];
            const f32 area = edge(a, b, c);
            if (area <= 0)
                return;
            const i32 x0 = std::max(0, static_cast<i32>(std::floor(std::min({ a.x, b.x, c.x }))));
            const i32 y0 = std::max(0, static_cast<i32>(std::floor(std::min({ a.y, b.y, c.y }))));
            const i32 x1 = std::min(static_cast<i32>(target.width), static_cast<i32>(std::ceil(std::max({ a.x, b.x, c.x }))));
            const i32 y1 = std::min(static_cast<i32>(target.height), static_cast<i32>(std::ceil(std::max({ a.y, b.y, c.y }))));
            for (i32 y = y0; y < y1; ++y)
            {
                for (i32 x = x0; x < x1; ++x)
                {
                    const Point2 p { static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f };
                    const f32 wa = edge(b, c, p);
                    const f32 wb = edge(c, a, p);
                    const f32 wc = edge(a, b, p);
                    if (wa < 0 || wb < 0 || wc < 0)
                        continue;
                    if ((wa == 0 && !owns(b, c)) || (wb == 0 && !owns(c, a)) || (wc == 0 && !owns(a, b)))
                        continue;
                    std::array<f32, 3> w { };
                    w[static_cast<usize>(order[0])] = wa / area;
                    w[static_cast<usize>(order[1])] = wb / area;
                    w[static_cast<usize>(order[2])] = wc / area;
                    shade(w, x, y);
                }
            }
        }

        void blend(ovl::texture::RgbaImage& target, i32 x, i32 y, const Colour4& c)
        {
            u8* px = target.pixels.data() + (static_cast<usize>(y) * target.width + static_cast<usize>(x)) * 4;
            const f32 a = std::clamp(c.a, 0.0f, 1.0f);
            const auto mix = [a](f32 source, u8 dest) { return static_cast<u8>(std::lround(std::clamp(source * a + static_cast<f32>(dest) / 255 * (1 - a), 0.0f, 1.0f) * 255)); };
            px[0] = mix(c.r, px[0]);
            px[1] = mix(c.g, px[1]);
            px[2] = mix(c.b, px[2]);
            px[3] = static_cast<u8>(std::lround(std::clamp(a + static_cast<f32>(px[3]) / 255 * (1 - a), 0.0f, 1.0f) * 255));
        }

        Colour4 texel(const ovl::texture::RgbaImage& image, i32 x, i32 y)
        {
            x = std::clamp(x, 0, static_cast<i32>(image.width) - 1);
            y = std::clamp(y, 0, static_cast<i32>(image.height) - 1);
            const u8* px = image.pixels.data() + (static_cast<usize>(y) * image.width + static_cast<usize>(x)) * 4;
            return unit({ px[0], px[1], px[2], px[3] });
        }

        // Linear filtering between the four nearest texel centres, clamped at the edges.
        Colour4 sample(const ovl::texture::RgbaImage* image, f32 u, f32 v)
        {
            if (image == nullptr || image->width == 0 || image->height == 0 || image->pixels.size() < usize { image->width } * image->height * 4)
                return { 1, 1, 1, 1 };
            const f32 fx = u * static_cast<f32>(image->width) - 0.5f;
            const f32 fy = v * static_cast<f32>(image->height) - 0.5f;
            const auto ix = static_cast<i32>(std::floor(fx));
            const auto iy = static_cast<i32>(std::floor(fy));
            const f32 tx = fx - static_cast<f32>(ix);
            const f32 ty = fy - static_cast<f32>(iy);
            const auto lerp = [](const Colour4& p, const Colour4& q, f32 t) { return Colour4 { p.r + (q.r - p.r) * t, p.g + (q.g - p.g) * t, p.b + (q.b - p.b) * t, p.a + (q.a - p.a) * t }; };
            return lerp(lerp(texel(*image, ix, iy), texel(*image, ix + 1, iy), tx), lerp(texel(*image, ix, iy + 1), texel(*image, ix + 1, iy + 1), tx), ty);
        }

        void paint_quads(const DrawList& list, const Batch& batch, const PainterTextures& textures, ovl::texture::RgbaImage& target)
        {
            const auto found = textures.find(batch.texture);
            const ovl::texture::RgbaImage* image = batch.texture == 0 || found == textures.end() ? nullptr : &found->second;
            const auto vertices = list.vertices();
            const auto indices = list.indices().subspan(batch.first_index, batch.index_count);
            for (usize i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::array<Vertex, 3> v { vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]] };
                fill_triangle(target, { { { v[0].x, v[0].y }, { v[1].x, v[1].y }, { v[2].x, v[2].y } } }, [&](const std::array<f32, 3>& w, i32 x, i32 y) {
                    Colour4 tint;
                    f32 u = 0;
                    f32 t = 0;
                    for (usize k = 0; k < 3; ++k)
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

        void paint_shapes(const DrawList& list, const Batch& batch, ovl::texture::RgbaImage& target)
        {
            const auto vertices = list.shape_vertices();
            const auto indices = list.indices().subspan(batch.first_index, batch.index_count);
            for (usize i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::array<ShapeVertex, 3> v { vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]] };
                // The measures are the same on every corner of a shape.
                const ShapeVertex& s = v[0];
                Shape shape;
                shape.top = colour(s.top);
                shape.bottom = colour(s.bottom);
                shape.outline = colour(s.outline);
                shape.light = colour(s.light);
                const ShapeMetrics m { s.local[2], s.local[3], { s.radii[0], s.radii[1], s.radii[2], s.radii[3] }, s.edge[0], s.edge[1], s.edge[2], s.edge[3], s.fill[0], s.fill[1], s.cut[0], s.cut[1], s.cut[2] };
                fill_triangle(target, { { { v[0].x, v[0].y }, { v[1].x, v[1].y }, { v[2].x, v[2].y } } }, [&](const std::array<f32, 3>& w, i32 x, i32 y) {
                    const f32 lx = v[0].local[0] * w[0] + v[1].local[0] * w[1] + v[2].local[0] * w[2];
                    const f32 ly = v[0].local[1] * w[0] + v[1].local[1] * w[1] + v[2].local[1] * w[2];
                    const Color c = shade_shape(shape, m, lx, ly);
                    if (c.a > 0)
                        blend(target, x, y, unit({ c.r, c.g, c.b, c.a }));
                });
            }
        }

    } // namespace

    ovl::texture::RgbaImage blank_picture(u32 width, u32 height, Color background)
    {
        ovl::texture::RgbaImage image { width, height, { } };
        image.pixels.reserve(usize { width } * height * 4);
        for (usize i = 0; i < usize { width } * height; ++i)
            image.pixels.insert(image.pixels.end(), { background.r, background.g, background.b, background.a });
        return image;
    }

    void paint_draw_list(const DrawList& list, const PainterTextures& textures, ovl::texture::RgbaImage& target)
    {
        if (target.pixels.size() < usize { target.width } * target.height * 4)
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
