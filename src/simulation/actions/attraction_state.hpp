// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opening and closing the park's rides and stalls.
#pragma once

#include "save/model/rides.hpp"
#include "simulation/sim_world.hpp"

namespace rct3::simulation::actions
{

    /// Puts an attraction in `state`, the running park with it: open lets guests on, testing runs it empty. A broken
    /// one stays shut until mended, opening once it is.
    void put_in_state(SimWorld& sim, save::model::AttractionCommon& attraction, save::model::AttractionState state);

} // namespace rct3::simulation::actions
