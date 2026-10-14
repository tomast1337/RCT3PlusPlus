// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/entropy.hpp"

#include <doctest/doctest.h>

TEST_CASE("fresh seeds differ from call to call")
{
    const std::uint64_t a = rct3::random::fresh_seed();
    const std::uint64_t b = rct3::random::fresh_seed();
    CHECK(a != b);
}
