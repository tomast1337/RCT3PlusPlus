// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/ids.hpp"

#include "core/types.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::actions;

TEST_CASE("a ride or stall is found by its id however the lists around it change")
{
    save::model::RideModel rides;
    for (const save::model::ObjectId id : { 11u, 12u, 13u })
        rides.rides.push_back({ .base = { .id = id } });
    rides.stalls.push_back({ .base = { .id = 21 } });
    // The first ride goes; the third keeps its id, though not its place.
    rides.rides.erase(rides.rides.begin());
    REQUIRE(ride_of(rides, 13));
    CHECK(ride_of(rides, 13)->base.id == 13);
    CHECK_FALSE(ride_of(rides, 11));
    // A ride's id is not a stall's.
    CHECK_FALSE(stall_of(rides, 13));
    CHECK(stall_of(rides, 21));
    // A demolished one is gone, though its entry stays.
    rides.stalls.front().base.demolished = true;
    CHECK_FALSE(stall_of(rides, 21));
}
