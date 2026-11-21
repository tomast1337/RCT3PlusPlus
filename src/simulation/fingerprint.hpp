// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fingerprint of the simulated park, to tell whether two runs that should go the same way did: the same park,
// stepped the same ticks, on another run, thread count, build or machine.
#pragma once

#include "core/types.hpp"
#include "simulation/sim_world.hpp"

namespace rct3::simulation
{

    /// FNV-1a over the tick, the seed and the gameplay stream, which entities each component's storage holds in
    /// its order, and the values that move each tick: where people stand and go, their needs, moods, money and
    /// dice, and how far each train has come. Floats count by their bits, so the smallest difference shows.
    [[nodiscard]] u64 fingerprint(const SimWorld& world);

} // namespace rct3::simulation
