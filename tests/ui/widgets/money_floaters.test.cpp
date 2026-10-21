// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/money_floaters.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("a money float rises steadily and fades out by the end of its life")
{
    CHECK(money_float_rise(0) == 0);
    CHECK(money_float_rise(1) > 0);
    CHECK(money_float_rise(2) == doctest::Approx(2 * money_float_rise(1)));

    CHECK(money_float_alpha(0) == 1);
    CHECK(money_float_alpha(kMoneyFloatLife / 2) == 1);
    CHECK(money_float_alpha(kMoneyFloatLife - 0.1f) < 1);
    CHECK(money_float_alpha(kMoneyFloatLife) == 0);
    CHECK(money_float_alpha(kMoneyFloatLife + 1) == 0);
}

TEST_CASE("a money float's letters bob in a wave, never below the line")
{
    const float line = 20;
    for (std::size_t letter = 0; letter < 6; ++letter)
    {
        for (int step = 0; step < 25; ++step)
        {
            const float age = static_cast<float>(step) * 0.1f;
            CHECK(money_float_bob(letter, age, line) >= 0);
            CHECK(money_float_bob(letter, age, line) <= line);
        }
    }
    // Neighbouring letters stand at different heights at the same moment: a wave, not a jump.
    CHECK(money_float_bob(0, 0.3f, line) != doctest::Approx(money_float_bob(1, 0.3f, line)));
}
