// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulated park: every entity and its components, and the clock and seed the run goes by. Two worlds
// with the same contents, seed and tick go on identically.
//
// Includes EnTT, which is large: include this only where the registry is needed.
#pragma once

#include "simulation/entt.hpp"
#include "simulation/path_network.hpp"

#include <cstdint>

namespace rct3::simulation
{

    struct SimWorld
    {
        entt::registry registry;
        PathNetwork paths;
        std::uint64_t tick = 0; ///< Simulation steps run so far
        std::uint64_t seed = 0; ///< Randomness of the whole run derives from it
    };

} // namespace rct3::simulation
