// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything a player can ask of the park, as one value: a small struct per kind, which names what it changes by
// ids that stay while the park's lists change. The interface turns input into actions and queues them (queue.hpp);
// only applying them changes the park.
#pragma once

#include "simulation/actions/park_management.hpp"
#include "simulation/actions/ride_settings.hpp"
#include "simulation/actions/world.hpp"

#include <variant>

namespace rct3::simulation::actions
{

    using Action = std::variant<LoanStep, SetLoan, ChangeRide, CallMechanic, RenameAttraction>;

    /// Whether `action` can be done in `world` as it is now, and what it costs.
    [[nodiscard]] Outcome check(const World& world, const Action& action);

    /// Checks `action` and, when it can be done, does it. A refused action changes nothing.
    Outcome apply(World& world, const Action& action);

} // namespace rct3::simulation::actions
