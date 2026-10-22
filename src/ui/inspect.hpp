// SPDX-License-Identifier: GPL-3.0-or-later
//
// Looking into a widget tree, for the developer panels and for checks: what a widget is and where it sits.
#pragma once

#include "core/maybe.hpp"
#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{
    /// Whether `target` is `root` or below it.
    [[nodiscard]] bool in_tree(const Widget& root, const Widget* target);

    /// The top-most visible widget at `p` that shows there: one taking the pointer over `p`, or one without
    /// children, such as a label. Frames that only hold others and let the pointer through are passed over,
    /// and so is `root`: none when nothing else is there.
    [[nodiscard]] std::optional<const Widget&> widget_at(const Widget& root, Point p);

    /// The part of the widget's rectangle that stops the pointer, to within `step` units: all of it, unless the
    /// widget has gaps (Widget::covers), as panels spanning the screen to sit by its corner do. Empty when it
    /// stops the pointer nowhere.
    [[nodiscard]] Rect covered_bounds(const Widget& w, float step = 4);

    /// The widget's class without its namespaces, e.g. "Button".
    [[nodiscard]] std::string type_name(const Widget& w);

    /// The names from below the top-most widget down to `w`, joined by '/', as Widget::find follows them.
    [[nodiscard]] std::string widget_path(const Widget& w);

} // namespace rct3::ui
