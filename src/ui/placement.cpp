// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/placement.hpp"

#include <algorithm>

namespace rct3::ui
{

    Rect place(const Placement& placement, const Rect& parent)
    {
        const Rect& a = placement.anchors;
        const Rect& o = placement.offsets;
        return {
            parent.x0 + parent.width() * a.x0 + o.x0,
            parent.y0 + parent.height() * a.y0 + o.y0,
            parent.x0 + parent.width() * a.x1 + o.x1,
            parent.y0 + parent.height() * a.y1 + o.y1,
        };
    }

    Placement fill(float margin)
    {
        return { { 0, 0, 1, 1 }, { margin, margin, -margin, -margin } };
    }

    Placement top_left(float x, float y, float width, float height)
    {
        return { { 0, 0, 0, 0 }, { x, y, x + width, y + height } };
    }

    Placement top_right(float right, float y, float width, float height)
    {
        return { { 1, 0, 1, 0 }, { -right - width, y, -right, y + height } };
    }

    Placement bottom_left(float x, float bottom, float width, float height)
    {
        return { { 0, 1, 0, 1 }, { x, -bottom - height, x + width, -bottom } };
    }

    Placement bottom_right(float right, float bottom, float width, float height)
    {
        return { { 1, 1, 1, 1 }, { -right - width, -bottom - height, -right, -bottom } };
    }

    Placement centred(float width, float height, float dx, float dy)
    {
        return { { 0.5f, 0.5f, 0.5f, 0.5f }, { dx - width / 2, dy - height / 2, dx + width / 2, dy + height / 2 } };
    }

    Placement top_band(float y, float height, float margin)
    {
        return { { 0, 0, 1, 0 }, { margin, y, -margin, y + height } };
    }

    Rect moved_inside(const Rect& rect, const Rect& bounds)
    {
        // Right and bottom first, then left and top, so the top-left corner wins when it does not fit.
        float dx = std::min(0.0f, bounds.x1 - rect.x1);
        float dy = std::min(0.0f, bounds.y1 - rect.y1);
        dx = std::max(dx, bounds.x0 - rect.x0);
        dy = std::max(dy, bounds.y0 - rect.y0);
        return { rect.x0 + dx, rect.y0 + dy, rect.x1 + dx, rect.y1 + dy };
    }

} // namespace rct3::ui
