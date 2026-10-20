// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/keyboard_focus.hpp"

#include "core/maybe.hpp"

#include <algorithm>
#include <vector>

namespace rct3::ui
{

    namespace
    {

        void collect(const Widget& w, std::vector<Widget*>& out)
        {
            for (const auto& c : w.children())
            {
                if (c->takes_focus() && usable(*c))
                    out.push_back(c.get());
                collect(*c, out);
            }
        }

    } // namespace

    bool usable(const Widget& w)
    {
        for (const Widget* p = &w; p; p = p->parent())
        {
            if (!p->visible || !p->enabled)
                return false;
        }
        return true;
    }

    std::optional<Widget&> next_focus(const Widget& scope, std::optional<const Widget&> current, bool backwards)
    {
        std::vector<Widget*> order;
        collect(scope, order);
        if (order.empty())
            return std::nullopt;
        const auto it = std::ranges::find(order, as_pointer(current));
        if (it == order.end())
            return backwards ? *order.back() : *order.front();
        const auto at = static_cast<std::size_t>(it - order.begin());
        const std::size_t n = order.size();
        return *order[backwards ? (at + n - 1) % n : (at + 1) % n];
    }

    void draw_focus_mark(DrawList& list, const Widget& w, const FocusMarkStyle& style)
    {
        std::vector<const Widget*> clipping;
        for (const Widget* p = w.parent(); p; p = p->parent())
        {
            if (p->clip_children)
                clipping.push_back(p);
        }
        // Outermost first, so each clip narrows the one before.
        bool visible = true;
        for (auto it = clipping.rbegin(); it != clipping.rend(); ++it)
            visible = list.push_clip((*it)->rect()) && visible;
        if (visible)
        {
            const Rect& r = w.rect();
            const float o = style.outset;
            const float t = style.thickness;
            const Rect outer { r.x0 - o, r.y0 - o, r.x1 + o, r.y1 + o };
            list.fill({ outer.x0, outer.y0, outer.x1, outer.y0 + t }, style.colour);
            list.fill({ outer.x0, outer.y1 - t, outer.x1, outer.y1 }, style.colour);
            list.fill({ outer.x0, outer.y0 + t, outer.x0 + t, outer.y1 - t }, style.colour);
            list.fill({ outer.x1 - t, outer.y0 + t, outer.x1, outer.y1 - t }, style.colour);
        }
        for (std::size_t i = 0; i < clipping.size(); ++i)
            list.pop_clip();
    }

} // namespace rct3::ui
