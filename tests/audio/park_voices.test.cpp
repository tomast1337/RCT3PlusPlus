// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/park_voices.hpp"

#include <doctest/doctest.h>

using namespace rct3::audio;

TEST_CASE("a park sound fades with distance and pans toward its side of the view")
{
    const glm::vec3 eye { 0.0f };
    const glm::vec3 right { 1.0f, 0.0f, 0.0f };
    CHECK(heard(eye, right, { 0.0f, 0.0f, 5.0f }, 0.8f).volume == doctest::Approx(0.8f));
    CHECK(heard(eye, right, { 0.0f, 0.0f, 5.0f }, 0.8f).pan == doctest::Approx(0.0f));
    const float halfway = (kNearVoice + kFarVoice) / 2;
    CHECK(heard(eye, right, { 0.0f, 0.0f, halfway }, 1.0f).volume == doctest::Approx(0.5f));
    CHECK(heard(eye, right, { 0.0f, 0.0f, kFarVoice + 1 }, 1.0f).volume == 0);
    CHECK(heard(eye, right, { 10.0f, 0.0f, 0.0f }, 1.0f).pan > 0);
    CHECK(heard(eye, right, { -10.0f, 0.0f, 0.0f }, 1.0f).pan < 0);
    CHECK(heard(eye, right, { -10.0f, 0.0f, 0.0f }, 1.0f).pan > -1);
    CHECK(is_valid(heard(eye, right, eye, 1.0f)));
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
