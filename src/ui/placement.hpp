// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a widget sits inside its parent, so layouts follow the window as it is resized. Each edge is tied to
// a fraction of the parent (0 = its left or top edge, 1 = its right or bottom edge) and then moved by a fixed
// offset in logical units. Tying both edges of an axis to the same fraction keeps the size fixed; tying them
// to different fractions stretches the widget with the parent.
#pragma once

#include "ui/draw_list.hpp"

namespace rct3::ui
{

    struct Placement
    {
        Rect anchors; ///< Fractions of the parent for each edge
        Rect offsets; ///< Logical units added to each anchored edge
    };

    /// The rectangle `placement` gives inside `parent`.
    [[nodiscard]] Rect place(const Placement& placement, const Rect& parent);

    /// Fills the parent, `margin` in from every edge.
    [[nodiscard]] Placement fill(float margin = 0);
    /// A fixed size whose top-left corner is at (x, y) from the parent's top-left.
    [[nodiscard]] Placement top_left(float x, float y, float width, float height);
    /// A fixed size whose top-right corner is `right` in from the parent's right edge and `y` down.
    [[nodiscard]] Placement top_right(float right, float y, float width, float height);
    /// A fixed size whose bottom-left corner is `x` in and `bottom` up from the parent's bottom-left.
    [[nodiscard]] Placement bottom_left(float x, float bottom, float width, float height);
    /// A fixed size whose bottom-right corner is `right` and `bottom` in from the parent's bottom-right.
    [[nodiscard]] Placement bottom_right(float right, float bottom, float width, float height);
    /// A fixed size centred in the parent, moved by (dx, dy).
    [[nodiscard]] Placement centred(float width, float height, float dx = 0, float dy = 0);
    /// Stretches across the parent's width, `margin` in from the sides, `height` tall, `y` down from the top.
    [[nodiscard]] Placement top_band(float y, float height, float margin = 0);

    /// Moves `rect` the least distance that puts it inside `bounds`. A rectangle larger than `bounds` is
    /// lined up with its top-left corner, so a window's title bar stays reachable.
    [[nodiscard]] Rect moved_inside(const Rect& rect, const Rect& bounds);

} // namespace rct3::ui
