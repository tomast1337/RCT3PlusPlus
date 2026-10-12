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
