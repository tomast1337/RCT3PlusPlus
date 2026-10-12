// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/desktop.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::ui;

namespace
{
    // Takes the pointer and records what happened to it.
    class Probe : public Widget
    {
    public:
        using Widget::Widget;
        bool blocks = true;
        std::vector<std::string> events;
        int updates = 0;

        [[nodiscard]] bool takes_pointer() const override
        {
            return blocks;
        }
        void on_press(Point p) override
        {
            events.push_back("press " + std::to_string(static_cast<int>(p.x)));
        }
        void on_drag(Point p) override
        {
            events.push_back("drag " + std::to_string(static_cast<int>(p.x)));
        }
        void on_release(Point, bool inside) override
        {
            events.push_back(inside ? "release inside" : "release outside");
        }
        void update(const UpdateContext&) override
        {
            ++updates;
        }
    };

    constexpr Rect kView { 0, 0, 200, 100 };

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }
} // namespace

TEST_CASE("layout places the tree in the view")
{
    Desktop desktop;
    Widget& w = desktop.root().add<Widget>("w");
    w.placement = top_right(10, 10, 50, 20);
    Widget& inner = w.add<Widget>("inner");
    inner.placement = fill(5);
    desktop.layout(kView);
    CHECK(w.rect() == Rect { 140, 10, 190, 30 });
    CHECK(inner.rect() == Rect { 145, 15, 185, 25 });

    desktop.layout({ 0, 0, 400, 300 });
    CHECK(w.rect() == Rect { 340, 10, 390, 30 });
}

TEST_CASE("keep_inside brings a widget back when the view shrinks")
{
    Desktop desktop;
    Widget& w = desktop.root().add<Widget>("w");
    w.placement = top_left(150, 10, 40, 20);
    w.keep_inside = true;
    desktop.layout({ 0, 0, 100, 100 });
    CHECK(w.rect() == Rect { 60, 10, 100, 30 });
    // It stays where it was put back.
    desktop.layout(kView);
    CHECK(w.rect() == Rect { 60, 10, 100, 30 });
}

TEST_CASE("the pointer goes to the top-most widget that takes it")
{
    Desktop desktop;
    Probe& back = desktop.root().add<Probe>("back");
    back.placement = top_left(0, 0, 100, 100);
    Probe& front = desktop.root().add<Probe>("front");
    front.placement = top_left(50, 0, 100, 100);
    Widget& label = front.add<Widget>("label"); // Lets the pointer through to its parent
    label.placement = fill();
    desktop.layout(kView);

    CHECK(desktop.hit({ 10, 10 }) == &back);
    CHECK(desktop.hit({ 60, 10 }) == &front);
    CHECK_FALSE(desktop.hit({ 190, 90 }) == &back);
    CHECK_FALSE(desktop.hit({ 199, 99 }) == &label);

    SUBCASE("invisible widgets and their children are skipped")
    {
        front.visible = false;
        CHECK(desktop.hit({ 60, 10 }) == &back);
    }
    SUBCASE("update reports whether the interface has the pointer")
    {
        CHECK(desktop.update(at(60, 10)));
        CHECK(front.hovered());
        CHECK_FALSE(back.hovered());
        desktop.root().remove(back);
        desktop.root().remove(front);
        CHECK_FALSE(desktop.update(at(60, 10)));
    }
}

TEST_CASE("a pressed widget holds the pointer until release")
{
    Desktop desktop;
    Probe& p = desktop.root().add<Probe>("p");
    p.placement = top_left(0, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10, true));
    CHECK(p.pressed());
    desktop.update(at(120, 10, true));
    CHECK_FALSE(p.pressed()); // Held, but the pointer is outside
    CHECK_FALSE(p.hovered());
    desktop.update(at(120, 10, false));
    CHECK(p.events == std::vector<std::string> { "press 10", "drag 120", "release outside" });

    SUBCASE("released over it")
    {
        p.events.clear();
        desktop.update(at(10, 10, true));
        desktop.update(at(12, 10, false));
        CHECK(p.events == std::vector<std::string> { "press 10", "release inside" });
    }
    SUBCASE("a press that starts outside never reaches it")
    {
        p.events.clear();
        desktop.update(at(120, 10, true));
        desktop.update(at(10, 10, true));
        desktop.update(at(10, 10, false));
        CHECK(p.events.empty());
    }
    SUBCASE("disabled widgets take the pointer but are not pressed")
    {
        p.events.clear();
        p.enabled = false;
        CHECK(desktop.update(at(10, 10, true)));
        CHECK(p.events.empty());
    }
    SUBCASE("an absent pointer releases outside")
    {
        p.events.clear();
        desktop.update(at(10, 10, true));
        desktop.update({ { 10, 10 }, true, false });
        CHECK(p.events == std::vector<std::string> { "press 10", "release outside" });
    }
}

TEST_CASE("pressing a top-level widget raises it and gives it focus")
{
    Desktop desktop;
    Probe& a = desktop.root().add<Probe>("a");
    a.placement = top_left(0, 0, 100, 100);
    Widget& a_child = a.add<Widget>("child");
    Probe& b = desktop.root().add<Probe>("b");
    b.placement = top_left(150, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(190, 90));
    CHECK(b.focused());
    CHECK_FALSE(a.focused());

    desktop.update(at(10, 10, true));
    CHECK(desktop.root().children().back().get() == &a);
    CHECK(a.focused());
    CHECK(a_child.focused());
    CHECK_FALSE(b.focused());
}

TEST_CASE("closing widgets are detached after the update")
{
    Desktop desktop;
    Probe& a = desktop.root().add<Probe>("a");
    a.placement = top_left(0, 0, 50, 50);
    Probe& b = desktop.root().add<Probe>("b");
    b.placement = top_left(100, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10, true)); // a holds the pointer
    CHECK(a.updates == 1);
    a.close();
    desktop.update(at(10, 10, true));
    REQUIRE(desktop.root().children().size() == 1);
    CHECK(desktop.root().children()[0].get() == &b);
    // The held widget is gone; the release goes nowhere.
    CHECK_FALSE(desktop.update(at(150, 90, false)));
}

TEST_CASE("clipped parents hide children outside them")
{
    Desktop desktop;
    Widget& box = desktop.root().add<Widget>("box");
    box.placement = top_left(0, 0, 50, 50);
    box.clip_children = true;
    Probe& p = box.add<Probe>("p");
    p.placement = top_left(40, 0, 40, 10);
    desktop.layout(kView);

    CHECK(desktop.hit({ 45, 5 }) == &p);
    CHECK_FALSE(desktop.hit({ 60, 5 }));

    DrawList list;
    SkinLibrary skins;
    const FontSet fonts;
    DrawContext context { list, skins, fonts };
    desktop.draw(context);
    CHECK(list.clip() == nullptr); // Balanced
}
