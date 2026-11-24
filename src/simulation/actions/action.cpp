// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/action.hpp"

namespace rct3::simulation::actions
{

    Outcome check(const World& world, const Action& action)
    {
        return std::visit([&](const auto& kind) { return check(world, kind); }, action);
    }

    Outcome apply(World& world, const Action& action)
    {
        return std::visit(
            [&](const auto& kind) {
                Outcome checked = check(world, kind);
                if (checked)
                    apply(world, kind);
                return checked;
            },
            action);
    }

} // namespace rct3::simulation::actions
