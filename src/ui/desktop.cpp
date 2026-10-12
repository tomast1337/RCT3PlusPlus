// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/desktop.hpp"

#include <cstddef>
#include <vector>

namespace rct3::ui
{

    struct DesktopAccess
    {
        static void set_rect(Widget& w, const Rect& r)
        {
            w.rect_ = r;
        }
        static void set_flags(Widget& w, bool hovered, bool pressed, bool focused)
        {
            w.hovered_ = hovered;
            w.pressed_ = pressed;
            w.focused_ = focused;
        }
    };

    namespace
    {

        void place_tree(Widget& w, const Rect& parent)
        {
            Rect r = place(w.placement, parent);
            if (w.keep_inside)
            {
                // Move the placement too, so the widget stays where it was put back.
                const Rect inside = moved_inside(r, parent);
                const float dx = inside.x0 - r.x0;
                const float dy = inside.y0 - r.y0;
                Rect& o = w.placement.offsets;
                o = { o.x0 + dx, o.y0 + dy, o.x1 + dx, o.y1 + dy };
                r = inside;
            }
            DesktopAccess::set_rect(w, r);
            for (const auto& c : w.children())
                place_tree(*c, r);
        }

        Widget* hit_tree(const Widget& w, Point p)
        {
            if (!w.visible)
                return nullptr;
            const bool inside = contains(w.rect(), p);
            if (!inside && w.clip_children)
                return nullptr;
            for (auto it = w.children().rbegin(); it != w.children().rend(); ++it)
            {
                if (Widget* found = hit_tree(**it, p))
                    return found;
            }
            return inside && w.takes_pointer() ? const_cast<Widget*>(&w) : nullptr;
        }

        Widget* deepest_tree(const Widget& w, Point p)
        {
            if (!w.visible || !contains(w.rect(), p))
                return nullptr;
            for (auto it = w.children().rbegin(); it != w.children().rend(); ++it)
            {
                if (Widget* found = deepest_tree(**it, p))
                    return found;
            }
            return const_cast<Widget*>(&w);
        }

        bool in_tree(const Widget& root, const Widget* target)
        {
            if (&root == target)
                return true;
            for (const auto& c : root.children())
            {
                if (in_tree(*c, target))
                    return true;
            }
            return false;
        }

        // The direct child of `root` that holds `w`.
        Widget* top_level(const Widget& root, Widget* w)
        {
            while (w != nullptr && w->parent() != &root)
                w = w->parent();
            return w;
        }

        void set_flags_tree(Widget& w, const Widget* under, const Widget* held, bool focused)
        {
            const bool hovered = &w == under && (held == nullptr || held == &w);
            DesktopAccess::set_flags(w, hovered, &w == held && held == under, focused);
            for (const auto& c : w.children())
                set_flags_tree(*c, under, held, focused);
        }

        void update_tree(Widget& w, const UpdateContext& context)
        {
            w.update(context);
            // By index: an update may add widgets here, e.g. a window opening another beside it.
            for (std::size_t i = 0; i < w.children().size(); ++i)
                update_tree(*w.children()[i], context);
        }

        // Detaches every closing widget below `w`.
        void sweep(Widget& w)
        {
            std::vector<const Widget*> closing;
            for (const auto& c : w.children())
            {
                if (c->closing())
                    closing.push_back(c.get());
                else
                    sweep(*c);
            }
            for (const Widget* c : closing)
                w.remove(*c);
        }

        void draw_tree(const Widget& w, DrawContext& context)
        {
            if (!w.visible)
                return;
            w.draw(context);
            if (w.children().empty())
                return;
            const bool visible = !w.clip_children || context.list.push_clip(w.rect());
            if (visible)
            {
                for (const auto& c : w.children())
                    draw_tree(*c, context);
            }
            if (w.clip_children)
                context.list.pop_clip();
        }

    } // namespace

    void Desktop::layout(const Rect& view)
    {
        view_ = view;
        root_->placement = { { 0, 0, 0, 0 }, view };
        place_tree(*root_, view);
    }

    Widget* Desktop::hit(Point p) const
    {
        return hit_tree(*root_, p);
    }

    Widget* Desktop::deepest(Point p) const
    {
        Widget* w = deepest_tree(*root_, p);
        return w != nullptr ? w : root_.get();
    }

    bool Desktop::update(const PointerState& pointer, const UpdateContext& context)
    {
        if (held_ != nullptr && !in_tree(*root_, held_))
            held_ = nullptr;

        Widget* under = pointer.present ? hit(pointer.position) : nullptr;
        const bool down = pointer.present && pointer.down;
        if (down && !was_down_ && under != nullptr && under->enabled)
        {
            held_ = under;
            if (Widget* window = top_level(*root_, under))
                root_->raise(*window);
            held_->on_press(pointer.position);
        }
        else if (down && held_ != nullptr)
        {
            held_->on_drag(pointer.position);
        }
        else if (!down && held_ != nullptr)
        {
            Widget* released = held_;
            held_ = nullptr;
            released->on_release(pointer.position, under == released);
        }
        was_down_ = down;

        // The wheel goes to the deepest widget under the pointer that wants it, then up through its parents.
        if (pointer.present && (pointer.wheel.x != 0 || pointer.wheel.y != 0) && held_ == nullptr)
        {
            for (Widget* w = deepest(pointer.position); w != nullptr; w = w->parent())
            {
                if (w->enabled && w->on_wheel(pointer.wheel))
                    break;
            }
        }

        // Input may have moved or raised things: lay out and look again so the flags match what is drawn.
        layout(view_);
        under = pointer.present ? hit(pointer.position) : nullptr;
        const auto& tops = root_->children();
        for (std::size_t i = 0; i < tops.size(); ++i)
            set_flags_tree(*tops[i], under, held_, i + 1 == tops.size());

        update_tree(*root_, context);
        sweep(*root_);
        if (held_ != nullptr && !in_tree(*root_, held_))
            held_ = nullptr;
        layout(view_);
        under = pointer.present ? hit(pointer.position) : nullptr;
        tooltip_.update(under, pointer.position, down, context.elapsed, tooltip_style);
        return under != nullptr || held_ != nullptr;
    }

    void Desktop::draw(DrawContext& context) const
    {
        draw_tree(*root_, context);
        tooltip_.draw(context, tooltip_style, view_);
    }

} // namespace rct3::ui
