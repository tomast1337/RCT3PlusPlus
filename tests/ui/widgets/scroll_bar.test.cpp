// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/scroll_bar.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    ScrollBarStyle style()
    {
        ScrollBarStyle s;
        s.thickness = 10;
        s.step = 20;
        s.wheel_step = 50;
        s.repeat_delay = 0.4f;
        s.repeat_interval = 0.1f;
        s.min_rider = 5;
        return s;
    }

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }

    // A vertical bar 10 wide and 120 tall at the origin: arrows 0-10 and 110-120, track 10-110 (100 long).
    struct Fixture
    {
        Desktop desktop;
        ScrollBar& bar = desktop.root().add<ScrollBar>("bar", style(), Orientation::Vertical);
        Fixture()
        {
            bar.placement = top_left(0, 0, 10, 120);
            bar.content = 400;
            bar.visible = 100;
            desktop.layout({ 0, 0, 200, 200 });
        }
        void click(float y)
        {
            desktop.update(at(5, y, true));
            desktop.update(at(5, y));
        }
    };
} // namespace

TEST_CASE("the rider shows the share in view at the scroll position")
{
    Fixture f;
    ScrollBarParts p = f.bar.parts();
    CHECK(p.back == Rect { 0, 0, 10, 10 });
    CHECK(p.forward == Rect { 0, 110, 10, 120 });
    CHECK(p.track == Rect { 0, 10, 10, 110 });
    CHECK(p.rider == Rect { 0, 10, 10, 35 }); // A quarter of the content is in view

    f.bar.position = 300;
    CHECK(f.bar.parts().rider == Rect { 0, 85, 10, 110 });
    f.bar.content = 100000;
    CHECK(f.bar.parts().rider.height() == 5);
    f.bar.content = 50;
    CHECK(f.bar.parts().rider.empty());
}

TEST_CASE("arrows step and stay within the range")
{
    Fixture f;
    f.click(115);
    CHECK(f.bar.position == 20);
    f.click(5);
    f.click(5);
    CHECK(f.bar.position == 0);
    f.bar.position = 295;
    f.click(115);
    CHECK(f.bar.position == 300);
}

TEST_CASE("a held arrow repeats after a delay")
{
    Fixture f;
    f.desktop.update(at(5, 115, true));
    CHECK(f.bar.position == 20);
    f.desktop.update(at(5, 115, true), { 0.3f });
    CHECK(f.bar.position == 20);
    f.desktop.update(at(5, 115, true), { 0.15f }); // 0.45 s: one repeat
    CHECK(f.bar.position == 40);
    f.desktop.update(at(5, 115, true), { 0.2f }); // 0.65 s: repeats at 0.5 and 0.6
    CHECK(f.bar.position == 80);
    SUBCASE("not while the pointer is off the arrow")
    {
        f.desktop.update(at(50, 115, true), { 1 });
        CHECK(f.bar.position == 80);
    }
    SUBCASE("not after release")
    {
        f.desktop.update(at(5, 115), { 1 });
        f.desktop.update(at(5, 115), { 1 });
        CHECK(f.bar.position == 80);
    }
}

TEST_CASE("the rider drags; the rail centres it on the pointer first")
{
    Fixture f;
    SUBCASE("dragging the rider keeps the grab point")
    {
        f.desktop.update(at(5, 20, true)); // 10 into the rider
        CHECK(f.bar.position == 0);
        f.desktop.update(at(5, 57.5f, true)); // Rider start 47.5: half way along 75 units of travel
        CHECK(f.bar.position == doctest::Approx(150));
        f.desktop.update(at(5, 500, true));
        CHECK(f.bar.position == doctest::Approx(300));
    }
    SUBCASE("a press on the rail jumps there")
    {
        f.desktop.update(at(5, 60, true)); // Centre 60: start 47.5
        CHECK(f.bar.position == doctest::Approx(150));
        f.desktop.update(at(5, 22.5f, true));
        CHECK(f.bar.position == doctest::Approx(0));
    }
}

TEST_CASE("the wheel scrolls when there is something to scroll")
{
    Fixture f;
    CHECK(f.bar.on_wheel({ 0, -1 }));
    CHECK(f.bar.position == 50);
    CHECK(f.bar.on_wheel({ 0, 2 }));
    CHECK(f.bar.position == 0);
    f.bar.content = 50;
    CHECK_FALSE(f.bar.on_wheel({ 0, -1 }));
}

TEST_CASE("horizontal bars lay out along x")
{
    Desktop desktop;
    ScrollBar& bar = desktop.root().add<ScrollBar>("bar", style(), Orientation::Horizontal);
    bar.placement = top_left(0, 0, 120, 10);
    bar.content = 200;
    bar.visible = 100;
    bar.position = 100;
    desktop.layout({ 0, 0, 200, 200 });
    const ScrollBarParts p = bar.parts();
    CHECK(p.back == Rect { 0, 0, 10, 10 });
    CHECK(p.track == Rect { 10, 0, 110, 10 });
    CHECK(p.rider == Rect { 60, 0, 110, 10 });
}
