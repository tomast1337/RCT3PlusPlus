// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/attraction_state.hpp"

#include "simulation/components.hpp"

namespace rct3::simulation::actions
{

    void put_in_state(SimWorld& sim, save::model::AttractionCommon& attraction, save::model::AttractionState state)
    {
        attraction.state = state;
        if (state == save::model::AttractionState::Open)
            attraction.flags &= ~save::model::AttractionCommon::kNeverOpened;
        for (auto [e, a] : sim.registry.view<Attraction>().each())
            if (a.saved == attraction.id)
            {
                const auto* upkeep = sim.registry.try_get<Upkeep>(e);
                a.open = state == save::model::AttractionState::Open && (upkeep == nullptr || !upkeep->broken);
                a.testing = state == save::model::AttractionState::Testing;
            }
    }

} // namespace rct3::simulation::actions
