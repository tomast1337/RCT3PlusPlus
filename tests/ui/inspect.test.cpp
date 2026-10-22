// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/inspect.hpp"

#include "ui/desktop.hpp"
#include "ui/widgets/button.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    /// Takes the pointer only on its left part, as a panel spanning the screen does by its corner.
    struct LeftPart : Widget
    {
        using Widget::Widget;
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool covers(Point p) const override
        {
            return p.x < 20;
        }
    };
} // namespace

TEST_CASE("a widget is named by its class and found again by its path")
{
    Desktop desktop;
    Widget& window = desktop.root().add<Widget>("options");
    Button& ok = window.add<Button>("ok", "Btn");
    CHECK(type_name(ok) == "Button");
    CHECK(type_name(window) == "Widget");
    CHECK(widget_path(ok) == "options/ok");
    CHECK(rct3::as_pointer(desktop.root().find(widget_path(ok))) == &ok);
    CHECK(widget_path(desktop.root()).empty());

    // The window lets the pointer through where it has nothing, and the desktop is never picked.
    window.placement = fill();
    ok.placement = top_left(10, 10, 20, 20);
    desktop.layout({ 0, 0, 100, 100 });
    CHECK(rct3::as_pointer(widget_at(desktop.root(), { 15, 15 })) == &ok);
    CHECK(rct3::as_pointer(widget_at(desktop.root(), { 60, 60 })) == nullptr);
    ok.visible = false;
    CHECK(rct3::as_pointer(widget_at(desktop.root(), { 15, 15 })) == nullptr);
    ok.visible = true;

    // A button covers all of its rectangle; a frame letting the pointer through has its own.
    const Rect b = covered_bounds(ok);
    CHECK(b.x0 == 10);
    CHECK(b.x1 == 30);
    CHECK(covered_bounds(window).x1 == 100);
    LeftPart& panel = desktop.root().add<LeftPart>("panel");
    desktop.layout({ 0, 0, 100, 100 });
    CHECK(panel.rect().x1 == 100);
    CHECK(covered_bounds(panel).x1 == 20);
    CHECK(covered_bounds(panel).y1 == 100);
    panel.visible = false;

    CHECK(in_tree(desktop.root(), &ok));
    const Button loose("loose", "Btn");
    CHECK_FALSE(in_tree(desktop.root(), &loose));
}
