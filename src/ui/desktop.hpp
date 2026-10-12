// SPDX-License-Identifier: GPL-3.0-or-later
//
// The root of the widget tree: lays it out for the window, routes the pointer to it and draws it. Its direct
// children are the screen's top-level widgets, usually windows; pressing on one raises it above the others,
// and the top-most one has focus.
//
// The pointer is read as a state each frame (where it is, whether the button is down), not as a stream of
// events. A widget that is pressed holds the pointer until release, so dragging works when the pointer
// leaves it, and gets a click only when released over it.
//
// Keys go to the widget with the keyboard focus, then up through its parents until one uses them; without a
// focused widget they start at the top-most window. Pressing gives the focus to the nearest widget under the
// pointer that takes it, or takes it away when none does, the 3D view included. Hiding or disabling the
// focused widget moves the focus to its nearest parent still in use. Tab and Shift+Tab that no widget uses
// move it within the window and show it with a mark (see keyboard_focus.hpp).
//
// Popups (Widget::popup), such as menus, are top-level widgets that close when a press misses them. The
// top-most one takes the keyboard focus, which goes back where it was when the popup closes.
#pragma once

#include "ui/keyboard.hpp"
#include "ui/keyboard_focus.hpp"
#include "ui/tooltip.hpp"
#include "ui/widget.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rct3::ui
{

    struct PointerState
    {
        Point position;      ///< Logical units
        bool down = false;   ///< The primary button
        bool present = true; ///< False when something else, such as a developer tool, has the pointer
        Point wheel { };     ///< Wheel notches turned since the last update; see Widget::on_wheel
    };

    class Desktop
    {
    public:
        [[nodiscard]] Widget& root()
        {
            return *root_;
        }
        [[nodiscard]] const Widget& root() const
        {
            return *root_;
        }

        /// Places the whole tree in `view`, the window in logical units. Call when the view changes or the
        /// tree was edited from outside; update() lays out again after its own changes.
        void layout(const Rect& view);

        /// Routes the pointer, runs every widget's update, detaches widgets that asked to close and lays the
        /// tree out again. Returns whether the pointer is over the interface; when false it belongs to the 3D
        /// view. The context's time drives tooltips and anything else that waits.
        bool update(const PointerState& pointer, const UpdateContext& context = { });
        /// The same, with the keys pressed since the last update, routed after the pointer.
        bool update(const PointerState& pointer, const KeyboardState& keyboard, const UpdateContext& context = { });

        /// The widget keys go to first; nothing when keys start at the top-most window.
        [[nodiscard]] Widget* keyboard_focus() const
        {
            return focus_;
        }
        /// Gives `w` the keyboard focus, or takes it away with nullptr. `show` draws the mark around it.
        void set_keyboard_focus(Widget* w, bool show = false)
        {
            focus_ = w;
            focus_shown_ = show && w != nullptr;
        }
        /// Whether the mark around the focused widget is drawn.
        [[nodiscard]] bool keyboard_focus_shown() const
        {
            return focus_shown_ && focus_ != nullptr;
        }
        FocusMarkStyle focus_mark_style;

        /// Text a widget copied or cut since the last call, for the platform to put on the system clipboard.
        [[nodiscard]] std::optional<std::u16string> take_clipboard()
        {
            return std::exchange(clipboard_, std::nullopt);
        }

        /// Draws the tree, then the tooltip over everything.
        void draw(DrawContext& context) const;

        TooltipStyle tooltip_style;
        [[nodiscard]] const Tooltip& tooltip() const
        {
            return tooltip_;
        }

        /// The widget under `p` that takes the pointer, if any.
        [[nodiscard]] Widget* hit(Point p) const;
        /// The deepest visible widget under `p`, whether it takes the pointer or not; the root when nothing
        /// else is there.
        [[nodiscard]] Widget* deepest(Point p) const;

    private:
        void follow_popups();

        std::unique_ptr<Widget> root_ = std::make_unique<Widget>("desktop"); // On the heap so moving keeps parent links
        Rect view_;
        Widget* held_ = nullptr;
        Widget* focus_ = nullptr;
        bool focus_shown_ = false;
        Widget* before_popup_ = nullptr; // The focus before a popup took it; checked before use
        std::optional<std::u16string> clipboard_;
        bool was_down_ = false;
        Tooltip tooltip_;
    };

} // namespace rct3::ui
