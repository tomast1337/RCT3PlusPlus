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
        if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
        {
            if (state == save::model::AttractionState::Open && scene.rides.stalls.at(ref.index).items.empty())
                return "CantOpenStallNoItems";
            return std::nullopt;
        }
        const usize ride = ref.index;
        const rct3::park::track::TrackRules rules = ride < scene.track_rules.size() ? scene.track_rules[ride] : rct3::park::track::TrackRules { };
        const auto course = ride < scene.courses.size() && scene.courses[ride] ? core::maybe(&*scene.courses[ride]) : std::nullopt;
        // A pool ride's doors are its stations joined to a pool complex. A slide's station joins where the paving, a
        // pool path or the top of path stairs meets a side of its platform off the track, at the deck's height; a lazy
        // river's, which has no platform, where the paving lies across its track, each run of station stretches one
        // station.
        // shortcut: a river's only along the course's own stretch, not a split track's other stretches with
        // stations; upgrade when a pool ride may be split.
        const save::model::Ride& r = scene.rides.rides.at(ride);
        std::vector<std::optional<rct3::park::pools::SlidePlatform>> platforms;
        if (rules.pool_ride && r.track)
            for (const save::model::TrackPiece& p : r.track->pieces)
                platforms.push_back(p.platform ? std::optional(rct3::park::pools::SlidePlatform { .col = p.platform->tile_x, .row = p.platform->tile_z, .height = p.platform->height, .turn = rct3::park::ModelTurn::from(p.platform->direction) }) : std::nullopt);
        usize joined = rct3::park::pools::joined_stations(scene.pools, platforms);
        if (rules.pool_ride && course && course->points.size() == course->kinds.size() && std::ranges::none_of(platforms, [](const auto& p) { return p.has_value(); }))
            for (usize k = 0; k + 1 < course->kinds.size(); ++k)
            {
                if (course->kinds[k] != rct3::park::track::CourseKind::Station)
                    continue;
                std::vector<glm::vec3> run { course->points[k] };
                for (; k + 1 < course->kinds.size() && course->kinds[k] == rct3::park::track::CourseKind::Station; ++k)
                    run.push_back(course->points[k + 1]);
                joined += simulation::pool_beside(scene.sim, run).has_value() ? 1 : 0;
            }
        return rct3::park::track::open_refusal(r, rules, state, scene.building.ride == ref.ride(), course, joined);
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

    std::optional<std::string> change_ride_state(ParkScene& scene, save::model::RideIndex ride, save::model::AttractionState state)
    {
        auto refused = state_refusal(scene, rct3::park::AttractionRef::of(ride), state);
        if (!refused)
            simulation::actions::put_in_state(scene.sim, scene.rides.rides.at(ride.value).base, state);
        if (!refused && state != save::model::AttractionState::Closed && scene.rides.rides[ride.value].kind == save::model::RideKind::Tracked && !std::ranges::contains(scene.readying, ride))
            scene.readying.push_back(ride);
        return refused;
    }

    std::optional<std::string> change_stall_state(ParkScene& scene, save::model::StallIndex stall, save::model::AttractionState state)
    {
        auto refused = state_refusal(scene, rct3::park::AttractionRef::of(stall), state);
        if (!refused)
            simulation::actions::put_in_state(scene.sim, scene.rides.stalls.at(stall.value).base, state);
        return refused;
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
