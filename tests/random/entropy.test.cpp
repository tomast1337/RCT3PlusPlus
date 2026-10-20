// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/entropy.hpp"

#include <doctest/doctest.h>

TEST_CASE("fresh seeds differ from call to call")
{
    const rct3::random::Seed a = rct3::random::fresh_seed();
    const rct3::random::Seed b = rct3::random::fresh_seed();
    CHECK(a != b);
}
