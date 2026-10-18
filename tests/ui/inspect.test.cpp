// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/inspect.hpp"

#include "ui/desktop.hpp"
#include "ui/widgets/button.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("a widget is named by its class and found again by its path")
{
    Desktop desktop;
    Widget& window = desktop.root().add<Widget>("options");
    Button& ok = window.add<Button>("ok", "Btn");
    CHECK(type_name(ok) == "Button");
    CHECK(type_name(window) == "Widget");
    CHECK(widget_path(ok) == "options/ok");
    CHECK(desktop.root().find(widget_path(ok)) == &ok);
    CHECK(widget_path(desktop.root()).empty());

    CHECK(in_tree(desktop.root(), &ok));
    const Button loose("loose", "Btn");
    CHECK_FALSE(in_tree(desktop.root(), &loose));
}
