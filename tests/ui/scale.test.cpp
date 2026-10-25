// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/scale.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("automatic scale grows in whole steps with the window height")
{
    CHECK(auto_scale(0) == 1);
    CHECK(auto_scale(600) == 1);
    CHECK(auto_scale(1080) == 1);
    CHECK(auto_scale(1440) == 2);
    CHECK(auto_scale(2160) == 3);
}

TEST_CASE("a chosen scale replaces the automatic one")
{
    CHECK(interface_scale(2160, std::nullopt) == 3);
    CHECK(interface_scale(2160, 1.0f) == 1);
    CHECK(interface_scale(600, 4.0f) == 4);
}

TEST_CASE("the interface keeps its size in the window whatever the render scale")
{
    // A 1440-pixel window drawn at half its pixels, or half again more.
    CHECK(interface_scale(720, std::nullopt, 0.5f) == 1);
    CHECK(interface_scale(2160, std::nullopt, 1.5f) == 3);
    CHECK(interface_scale(720, 2.0f, 0.5f) == 1);
}
