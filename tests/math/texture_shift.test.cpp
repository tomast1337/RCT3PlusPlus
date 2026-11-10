// SPDX-License-Identifier: GPL-3.0-or-later
#include "math/texture_shift.hpp"

#include <doctest/doctest.h>

using namespace rct3::math;

TEST_CASE("a texture shift keeps only the fraction, either way")
{
    CHECK(TextureShift::by(0.0f).lengths == 0.0f);
    CHECK(TextureShift::by(1.25f).lengths == doctest::Approx(0.25f));
    CHECK(TextureShift::by(3.0f).lengths == 0.0f);
    CHECK(TextureShift::by(-0.25f).lengths == doctest::Approx(0.75f));
    CHECK(TextureShift::by(-2.0f).lengths == 0.0f);
    CHECK(TextureShift { }.lengths == 0.0f);
}
