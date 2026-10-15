// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_models.hpp"

#include "park/person_motions.hpp"

#include <array>
#include <doctest/doctest.h>
#include <string_view>

using namespace rct3;
using park::KeyBlend;

TEST_CASE("the keys around a time are blended by how far it lies between them")
{
    // Keys at 0, 1, 2 and 3; the motion lasts 4 and comes back to its first key at the end.
    const std::array times { 1.0f, 2.0f, 3.0f, 4.0f };
    CHECK(park::blend_keys(times, 0.0f, true) == KeyBlend { 0, 0, 0.0f });
    CHECK(park::blend_keys(times, 0.5f, true) == KeyBlend { 0, 1, 0.5f });
    CHECK(park::blend_keys(times, 2.25f, true) == KeyBlend { 2, 3, 0.25f });
    CHECK(park::blend_keys(times, 3.5f, true) == KeyBlend { 3, 0, 0.5f });
}

TEST_CASE("a repeating motion starts again at its length, one played once holds its last key")
{
    const std::array times { 1.0f, 2.0f };
    CHECK(park::blend_keys(times, 4.5f, true) == KeyBlend { 0, 1, 0.5f });
    CHECK(park::blend_keys(times, 4.5f, false) == KeyBlend { 1, 1, 0.0f });
}

TEST_CASE("a single key or a time before the start shows the first key")
{
    const std::array one { 2.0f };
    CHECK(park::blend_keys(one, 1.0f, true) == KeyBlend { });
    const std::array times { 1.0f, 2.0f };
    CHECK(park::blend_keys(times, -1.0f, true) == KeyBlend { });
    CHECK(park::blend_keys({ }, 1.0f, true) == KeyBlend { });
}

TEST_CASE("a model's motions are found by number, and missing ones play its first")
{
    const std::array<std::string_view, 3> names { "Dance", "IdleStandard", "Walk" };
    const park::MotionIndex index(names);
    CHECK(index[park::motion::kWalk] == 2);
    CHECK(index[park::motion::kIdle] == 1);
    CHECK(index[-1] == 0);
    const auto unlisted = park::motion_number("applause");
    REQUIRE(unlisted);
    CHECK(index[*unlisted] == 0);
}
