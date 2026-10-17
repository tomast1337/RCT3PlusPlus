// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_screen_layout.hpp"

#include <cmath>
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

TEST_CASE("a frame at one scale keeps circles round, the design held to the bottom-left corner")
{
    for (const Rect screen : { Rect { 0, 0, 1900, 800 }, Rect { 0, 0, 800, 600 }, Rect { 10, 20, 650, 1000 } })
    {
        CAPTURE(screen.width());
        const Frame f = frame_for(screen, true);
        CHECK(f.sx == doctest::Approx(f.sy));
        CHECK(f.sx == doctest::Approx(f.round));
        const Point corner = f.at({ 0, kDesignHeight });
        CHECK(corner.x == doctest::Approx(screen.x0));
        CHECK(corner.y == doctest::Approx(screen.y1));
        // A button is as wide as it is high, and points on the ring all lie as far from its centre.
        const Rect b = button_rect(f, on_ring(1, kMenuRadius), 20);
        CHECK(b.width() == doctest::Approx(b.height()).epsilon(0.05));
        const Point c = f.at(kRingCentre);
        for (const float angle : { 0.5f, 2.0f, 3.5f })
        {
            const Point p = f.at(on_ring(angle, kMenuRadius));
            CHECK(std::hypot(p.x - c.x, p.y - c.y) == doctest::Approx(kMenuRadius * f.round));
        }
        // The screen's edges in design units hold the whole design and reach past it on the long side.
        const Rect edges = f.design_screen();
        CHECK(edges.x0 == doctest::Approx(0));
        CHECK(edges.y1 == doctest::Approx(kDesignHeight));
        CHECK(edges.x1 >= kDesignWidth - 0.01f);
        CHECK(edges.y0 <= 0.01f);
    }
}

TEST_CASE("what runs along the strip spans the screen on any frame")
{
    const Rect design { 256, 700, 768, 720 };
    const Frame stretched = frame_for({ 0, 0, 2048, 768 });
    const Rect a = stretched.strip(design);
    const Rect b = stretched.stretched(design);
    CHECK(a.x0 == doctest::Approx(b.x0));
    CHECK(a.x1 == doctest::Approx(b.x1));

    const Frame uniform = frame_for({ 0, 0, 2048, 768 }, true);
    const Rect c = uniform.strip(design);
    CHECK(c.x0 == doctest::Approx(256));
    CHECK(c.x1 == doctest::Approx(2048 - 256));
    CHECK(c.height() == doctest::Approx(20));
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

    // On a wider, taller screen the corner moves out, but the badge keeps its size.
    const Frame wide = frame_for({ 0, 0, 2 * kDesignWidth, 1.5f * kDesignHeight });
    const Rect moved = badge_rect(wide, 0);
    CHECK(moved.width() == doctest::Approx(kBadgeSize));
    CHECK(moved.height() == doctest::Approx(kBadgeSize));
    CHECK(moved.x0 == std::floor(moved.x0)); // Snapped
    CHECK(std::abs(moved.x0 - 2 * first.x0) <= 2);
    CHECK(std::abs(moved.y1 - 1.5f * first.y1) <= 2);
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

TEST_CASE("on a wide screen the ring round a menu button stays round and the band stretches")
{
    const Frame square = frame_for({ 0, 0, kDesignWidth, kDesignHeight });
    const Frame wide = frame_for({ 0, 0, 2 * kDesignWidth, kDesignHeight });
    for (std::size_t i = 0; i < kMaxMenuEntries; ++i)
    {
        const LabelBox box = label_box(wide, i);
        CHECK(box.patch.width() / box.patch.height() == doctest::Approx(kLabelPatch.width() / kLabelPatch.height()));
        CHECK(box.patch.width() == doctest::Approx(kLabelPatch.width() * wide.round));
        // The ring keeps its place round the button.
        const Rect button = button_rect(wide, on_ring(menu_angle(i), kMenuRadius), kMenuButtonSize);
        CHECK(box.patch.x0 < button.x0);
        CHECK(box.patch.x1 > button.x1);
        CHECK(box.patch.y0 < button.y0);
        CHECK(box.patch.y1 > button.y1);
        // The band starts under the ring and reaches twice as far as on the design screen.
        const LabelBox base = label_box(square, i);
        CHECK(box.band.x0 < box.patch.x1);
        CHECK(box.band.x1 == doctest::Approx(2 * base.band.x1));
        CHECK(box.band.height() == doctest::Approx(base.band.height()));
        CHECK(box.text.x0 > box.patch.x0);
        CHECK(box.text.x1 > box.text.x0);
    }
    // On the design screen nothing moves.
    const LabelBox base = label_box(square, 0);
    const Point p = on_ring(menu_angle(0), kMenuRadius);
    CHECK(base.patch.x0 == doctest::Approx(p.x + kLabelBoxOffset.x + kLabelPatch.x0));
    CHECK(base.band.x1 == doctest::Approx(p.x + kLabelBoxOffset.x + kLabelBand.x1));
}
