// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/attraction_state.hpp"

#include "core/maybe.hpp"
#include "core/types.hpp"
#include "park/object/mesh.hpp"
#include "park/object/placement.hpp"
#include "park/pools/pools.hpp"
#include "park/track/rules.hpp"
#include "scene/park/scene.hpp"
#include "simulation/actions/attraction_state.hpp"
#include "simulation/components.hpp"
#include "simulation/fireworks.hpp"
#include "simulation/import_park.hpp"
#include "simulation/rides.hpp"
#include "simulation/swimming.hpp"

#include <algorithm>

namespace rct3::scene::park
{

    std::optional<std::string> state_refusal(const ParkScene& scene, rct3::park::AttractionRef ref, save::model::AttractionState state)
    {
        return simulation::actions::state_refusal(scene.rides, scene.sim, scene.track_rules, scene.courses, scene.pools, scene.building.ride, ref, state);
    }

    std::vector<ui::screens::info::StateChoice> state_choices(const ParkScene& scene, rct3::park::AttractionRef ref)
    {
        if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
            return ui::screens::info::state_choices(ui::screens::info::AttractionKind::Stall);
        const bool tested = ref.index >= scene.track_rules.size() || !scene.track_rules[ref.index].never_tested;
        return ui::screens::info::state_choices(ui::screens::info::attraction_kind(scene.rides.rides.at(ref.index)), tested);
    }

    std::vector<ui::screens::info::InfoChoice> state_fan(const ParkScene& scene, rct3::park::AttractionRef ref, const ui::StringTable& strings)
    {
        std::vector<ui::screens::info::InfoChoice> fan;
        for (const ui::screens::info::StateChoice& c : state_choices(scene, ref))
        {
            // Greyed only where the attraction cannot be put in that state, saying why; the state it is in stays live.
            const auto refused = state_refusal(scene, ref, c.state);
            fan.push_back({ std::string(refused ? c.disabled_icon : c.icon), std::u16string(strings.get(refused ? *refused : c.help)), !refused });
        }
        return fan;
    }

    void stage_show(ParkScene& scene, const save::model::RideShow& show)
    {
        // Passers-by watch from round the middle of the ground the ride's own object stands on.
        const auto saved = std::ranges::find(scene.rides.rides, show.ride, [](const save::model::Ride& r) { return r.base.id; });
        std::optional<glm::vec3> centre;
        for (const save::model::PlacedObject& o : scene.objects)
            if (const auto model = scene.models.find(rct3::park::object::model_ref(o)); saved != scene.rides.rides.end() && o.owner_ref == saved->base.placement && model != scene.models.end() && !model->second.seat_places.empty())
            {
                const glm::vec2 middle = rct3::park::object::footprint_middle(o, model->second.squares);
                centre = glm::vec3 { scene.grid.origin_x + middle.x * scene.grid.tile_size_x, 0.0f, scene.grid.origin_z + middle.y * scene.grid.tile_size_z };
                break;
            }
        for (auto [e, a, ride] : scene.sim.registry.view<simulation::Attraction, simulation::CycleRide>().each())
            if (a.saved == show.ride)
            {
                simulation::stage_show(ride, show);
                if (centre)
                    scene.sim.registry.emplace_or_replace<simulation::Stage>(e, simulation::Stage { .centre = *centre });
            }
    }

} // namespace rct3::scene::park
