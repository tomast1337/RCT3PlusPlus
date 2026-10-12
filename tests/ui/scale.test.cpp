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
