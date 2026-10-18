// SPDX-License-Identifier: GPL-3.0-or-later
//
// Looking into a widget tree, for the developer panels and for checks: what a widget is and where it sits.
#pragma once

#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{
    /// Whether `target` is `root` or below it.
    [[nodiscard]] bool in_tree(const Widget& root, const Widget* target);

    /// The widget's class without its namespaces, e.g. "Button".
    [[nodiscard]] std::string type_name(const Widget& w);

    /// The names from below the top-most widget down to `w`, joined by '/', as Widget::find follows them.
    [[nodiscard]] std::string widget_path(const Widget& w);

} // namespace rct3::ui
