// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/coded_frame.hpp"

#include <algorithm>
#include <cstdint>

namespace rct3::ui
{

    namespace
    {

        struct Scale
        {
            float x = 1;
            float y = 1;
            float length = 1; ///< For radii and widths: the smaller of the two, so round things stay round
        };

        Scale frame_scale(const Rect& target, float width, float height)
        {
            if (width <= 0 || height <= 0)
                return { };
            const float x = target.width() / width;
            const float y = target.height() / height;
            return { x, y, std::min(x, y) };
        }

        float edge(const FrameEdge& e, float start, float size, float scale)
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
            const auto channel = [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>((a * b + 127) / 255); };
            return { channel(c.r, tint.r), channel(c.g, tint.g), channel(c.b, tint.b), channel(c.a, tint.a) };
        }

    } // namespace

    Rect frame_rect(const FrameBox& box, const Rect& target, float width, float height)
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
            const float r = s.length;
            shape.corners = { shape.corners.top_left * r, shape.corners.top_right * r, shape.corners.bottom_right * r, shape.corners.bottom_left * r };
            shape.ring *= r;
            shape.outline_width *= r;
            shape.light_width *= r;
            shape.softness *= r;
            shape.cut_x = shape.rect.x0 + shape.cut_x * s.x;
            shape.cut_y = shape.rect.y0 + shape.cut_y * s.y;
            shape.cut_radius *= r;
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
