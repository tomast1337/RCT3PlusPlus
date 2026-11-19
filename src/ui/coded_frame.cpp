// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/coded_frame.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::ui
{

    namespace
    {

        /// How many times the box's diagonal the disc that cuts along a line is: its edge then bows by under a
        /// thousandth of the diagonal, while staying small enough to measure in single precision.
        constexpr f32 kStraightCut = 256;

        struct Scale
        {
            f32 x = 1;
            f32 y = 1;
            f32 length = 1; ///< For radii and widths: the smaller of the two, so round things stay round
        };

        Scale frame_scale(const Rect& target, f32 width, f32 height)
        {
            if (width <= 0 || height <= 0)
                return { };
            const f32 x = target.width() / width;
            const f32 y = target.height() / height;
            return { x, y, std::min(x, y) };
        }

        f32 edge(const FrameEdge& e, f32 start, f32 size, f32 scale)
        {
            return start + e.share * size + e.offset * scale;
        }

        Rect place(const FrameBox& box, const Rect& target, const Scale& s)
        {
            return {
                edge(box.x0, target.x0, target.width(), s.x),
                edge(box.y0, target.y0, target.height(), s.y),
                edge(box.x1, target.x0, target.width(), s.x),
                edge(box.y1, target.y0, target.height(), s.y),
            };
        }

        Color times(Color c, Color tint)
        {
            const auto channel = [](u8 a, u8 b) { return static_cast<u8>((a * b + 127) / 255); };
            return { channel(c.r, tint.r), channel(c.g, tint.g), channel(c.b, tint.b), channel(c.a, tint.a) };
        }

    } // namespace

    Rect frame_rect(const FrameBox& box, const Rect& target, f32 width, f32 height)
    {
        return place(box, target, frame_scale(target, width, height));
    }

    void draw_frame(DrawList& list, const CodedFrame& frame, const Rect& target, Color tint)
    {
        const Scale s = frame_scale(target, frame.width, frame.height);
        for (const FrameLayer& layer : frame.layers)
        {
            Shape shape = layer.shape;
            shape.rect = place(layer.box, target, s);
            if (shape.rect.empty())
                continue;
            const f32 r = s.length;
            shape.corners = { shape.corners.top_left * r, shape.corners.top_right * r, shape.corners.bottom_right * r, shape.corners.bottom_left * r };
            shape.ring *= r;
            shape.outline_width *= r;
            shape.light_width *= r;
            shape.softness *= r;
            shape.cut_x = shape.rect.x0 + shape.cut_x * s.x;
            shape.cut_y = shape.rect.y0 + shape.cut_y * s.y;
            shape.cut_radius *= r;
            if (layer.cut_line)
            {
                // A disc so large that its edge is straight along the line, lying over its left-hand side.
                const auto at = [&](std::array<f32, 2> p) { return std::array { shape.rect.x0 + p[0] * shape.rect.width(), shape.rect.y0 + p[1] * shape.rect.height() }; };
                const auto [ax, ay] = at(layer.cut_line->from);
                const auto [bx, by] = at(layer.cut_line->to);
                const f32 dx = bx - ax;
                const f32 dy = by - ay;
                const f32 length = std::hypot(dx, dy);
                if (length <= 0)
                    continue;
                const f32 radius = kStraightCut * std::hypot(shape.rect.width(), shape.rect.height());
                shape.cut_x = (ax + bx) / 2 + dy / length * radius;
                shape.cut_y = (ay + by) / 2 - dx / length * radius;
                shape.cut_radius = radius;
            }
            if (tint != kWhite)
            {
                shape.top = times(shape.top, tint);
                shape.bottom = times(shape.bottom, tint);
                shape.outline = times(shape.outline, tint);
                shape.light = times(shape.light, tint);
            }
            if (!layer.clip)
            {
                list.shape(shape);
                continue;
            }
            if (list.push_clip(place(*layer.clip, target, s)))
                list.shape(shape);
            list.pop_clip();
        }
    }

} // namespace rct3::ui
