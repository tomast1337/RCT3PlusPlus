// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/attraction_objects.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("track pieces, placements and stations find their attraction")
{
    save::RideModel rides;
    save::Ride& coaster = rides.rides.emplace_back();
    coaster.track.emplace();
    coaster.track->pieces.resize(2);
    coaster.track->pieces[0].scenery = 10;
    coaster.track->pieces[1].scenery = 11;
    coaster.stations.push_back({ .entrance = 20, .exit = 21 });
    save::Ride& flat = rides.rides.emplace_back();
    flat.base.placement = 30;
    save::Stall& stall = rides.stalls.emplace_back();
    stall.base.placement = 40;

    std::vector<save::PlacedObject> objects(5);
    objects[0].id = 1;
    objects[0].owner_ref = 30;
    objects[1].id = 2;
    objects[1].owner_ref = 40;
    objects[2].id = 3;
    objects[2].owner_ref = 20;
    objects[3].id = 4;
    objects[3].owner_ref = 99; // Hangs from nothing known
    objects[4].id = 5;

    const AttractionObjects found = attraction_objects(rides, objects);
    CHECK(found.at(10) == AttractionRef { AttractionRef::Kind::Ride, 0 });
    CHECK(found.at(11) == AttractionRef { AttractionRef::Kind::Ride, 0 });
    CHECK(found.at(1) == AttractionRef { AttractionRef::Kind::Ride, 1 });
    CHECK(found.at(2) == AttractionRef { AttractionRef::Kind::Stall, 0 });
    CHECK(found.at(3) == AttractionRef { AttractionRef::Kind::Ride, 0 });
    CHECK_FALSE(found.contains(4));
    CHECK_FALSE(found.contains(5));
}
