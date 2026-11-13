// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/attraction_state.hpp"

#include "core/maybe.hpp"
#include "park/pools/pools.hpp"
#include "park/track/rules.hpp"
#include "scene/park/scene.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/rides.hpp"
#include "simulation/swimming.hpp"

#include <algorithm>

namespace rct3::scene::park
{

    void set_attraction_state(ParkScene& scene, save::model::AttractionCommon& attraction, save::model::AttractionState state)
    {
        attraction.state = state;
        if (state == save::model::AttractionState::Open)
            attraction.flags &= ~save::model::AttractionCommon::kNeverOpened;
        for (auto [e, a] : scene.sim.registry.view<simulation::Attraction>().each())
            if (a.saved == attraction.id)
            {
                // A broken one stays shut until mended, opening once it is.
                const auto* upkeep = scene.sim.registry.try_get<simulation::Upkeep>(e);
                a.open = state == save::model::AttractionState::Open && (upkeep == nullptr || !upkeep->broken);
                a.testing = state == save::model::AttractionState::Testing;
            }
    }

    std::optional<std::string> state_refusal(const ParkScene& scene, rct3::park::AttractionRef ref, save::model::AttractionState state)
    {
        if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
        {
            if (state == save::model::AttractionState::Open && scene.rides.stalls.at(ref.index).items.empty())
                return "CantOpenStallNoItems";
            return std::nullopt;
        }
        const std::size_t ride = ref.index;
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
        std::size_t joined = rct3::park::pools::joined_stations(scene.pools, platforms);
        if (rules.pool_ride && course && course->points.size() == course->kinds.size() && std::ranges::none_of(platforms, [](const auto& p) { return p.has_value(); }))
            for (std::size_t k = 0; k + 1 < course->kinds.size(); ++k)
            {
                if (course->kinds[k] != rct3::park::track::CourseKind::Station)
                    continue;
                std::vector<glm::vec3> run { course->points[k] };
                for (; k + 1 < course->kinds.size() && course->kinds[k] == rct3::park::track::CourseKind::Station; ++k)
                    run.push_back(course->points[k + 1]);
                joined += simulation::pool_beside(scene.sim, run).has_value() ? 1 : 0;
            }
        return rct3::park::track::open_refusal(r, rules, state, scene.building.ride == ride, course, joined);
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

    std::optional<std::string> change_ride_state(ParkScene& scene, std::size_t ride, save::model::AttractionState state)
    {
        auto refused = state_refusal(scene, { .kind = rct3::park::AttractionRef::Kind::Ride, .index = ride }, state);
        if (!refused)
            set_attraction_state(scene, scene.rides.rides.at(ride).base, state);
        if (!refused && state != save::model::AttractionState::Closed && scene.rides.rides[ride].kind == save::model::RideKind::Tracked && !std::ranges::contains(scene.readying, ride))
            scene.readying.push_back(ride);
        return refused;
    }

    std::optional<std::string> change_stall_state(ParkScene& scene, std::size_t stall, save::model::AttractionState state)
    {
        auto refused = state_refusal(scene, { .kind = rct3::park::AttractionRef::Kind::Stall, .index = stall }, state);
        if (!refused)
            set_attraction_state(scene, scene.rides.stalls.at(stall).base, state);
        return refused;
    }

    void stage_show(ParkScene& scene, const save::model::RideShow& show)
    {
        for (auto [_, a, ride] : scene.sim.registry.view<simulation::Attraction, simulation::CycleRide>().each())
            if (a.saved == show.ride)
                simulation::stage_show(ride, show);
    }

} // namespace rct3::scene::park
