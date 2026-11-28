// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions on how the park looks: the colours scenery and attractions are painted in, and the items a ride's fence
// and doors are shown as. All are free, as in the game. Each colour picked is final; the palette has no preview.
#pragma once

#include "save/model/rides.hpp"
#include "simulation/actions/world.hpp"

namespace rct3::simulation::actions
{

    /// Paints colour slot `slot` of the placed object saved under `object`.
    struct PaintObject
    {
        u64 object = 0;
        usize slot = 0;
        i32 colour = 0;
    };

    /// Paints colour slot `slot` of every object of the ride or stall saved under `attraction`.
    struct PaintAttraction
    {
        save::model::ObjectId attraction = 0;
        usize slot = 0;
        i32 colour = 0;
    };

    [[nodiscard]] Outcome check(const World& world, const PaintObject& action);
    void apply(World& world, const PaintObject& action);
    [[nodiscard]] Outcome check(const World& world, const PaintAttraction& action);
    void apply(World& world, const PaintAttraction& action);

} // namespace rct3::simulation::actions
