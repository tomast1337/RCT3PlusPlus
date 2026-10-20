// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game interface is a tree of widgets that lives as long as the screens it makes up. Each widget has a
// name, a placement in its parent, flags and children, and draws itself into a DrawList every frame in tree
// order, so later children cover earlier ones. Windows are widget classes that build their children in
// their constructor and react to them in update(): a button records a click, the window asks for it. There
// are no listeners or callbacks.
//
// Rectangles are in logical units (see DrawList) and absolute: layout turns each placement into a rectangle
// on the desktop before input and drawing use them.
#pragma once

#include "core/maybe.hpp"
#include "ui/cursor.hpp"
#include "ui/draw_list.hpp"
#include "ui/font_set.hpp"
#include "ui/keyboard.hpp"
#include "ui/placement.hpp"
#include "ui/skin_library.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct Point
    {
        float x = 0;
        float y = 0;
    };

    [[nodiscard]] inline bool contains(const Rect& rect, Point p)
    {
        return p.x >= rect.x0 && p.x < rect.x1 && p.y >= rect.y0 && p.y < rect.y1;
    }

    /// What widgets update with.
    struct UpdateContext
    {
        float elapsed = 0;              ///< Seconds since the last update
        const FontSet* fonts = nullptr; ///< For widgets that measure text; null when there are none
    };

    /// What widgets draw with.
    struct DrawContext
    {
        DrawList& list;
        const SkinLibrary& skins;
        const FontSet& fonts;
    };

    class Widget
    {
    public:
        explicit Widget(std::string name = { });
        virtual ~Widget() = default;
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget(Widget&&) = delete;
        Widget& operator=(Widget&&) = delete;

        Placement placement = fill();
        bool visible = true;
        bool enabled = true;
        bool clip_children = false; ///< Children are cut to this widget's rectangle
        bool keep_inside = false;   ///< Layout moves the widget back inside its parent, e.g. a dragged window
        /// For a top-level widget such as a menu: it closes when the pointer presses outside it, and takes
        /// the keyboard focus while it is the top-most (see Desktop).
        bool popup = false;
        /// For a top-level widget that must stay behind others, as a drawer behind a bar: pressing it does not
        /// raise it above them.
        bool stays_behind = false;
        /// For a top-level widget such as a question: while it is shown, the pointer and the keys reach only
        /// it and what is above it, and what is below is shaded (see Desktop).
        bool modal = false;
        /// Help shown beside the pointer when it rests here; children without their own show this one.
        std::u16string tooltip;

        [[nodiscard]] std::string_view name() const
        {
            return name_;
        }
        /// On the desktop, as of the last layout.
        [[nodiscard]] const Rect& rect() const
        {
            return rect_;
        }
        [[nodiscard]] Widget* parent() const
        {
            return parent_;
        }
        [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const
        {
            return children_;
        }

        /// Appends a child, drawn over the children before it, and returns it.
        template<class T, class... Args> T& add(Args&&... args)
        {
            auto child = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *child;
            adopt(std::move(child));
            return ref;
        }
        /// Detaches `child`; nothing when it is not a direct child.
        std::unique_ptr<Widget> remove(const Widget& child);
        /// Moves `child` to the end, above its siblings.
        void raise(const Widget& child);
        /// Moves `child` to the start, under its siblings.
        void lower(const Widget& child);

        /// Follows a path of names separated by '/' down from this widget, e.g. "options/ok".
        [[nodiscard]] std::optional<Widget&> find(std::string_view path) const;
        /// `find`, as the kind of widget the caller knows lives there.
        template<typename T> [[nodiscard]] std::optional<T&> find_as(std::string_view path) const
        {
            if (auto w = find(path))
                return static_cast<T&>(*w);
            return std::nullopt;
        }

        /// Asks the desktop to detach this widget after the current update.
        void close()
        {
            closing_ = true;
        }
        [[nodiscard]] bool closing() const
        {
            return closing_;
        }

        // Interaction state the desktop sets each update.
        [[nodiscard]] bool hovered() const
        {
            return hovered_;
        }
        /// Held by the pointer and the pointer is still over it.
        [[nodiscard]] bool pressed() const
        {
            return pressed_;
        }
        /// Inside the top-most window, the one drawn highlighted.
        [[nodiscard]] bool focused() const
        {
            return focused_;
        }
        /// Gets keys before any other widget (see Desktop::keyboard_focus).
        [[nodiscard]] bool has_keyboard_focus() const
        {
            return keyboard_focus_;
        }

        /// Whether the pointer stops at this widget. Widgets that return false let it through to whatever is
        /// below, ending at the 3D view when nothing takes it.
        [[nodiscard]] virtual bool takes_pointer() const
        {
            return false;
        }
        /// Whether a point in the rectangle stops the pointer, for a widget that takes it: false where the
        /// widget has a gap that shows what is behind it. All of the rectangle by default.
        [[nodiscard]] virtual bool covers(Point) const
        {
            return true;
        }
        /// Whether pressing and releasing this widget clicks (interface_sound.hpp), as buttons do.
        [[nodiscard]] virtual bool clicks() const
        {
            return false;
        }
        /// Whether the pointer coming over this widget, enabled, clicks as a press does.
        [[nodiscard]] virtual bool clicks_on_hover() const
        {
            return false;
        }
        /// The pointer's look while it rests over this widget at `p`, for a widget that takes it: a hand where
        /// pressing clicks, the arrow elsewhere. The desktop shows "not allowed" instead for a disabled widget
        /// that asks for anything but the arrow.
        [[nodiscard]] virtual CursorKind cursor(Point) const
        {
            return clicks() ? CursorKind::Hand : CursorKind::Arrow;
        }
        /// The pointer's look while this widget holds it, wherever the pointer is: as over it by default.
        [[nodiscard]] virtual CursorKind held_cursor(Point p) const
        {
            return cursor(p);
        }
        virtual void on_press(Point)
        {
        }
        /// The pointer is over this widget and it is hovered(); each update, before update().
        virtual void on_hover(Point)
        {
        }
        /// The pointer moved while this widget holds it, wherever the pointer is.
        virtual void on_drag(Point)
        {
        }
        /// `inside`: the pointer was released over this widget.
        virtual void on_release(Point, bool /*inside*/)
        {
        }
        /// The wheel turned with the pointer over this widget or a child that did not take it. `notches.y`
        /// is positive away from the user, `notches.x` (a tilting wheel, a touchpad, or Shift held) positive to
        /// the left: both move the view towards the start. Returns whether the widget used it; if not, its
        /// parent is asked.
        virtual bool on_wheel(Point /*notches*/)
        {
            return false;
        }

        /// Whether pressing this widget, or a child that does not take it, gives it the keyboard focus.
        [[nodiscard]] virtual bool takes_focus() const
        {
            return false;
        }
        /// A key pressed while this widget or a child that did not use it has the keyboard focus. Returns
        /// whether the widget used it; if not, its parent is asked.
        virtual bool on_key(const KeyPress&)
        {
            return false;
        }
        /// What an input method is composing, every update while this widget has the keyboard focus.
        virtual void on_composition(const Composition&)
        {
        }
        /// Text for the clipboard, taken by the desktop right after this widget used a key: what Copy or Cut
        /// gave. Nothing by default.
        [[nodiscard]] virtual std::optional<std::u16string> take_copied()
        {
            return std::nullopt;
        }

        /// What the widget covers when laid out in its rectangle: the rectangle, unless its content runs
        /// further, as wrapped text can. Scroll areas size their content by it.
        [[nodiscard]] virtual Rect extent() const
        {
            return rect_;
        }

        /// Once per frame after input, parents before children. May add widgets anywhere in the tree;
        /// removing goes through close().
        virtual void update(const UpdateContext&)
        {
        }
        /// Draws this widget, not its children.
        virtual void draw(DrawContext&) const
        {
        }

    private:
        friend struct DesktopAccess; // Layout and interaction state are written by the desktop only

        void adopt(std::unique_ptr<Widget> child);

        std::string name_;
        Rect rect_;
        Widget* parent_ = nullptr;
        std::vector<std::unique_ptr<Widget>> children_;
        bool closing_ = false;
        bool hovered_ = false;
        bool pressed_ = false;
        bool focused_ = false;
        bool keyboard_focus_ = false;
    };

} // namespace rct3::ui
