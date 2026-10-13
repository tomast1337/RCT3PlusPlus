// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_screen_layout.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;
using namespace rct3::ui::front_screen;

TEST_CASE("the design frame stretches to the screen, round things by the smaller scale")
{
    const Frame f = frame_for({ 0, 0, 2048, 1152 });
    CHECK(f.sx == doctest::Approx(2));
    CHECK(f.sy == doctest::Approx(1.5));
    CHECK(f.round == doctest::Approx(1.5));
    const Rect r = f.stretched({ 10, 20, 30, 40 });
    CHECK(r.x0 == doctest::Approx(20));
    CHECK(r.y1 == doctest::Approx(60));

    const Rect b = button_rect(f, { 100, 100 }, 10);
    CHECK(b.width() == doctest::Approx(15));
    CHECK(b.height() == doctest::Approx(15));
    CHECK((b.x0 + b.x1) / 2 == doctest::Approx(200));
    CHECK((b.y0 + b.y1) / 2 == doctest::Approx(150));
}

TEST_CASE("angles turn anticlockwise from the right")
{
    const Point right = on_ring(0, 100);
    CHECK(right.x == doctest::Approx(kRingCentre.x + 100));
    const Point up = on_ring(1.5707964f, 100);
    CHECK(up.y == doctest::Approx(kRingCentre.y - 100));
}

TEST_CASE("menu buttons run down the left edge, each label box beside its button")
{
    const Frame f = frame_for({ 0, 0, kDesignWidth, kDesignHeight });
    float last_y = -1;
    for (std::size_t i = 0; i < kMaxMenuEntries; ++i)
    {
        const Point p = on_ring(menu_angle(i), kMenuRadius);
        CHECK(p.x > 0);
        CHECK(p.x < 200);
        CHECK(p.y > last_y);
        last_y = p.y;
        const LabelBox box = label_box(f, i);
        CHECK(box.patch.x0 < p.x);
        CHECK(box.patch.x1 > p.x);
        CHECK(box.text.x0 > p.x);
    }
    // Back and Quit sit low in the middle of the screen, Quit to the right of Back.
    const Point back = on_ring(kSmallAngles[0], kSmallRadius);
    const Point quit = on_ring(kSmallAngles[1], kSmallRadius);
    CHECK(back.y > kDesignHeight * 3 / 4);
    CHECK(quit.x > back.x);
}

TEST_CASE("badges sit below the circle's centre, each further round clockwise")
{
    const Frame f = frame_for({ 0, 0, kDesignWidth, kDesignHeight });
    const Rect first = badge_rect(f, 0);
    const Rect second = badge_rect(f, 1);
    CHECK(first.width() == doctest::Approx(kBadgeSize));
    CHECK(first.y0 > kRingCentre.y);
    CHECK(second.x0 < first.x0); // Clockwise from below-right goes left
}

TEST_CASE("buttons ease round first, then out, and arrive")
{
    Polar p = kStart;
    const Polar goal { 3, 650 };
    CHECK_FALSE(ease(p, goal, 0.1f));
    CHECK(p.angle > kStart.angle);
    CHECK(p.radius == kStart.radius); // Not while the angle is far
    int frames = 0;
    while (!ease(p, goal, 1.0f / 60) && frames < 1000)
        ++frames;
    CHECK(frames < 1000);
    CHECK(p.angle == goal.angle);
    CHECK(p.radius == goal.radius);
}
