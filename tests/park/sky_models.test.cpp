// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky_models.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("a glowing picture is see-through where it is dark")
{
    ovl::RgbaImage image { 3, 1, { 0, 0, 0, 255, 255, 255, 255, 255, 64, 32, 0, 255 } };
    glowing(image);
    CHECK(image.pixels[3] == 0);   // Black: clear
    CHECK(image.pixels[7] == 255); // White: solid white
    CHECK(image.pixels[4] == 255);
    // A dim orange: its hue at full brightness, as opaque as it was bright.
    CHECK(image.pixels[8] == 255);
    CHECK(image.pixels[9] == 127);
    CHECK(image.pixels[10] == 0);
    CHECK(image.pixels[11] == 64);
}

TEST_CASE("a glowing picture keeps the see-through parts it had")
{
    ovl::RgbaImage image { 1, 1, { 200, 200, 200, 128 } };
    glowing(image);
    CHECK(image.pixels[3] == 100);
}
