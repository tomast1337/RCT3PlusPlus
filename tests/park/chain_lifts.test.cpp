// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/chain_lifts.hpp"

#include <array>
#include <doctest/doctest.h>
#include <vector>

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
    const Chains runs = chain_runs(rides, { }, { });
    CHECK(runs.size() == 2);
    CHECK(runs.at(7) == 1);
    CHECK(runs.at(9) == -3);

    const std::array shifts { 0.25f, 0.0f, 0.25f };
    CHECK(chain_scroll(shifts, 1).lengths == doctest::Approx(0.25f));
    CHECK(chain_scroll(shifts, -3).lengths == doctest::Approx(0.75f));
    CHECK(chain_scroll(shifts, 0).lengths == 0);
    CHECK(chain_scroll(shifts, 4).lengths == 0);
}

TEST_CASE("a section that lays its piece from the far end takes the chain and the water the other way round")
{
    // One curve piece, laid by its own section turning left and by a right-hand curve that runs it from its far
    // end; each laid forwards and backwards, as built on from a station and built back from it.
    std::vector<save::PlacedObject> objects;
    for (std::uint64_t id = 1; id <= 4; ++id)
        objects.push_back({ .id = id, .symbol = "curve", .overlay = "tracks/flume" });
    ObjectModels models;
    models[model_key(objects[0])].runs_back = { "CurveRight" };
    save::RideModel rides;
    rides.rides.resize(1);
    rides.rides[0].track = save::Track { };
    rides.rides[0].track->pieces = {
        { .symbol = "Curve:tks", .scenery = 1 },
        { .symbol = "CurveRight:tks", .scenery = 2 },
        { .symbol = "Curve:tks", .scenery = 3, .reversed = true },
        { .symbol = "curveright:tks", .scenery = 4, .reversed = true },
    };
    const Chains runs = chain_runs(rides, objects, models);
    CHECK(runs.at(1) == 1);
    CHECK(runs.at(2) == -1);
    CHECK(runs.at(3) == -1);
    CHECK(runs.at(4) == 1);
}
