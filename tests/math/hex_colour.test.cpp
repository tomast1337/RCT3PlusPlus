// SPDX-License-Identifier: GPL-3.0-or-later
#include "math/hex_colour.hpp"

#include <doctest/doctest.h>

using namespace rct3;

static_assert(hex("#000000") == HexColour { 0, 0, 0, 255 });
static_assert(hex("#ff8001") == HexColour { 255, 128, 1, 255 });
static_assert(hex("#A0b0C0") == HexColour { 160, 176, 192, 255 });
static_assert(hex("#12345678") == HexColour { 0x12, 0x34, 0x56, 0x78 });

TEST_CASE("hex colours turn into unit floats")
{
    const glm::vec4 c = unit(hex("#ff000080"));
    CHECK(c.r == 1.0f);
    CHECK(c.g == 0.0f);
    CHECK(c.b == 0.0f);
    CHECK(c.a == doctest::Approx(128 / 255.0));
}
