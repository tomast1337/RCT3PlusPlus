// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shapes the interface draws in code instead of from a picture: rounded rectangles, discs and rings with a
// gradient fill, an outline, a light along the edge and soft edges for shadows, optionally with a round bite
// taken out of them. Every pixel works out how far
// it lies from the shape's edge (a signed distance, negative inside) and takes its colour from that, so edges
// stay sharp and smooth at any size.
//
// The renderer's shape shader and the CPU painter (draw_list_painter.hpp) compute the same thing; the
// functions here are the reference both follow. Lengths are in pixels from the shape's middle, y down.
#pragma once

#include "ui/draw_list.hpp"

namespace rct3::ui
{

    /// Radii of a shape's corners, clockwise from the top-left. Half the shorter side or more rounds the whole
    /// end: a square with all four at half its side is a disc.
    struct Corners
    {
        float top_left = 0;
        float top_right = 0;
        float bottom_right = 0;
        float bottom_left = 0;
    };

    [[nodiscard]] constexpr Corners round_corners(float radius)
    {
        return { radius, radius, radius, radius };
    }

    struct Shape
    {
        Rect rect; ///< The box the outline fits in
        Corners corners;
        float ring = 0; ///< When above 0, only a band this wide inside the outline is filled
        Color top = kWhite;
        Color bottom = kWhite; ///< The fill runs from `top` at the top of `rect` to this at the bottom
        float fill_from = 0;   ///< Share of the height where the fill starts to change from `top`
        float fill_to = 1;     ///< Share of the height where it has become `bottom`
        Color outline { 0, 0, 0, 0 };
        float outline_width = 0; ///< Along the inside of the edge
        Color light { 0, 0, 0, 0 };
        float light_width = 0; ///< From the edges that face up inwards, fading out: a lit bevel
        float softness = 0;    ///< Across the edge; 0 for a crisp edge, more for a blurred one such as a shadow
        float cut_x = 0;       ///< Centre of a disc taken out of the shape, in the units of `rect`
        float cut_y = 0;
        float cut_radius = 0; ///< 0 for no cut
    };

    /// A shape's measures in pixels, as the shader receives them.
    struct ShapeMetrics
    {
        float half_width = 0;
        float half_height = 0;
        Corners corners; ///< Each already no more than the shorter half side
        float softness = 0;
        float outline_width = 0;
        float light_width = 0;
        float ring = 0;
        float fill_from = 0;
        float fill_to = 1;
        float cut_x = 0; ///< From the middle
        float cut_y = 0;
        float cut_radius = 0;
    };

    /// `shape` measured in pixels at `scale` pixels per unit.
    [[nodiscard]] ShapeMetrics shape_metrics(const Shape& shape, float scale);

    /// How far the point (x, y), from the middle, lies outside the shape's edge; negative inside.
    [[nodiscard]] float shape_distance(const ShapeMetrics& m, float x, float y);

    /// How much of a pixel at `distance` from the edge the shape covers, 0 to 1.
    [[nodiscard]] float shape_coverage(const ShapeMetrics& m, float distance);

    /// The colour of the shape at (x, y) from its middle, its alpha including the coverage.
    [[nodiscard]] Color shade_shape(const Shape& shape, const ShapeMetrics& m, float x, float y);

    /// How far past `rect` a shape can colour pixels: its soft edge, plus one pixel for smoothing, in pixels.
    [[nodiscard]] float shape_margin(const ShapeMetrics& m);

} // namespace rct3::ui
