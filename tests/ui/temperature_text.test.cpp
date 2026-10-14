// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/temperature_text.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("temperatures are whole degrees, rounded down")
{
    CHECK(temperature_text(21.9f, false) == u"21°C");
    CHECK(temperature_text(-0.5f, false) == u"-1°C");
    CHECK(temperature_text(0, true) == u"32°F");
    CHECK(temperature_text(37, true) == u"98°F");
}
