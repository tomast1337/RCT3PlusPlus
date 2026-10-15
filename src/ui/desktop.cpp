// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/desktop.hpp"

#include "ui/keyboard_focus.hpp"

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
        static void set_flags(Widget& w, bool hovered, bool pressed, bool focused, bool keyboard_focus)
        {
            w.hovered_ = hovered;
            w.pressed_ = pressed;
            w.focused_ = focused;
            w.keyboard_focus_ = keyboard_focus;
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
            return inside && w.takes_pointer() && w.covers(p) ? const_cast<Widget*>(&w) : nullptr;
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

        void set_flags_tree(Widget& w, const Widget* under, const Widget* held, bool focused, const Widget* focus)
        {
            const bool hovered = &w == under && (held == nullptr || held == &w);
            DesktopAccess::set_flags(w, hovered, &w == held && held == under, focused, &w == focus);
            for (const auto& c : w.children())
                set_flags_tree(*c, under, held, focused, focus);
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

    std::optional<std::size_t> Desktop::top_modal() const
    {
        const auto& tops = root_->children();
        for (std::size_t i = tops.size(); i-- > 0;)
        {
            if (tops[i]->modal && tops[i]->visible && !tops[i]->closing())
                return i;
        }
        return std::nullopt;
    }

    void Desktop::drop_shut_out_focus()
    {
        const std::size_t from = top_modal().value_or(0);
        if (focus_ == nullptr || from == 0)
            return;
        const Widget* top = top_level(*root_, focus_);
        const auto& tops = root_->children();
        for (std::size_t i = 0; i < from; ++i)
        {
            if (tops[i].get() == top)
            {
                focus_ = nullptr;
                focus_shown_ = false;
                return;
            }
        }
    }

    Widget* Desktop::hit(Point p) const
    {
        if (!root_->visible || (root_->clip_children && !contains(root_->rect(), p)))
            return nullptr;
        const auto& tops = root_->children();
        const std::size_t from = top_modal().value_or(0);
        for (std::size_t i = tops.size(); i-- > from;)
        {
            if (Widget* w = hit_tree(*tops[i], p))
                return w;
        }
        return nullptr;
    }

    Widget* Desktop::deepest(Point p) const
    {
        const auto& tops = root_->children();
        const std::size_t from = top_modal().value_or(0);
        for (std::size_t i = tops.size(); i-- > from;)
        {
            if (Widget* w = deepest_tree(*tops[i], p))
                return w;
        }
        return root_.get();
    }

    bool Desktop::update(const PointerState& pointer, const UpdateContext& context)
    {
        return update(pointer, KeyboardState { }, context);
    }

    bool Desktop::update(const PointerState& pointer, const KeyboardState& keyboard, const UpdateContext& context)
    {
        if (held_ != nullptr && !in_tree(*root_, held_))
            held_ = nullptr;
        if (focus_ != nullptr && !in_tree(*root_, focus_))
            focus_ = nullptr;
        drop_shut_out_focus();

        Widget* under = pointer.present ? hit(pointer.position) : nullptr;
        const bool down = pointer.present && pointer.down;
        // Any press, even on the 3D view, takes the focus from where it was, and closes the popups it
        // misses.
        if (down && !was_down_)
        {
            focus_ = nullptr;
            focus_shown_ = false;
            Widget* pressed_top = pointer.present ? top_level(*root_, deepest(pointer.position)) : nullptr;
            bool closed = false;
            for (const auto& top : root_->children())
            {
                if (top->popup && top.get() != pressed_top)
                {
                    closed = closed || (top->visible && !top->closing());
                    top->close();
                }
            }
            if (closed)
                sounds_.push_back(InterfaceSound::Release);
        }
        if (down && !was_down_ && under != nullptr && under->enabled)
        {
            held_ = under;
            if (Widget* window = top_level(*root_, under); window != nullptr && !window->stays_behind)
                root_->raise(*window);
            for (Widget* w = deepest(pointer.position); w != nullptr && focus_ == nullptr; w = w->parent())
            {
                if (w->takes_focus() && usable(*w))
                    focus_ = w;
            }
            if (held_->clicks())
                sounds_.push_back(InterfaceSound::Press);
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
            if (released->clicks() && under == released)
                sounds_.push_back(InterfaceSound::Release);
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

        // Keys after the pointer, so a press that moves the focus counts for keys in the same frame.
        for (const KeyPress& key : keyboard.presses)
        {
            // Checked for each key: one may hide the focused widget, e.g. by turning a page. Keys then
            // start at its nearest parent still in use, so the rest still reach the widget that hid it.
            if (focus_ != nullptr && !usable(*focus_))
            {
                while (focus_ != nullptr && focus_ != root_.get() && !usable(*focus_))
                    focus_ = focus_->parent();
                if (focus_ == root_.get())
                    focus_ = nullptr;
                focus_shown_ = false;
            }
            Widget* top = root_->children().empty() ? nullptr : root_->children().back().get();
            Widget* start = focus_ != nullptr ? focus_ : top;
            bool used = false;
            for (Widget* w = start; w != nullptr && !used; w = w->parent())
            {
                used = w->enabled && w->on_key(key);
                if (used)
                {
                    if (auto copied = w->take_copied())
                        clipboard_ = std::move(copied);
                }
            }
            // Tab nobody used moves the focus within its window.
            if (!used && key.key == Key::Tab && !key.control)
            {
                Widget* scope = focus_ != nullptr ? top_level(*root_, focus_) : top;
                if (scope != nullptr)
                {
                    if (Widget* next = next_focus(*scope, focus_, key.shift))
                    {
                        focus_ = next;
                        focus_shown_ = true;
                    }
                }
            }
        }

        if (focus_ != nullptr && usable(*focus_))
            focus_->on_composition(keyboard.composition);

        // Input may have moved or raised things: lay out and look again so the flags match what is drawn.
        layout(view_);
        under = pointer.present ? hit(pointer.position) : nullptr;
        const auto& tops = root_->children();
        for (std::size_t i = 0; i < tops.size(); ++i)
            set_flags_tree(*tops[i], under, held_, i + 1 == tops.size(), focus_);
        if (under != nullptr && (held_ == nullptr || held_ == under))
            under->on_hover(pointer.position);
        // Only compared, never followed: it may be gone.
        if (held_ == nullptr && under != last_under_ && under != nullptr && under->enabled && under->clicks_on_hover())
            sounds_.push_back(InterfaceSound::Press);
        if (held_ == nullptr)
            last_under_ = under;

        update_tree(*root_, context);
        sweep(*root_);
        if (held_ != nullptr && !in_tree(*root_, held_))
            held_ = nullptr;
        if (focus_ != nullptr && !in_tree(*root_, focus_))
            focus_ = nullptr;
        follow_popups();
        drop_shut_out_focus();
        layout(view_);
        under = pointer.present ? hit(pointer.position) : nullptr;
        tooltip_.update(under, pointer.position, down, context.elapsed, tooltip_style);
        return under != nullptr || held_ != nullptr || (pointer.present && top_modal());
    }

    void Desktop::follow_popups()
    {
        Widget* top = root_->children().empty() ? nullptr : root_->children().back().get();
        if (top != nullptr && top->popup && !top->closing())
        {
            // A popup on top takes the keys; where they went before gets them back when it goes.
            if (focus_ == nullptr || top_level(*root_, focus_) != top)
            {
                if (before_popup_ == nullptr)
                    before_popup_ = focus_;
                focus_ = top;
                focus_shown_ = false;
            }
            return;
        }
        if (before_popup_ != nullptr)
        {
            const bool in_popup = focus_ != nullptr && top_level(*root_, focus_) != nullptr && top_level(*root_, focus_)->popup;
            if ((focus_ == nullptr || in_popup) && in_tree(*root_, before_popup_) && usable(*before_popup_))
                focus_ = before_popup_;
            before_popup_ = nullptr;
        }
    }

    void Desktop::draw(DrawContext& context) const
    {
        const auto shaded = top_modal();
        if (!shaded)
        {
            draw_tree(*root_, context);
        }
        else if (root_->visible)
        {
            // The root draws nothing of its own and does not clip; the shade goes between its children.
            root_->draw(context);
            const auto& tops = root_->children();
            for (std::size_t i = 0; i < tops.size(); ++i)
            {
                if (i == *shaded)
                    context.list.fill(view_, modal_shade);
                draw_tree(*tops[i], context);
            }
        }
        if (focus_shown_ && focus_ != nullptr && usable(*focus_))
            draw_focus_mark(context.list, *focus_, focus_mark_style);
        tooltip_.draw(context, tooltip_style, view_);
    }

} // namespace rct3::ui
