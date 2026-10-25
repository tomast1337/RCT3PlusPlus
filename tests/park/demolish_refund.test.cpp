// SPDX-License-Identifier: GPL-3.0-or-later

#include "park/demolish_refund.hpp"

#include <doctest/doctest.h>

TEST_CASE("demolish_refund pays all back for a ride never opened")
{
    CHECK(rct3::park::demolish_refund(12'000, false) == 12'000);
}

TEST_CASE("demolish_refund pays 70% back once a ride has been opened")
{
    CHECK(rct3::park::demolish_refund(12'000, true) == 8'400);
}
