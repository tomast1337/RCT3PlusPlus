// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/attraction_info.hpp"

#include <doctest/doctest.h>
#include <vector>

TEST_CASE("a ride suits the guests in the park whose preferred intensity takes it in")
{
    std::vector<rct3::save::Guest> guests(4);
    guests[3].in_park = false;
    guests[0].ride_preference = { .min = 1, .max = 5 };
    guests[1].ride_preference = { .min = 4, .max = 9 };
    guests[2].ride_preference = { .min = 6, .max = 12 };
    guests[3].ride_preference = { .min = 0, .max = 12 }; // Not in the park
    const auto s = rct3::ui::guests_suited(4.5f, guests);
    CHECK(s.suited == 2);
    CHECK(s.in_park == 3);
}
