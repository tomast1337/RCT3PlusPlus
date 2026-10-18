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

    // The window lets the pointer through where it has nothing: there the desktop is under it.
    window.placement = fill();
    ok.placement = top_left(10, 10, 20, 20);
    desktop.layout({ 0, 0, 100, 100 });
    CHECK(&widget_at(desktop.root(), { 15, 15 }) == &ok);
    CHECK(&widget_at(desktop.root(), { 60, 60 }) == &desktop.root());
    ok.visible = false;
    CHECK(&widget_at(desktop.root(), { 15, 15 }) == &desktop.root());
    ok.visible = true;

    CHECK(in_tree(desktop.root(), &ok));
    const Button loose("loose", "Btn");
    CHECK_FALSE(in_tree(desktop.root(), &loose));
}
