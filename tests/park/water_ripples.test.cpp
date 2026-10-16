// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_ripples.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("a ripple's ring widens at its speed and its crests fall as it ages")
{
    Ripples r;
    start_ripple(r, { 2, 3 });
    REQUIRE(live_ripples(r) == 1);
    const Ripple& first = r.ripples[0];
    CHECK(ripple_amplitude(first) == doctest::Approx(kRippleLook.height));
    advance_ripples(r, 0.5f);
    CHECK(ripple_radius(first) == doctest::Approx(0.5f * kRippleLook.speed));
    const float earlier = ripple_amplitude(first);
    advance_ripples(r, 0.5f);
    CHECK(ripple_amplitude(first) < earlier);
}

TEST_CASE("the water stands highest on the ring and is still far from it")
{
    Ripples r;
    start_ripple(r, { 0, 0 });
    advance_ripples(r, 1);
    const float radius = ripple_radius(r.ripples[0]);
    CHECK(ripple_height(r, { radius, 0 }) == doctest::Approx(ripple_amplitude(r.ripples[0])));
    CHECK(ripple_height(r, { 0, radius }) == doctest::Approx(ripple_height(r, { radius, 0 })));
    CHECK(std::abs(ripple_height(r, { radius + 10 * kRippleLook.width, 0 })) < 1e-6f);
}

TEST_CASE("overlapping ripples add up")
{
    Ripples one;
    start_ripple(one, { 0, 0 });
    Ripples two = one;
    start_ripple(two, { 0, 0 });
    CHECK(ripple_height(two, { 0, 0 }) == doctest::Approx(2 * ripple_height(one, { 0, 0 })));
}

TEST_CASE("a ripple ends once it has died down, freeing its place")
{
    Ripples r;
    start_ripple(r, { 0, 0 });
    for (int i = 0; i < 200 && live_ripples(r) > 0; ++i)
        advance_ripples(r, 0.1f);
    CHECK(live_ripples(r) == 0);
    CHECK(ripple_height(r, { 0, 0 }) == 0);
}

TEST_CASE("with every place taken, a new ripple replaces the oldest")
{
    Ripples r;
    for (std::size_t i = 0; i < kMaxRipples; ++i)
    {
        start_ripple(r, { static_cast<float>(i), 0 });
        advance_ripples(r, 0.01f);
    }
    REQUIRE(live_ripples(r) == kMaxRipples);
    start_ripple(r, { 99, 0 });
    CHECK(live_ripples(r) == kMaxRipples);
    CHECK(r.ripples[0].middle.x == doctest::Approx(99)); // The first started was the oldest
}

TEST_CASE("the renderer is handed each ripple's middle, ring, height and shape")
{
    Ripples r;
    start_ripple(r, { 4, -2 }, 0.5f);
    advance_ripples(r, 1);
    const RippleTexels t = ripple_texels(r);
    CHECK(t[0].x == doctest::Approx(4));
    CHECK(t[0].y == doctest::Approx(-2));
    CHECK(t[0].z == doctest::Approx(ripple_radius(r.ripples[0])));
    CHECK(t[0].w == doctest::Approx(ripple_amplitude(r.ripples[0])));
    CHECK(t[1].w == 0); // Free places read as none
    CHECK(t[kMaxRipples].y == doctest::Approx(kRippleLook.width));
}
