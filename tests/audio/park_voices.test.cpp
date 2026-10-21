// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/park_voices.hpp"

#include <doctest/doctest.h>

using namespace rct3::audio;

TEST_CASE("a park sound is placed where it happens, full loud near and silent far")
{
    const PlayParams p = heard({ 1.0f, 2.0f, 3.0f }, 0.8f);
    REQUIRE(p.at.has_value());
    CHECK(p.at->y == 2.0f);
    CHECK(p.volume == doctest::Approx(0.8f));
    CHECK(p.near == kNearVoice);
    CHECK(p.far == kFarVoice);
    CHECK(is_valid(p));
}

TEST_CASE("the crowd's chatter grows with the people about, silent for one")
{
    CHECK(chatter_level(0) == 0);
    CHECK(chatter_level(1) == 0);
    CHECK(chatter_level(2) > 0);
    CHECK(chatter_level(kCrowd / 2) < chatter_level(kCrowd));
    CHECK(chatter_level(kCrowd) == 1);
    CHECK(chatter_level(kCrowd * 4) == 1);
}

TEST_CASE("riders scream on fast or falling stretches, not on a gentle roll")
{
    CHECK(screams_per_second(5, 0) == 0);
    CHECK(screams_per_second(5, -10) == 0); // Climbing
    CHECK(screams_per_second(25, 0) > 0);
    CHECK(screams_per_second(5, 15) > 0);
    CHECK(screams_per_second(25, 15) >= screams_per_second(25, 0));
    CHECK(screams_per_second(100, 100) == screams_per_second(200, 200)); // Capped
}
