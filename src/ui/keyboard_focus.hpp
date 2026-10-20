// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving the keyboard focus with the keyboard and showing where it is. Tab moves it to the next widget that
// takes it, in tree order (the order windows add their widgets), and Shift+Tab to the previous one; both
// stay inside one window and wrap around. The desktop draws a mark around the focused widget while the
// focus was last moved by the keyboard, and hides it again when the pointer presses.
#pragma once

#include "core/maybe.hpp"
#include "ui/widget.hpp"

namespace rct3::ui
{

    struct FocusMarkStyle
    {
        Color colour = hex("#000000a0");
        float thickness = 1;
        float outset = 2; ///< From the widget's edges outwards
    };

    /// Whether `w` and everything above it are shown and enabled.
    [[nodiscard]] bool usable(const Widget& w);

    /// The widget after `current` (or before it, `backwards`) among those below `scope` that take the
    /// focus and are usable, wrapping around. From the first or last one when `current` is not among them.
    /// Nothing when none takes it.
    [[nodiscard]] std::optional<Widget&> next_focus(const Widget& scope, std::optional<const Widget&> current, bool backwards);

    /// Draws the mark around `w`, cut by every ancestor that clips its children.
    void draw_focus_mark(DrawList& list, const Widget& w, const FocusMarkStyle& style);

} // namespace rct3::ui
