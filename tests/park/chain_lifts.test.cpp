// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/chain_lifts.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("a chain moves its picture with its lift's speed, wrapping round, and stands while park time does")
{
    // A tenth of a second at a metre a second moves it 0.4 of a picture, the way the shift falls.
    CHECK(chain_shift(0.5f, 1, 0.1f) == doctest::Approx(0.1f));
    CHECK(chain_shift(0.1f, 1, 0.1f) == doctest::Approx(0.7f));
    CHECK(chain_shift(0.3f, 5, 0) == doctest::Approx(0.3f));
    CHECK(chain_shift(0.3f, 0, 2) == doctest::Approx(0.3f));
}

TEST_CASE("pieces laid backwards take their ride's chain the other way round")
{
    save::RideModel rides;
    rides.rides.resize(3);
    rides.rides[0].track = save::Track { };
    rides.rides[0].track->pieces = { { .scenery = 7 }, { .scenery = 0 } };
    rides.rides[2].track = save::Track { };
    rides.rides[2].track->pieces = { { .scenery = 9, .reversed = true } };
    const Chains runs = chain_runs(rides);
    CHECK(runs.size() == 2);
    CHECK(runs.at(7) == 1);
    CHECK(runs.at(9) == -3);

    const std::array shifts { 0.25f, 0.0f, 0.25f };
    CHECK(chain_scroll(shifts, 1) == doctest::Approx(0.25f));
    CHECK(chain_scroll(shifts, -3) == doctest::Approx(0.75f));
    CHECK(chain_scroll(shifts, 0) == 0);
    CHECK(chain_scroll(shifts, 4) == 0);
}
