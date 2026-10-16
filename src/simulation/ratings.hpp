// SPDX-License-Identifier: GPL-3.0-or-later
//
// How exciting, intense and sickening a ride is, from what a test run found: our own model, not the original
// game's. The ratings are on the same scale as a park keeps them, about 0 to 10 and more for wild rides.
#pragma once

#include "save/model/rides.hpp"
#include "simulation/coaster.hpp"

#include <optional>

namespace rct3::simulation
{

    struct Ratings
    {
        float excitement = 0;
        float intensity = 0;
        float nausea = 0;
    };

    /// The ratings a test run with these results earns. Forces are in metres per second squared.
    [[nodiscard]] Ratings rate(const save::TestResults& test);

    /// A test run of `coaster` from a standing start, as far as the model knows: speeds, time, length and the
    /// drops it goes down. Forces, air time and turns are left at nothing. Nothing if the train does not come
    /// round within `most` seconds.
    [[nodiscard]] std::optional<save::TestResults> test_lap(Coaster coaster, float most = 1200);

} // namespace rct3::simulation
