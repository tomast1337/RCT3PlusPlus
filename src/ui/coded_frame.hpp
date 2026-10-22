// SPDX-License-Identifier: GPL-3.0-or-later
//
// A skin drawn in code: the shapes (shape.hpp) that stand in for one of the game's pictures, laid out in the
// rectangle the picture would be stretched over. Each shape's box is pinned to that rectangle edge by edge,
// so a frame keeps its corners at their size while its middle grows, the way a skin's caps do.
//
// A frame that stands in for a picture stretched whole (a skin with a single cell) gives the size it was
// measured at; it then grows and shrinks with its rectangle, corners and all.
#pragma once

#include "ui/draw_list.hpp"
#include "ui/shape.hpp"

#include <array>
#include <optional>
#include <vector>

namespace rct3::ui
{

    /// One edge of a layer's box: a share of the frame's width (or height) from its left (or top), plus units.
    struct FrameEdge
    {
        float share = 0;
        float offset = 0;
    };

    struct FrameBox
    {
        FrameEdge x0;
        FrameEdge y0;
        FrameEdge x1 { 1, 0 };
        FrameEdge y1 { 1, 0 };
    };

    /// The box `left`, `top`, `right` and `bottom` units in from the frame's sides.
    [[nodiscard]] constexpr FrameBox inset_box(float left, float top, float right, float bottom)
    {
        return { { 0, left }, { 0, top }, { 1, -right }, { 1, -bottom } };
    }

    /// A box `width` by `height` units with its top-left corner `x`, `y` units from the frame's top-left.
    [[nodiscard]] constexpr FrameBox corner_box(float x, float y, float width, float height)
    {
        return { { 0, x }, { 0, y }, { 0, x + width }, { 0, y + height } };
    }

    /// A straight line across a layer's box, from `from` to `to`, each a share of the box's width and height
    /// from its top-left, so it stays on the same points however the box is stretched.
    struct CutLine
    {
        std::array<float, 2> from;
        std::array<float, 2> to;
    };

    /// The line from a box's top-left corner to its bottom-right: a layer cut along it keeps the half below.
    inline constexpr CutLine kBelowDiagonal { { 0, 0 }, { 1, 1 } };

    struct FrameLayer
    {
        FrameBox box;
        /// Its look; `rect` is replaced by the box, and the cut's centre is measured from the box's top-left.
        Shape shape;
        std::optional<FrameBox> clip; ///< When set, the layer only shows inside this
        /// When set, keeps only the part of the shape on the right hand of this line, walking it on screen
        /// (y down); this takes the place of the shape's own cut.
        std::optional<CutLine> cut_line { };
    };

    struct CodedFrame
    {
        std::vector<FrameLayer> layers; ///< In painter's order
        float width = 0;                ///< When above 0 with `height`, the size the frame was measured at
        float height = 0;
    };

    /// Where `box` falls in `target`, for a frame measured at `width` by `height` (0 for one that does not scale).
    [[nodiscard]] Rect frame_rect(const FrameBox& box, const Rect& target, float width, float height);

    /// Draws `frame` over `target`, its colours multiplied by `tint` as a picture's would be.
    void draw_frame(DrawList& list, const CodedFrame& frame, const Rect& target, Color tint = kWhite);

} // namespace rct3::ui
