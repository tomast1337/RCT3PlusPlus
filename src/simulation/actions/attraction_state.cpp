// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/attraction_state.hpp"

#include "core/maybe.hpp"
#include "simulation/actions/ids.hpp"
#include "simulation/components.hpp"
#include "simulation/swimming.hpp"

#include <algorithm>

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

    Outcome check(const World& world, const SetAttractionState& action)
    {
        std::optional<rct3::park::AttractionRef> ref;
        if (const std::optional<save::model::Ride&> ride = ride_of(world.rides, action.attraction))
            ref = rct3::park::AttractionRef::of(save::model::RideIndex { static_cast<usize>(&*ride - world.rides.rides.data()) });
        else if (const std::optional<save::model::Stall&> stall = stall_of(world.rides, action.attraction))
            ref = rct3::park::AttractionRef::of(save::model::StallIndex { static_cast<usize>(&*stall - world.rides.stalls.data()) });
        if (!ref)
            return std::unexpected(Refusal::Missing);
        if (auto refused = state_refusal(world.rides, world.sim, world.track_rules, world.courses, world.pools, world.building, *ref, action.state))
            return std::unexpected(Refusal { Refusal::Forbidden, action.tell ? std::move(*refused) : std::string { } });
        return Checked { };
    }

    void apply(World& world, const SetAttractionState& action)
    {
        if (const std::optional<save::model::Stall&> stall = stall_of(world.rides, action.attraction))
        {
            put_in_state(world.sim, stall->base, action.state);
            return;
        }
        const std::optional<save::model::Ride&> found = ride_of(world.rides, action.attraction);
        save::model::Ride& ride = *found;
        put_in_state(world.sim, ride.base, action.state);
        // Its trains are made ready to run once the park next updates.
        const save::model::RideIndex index { static_cast<usize>(&ride - world.rides.rides.data()) };
        if (action.state != save::model::AttractionState::Closed && ride.kind == save::model::RideKind::Tracked && !std::ranges::contains(world.readying, index))
            world.readying.push_back(index);
    }

    std::optional<std::string> state_refusal(const save::model::RideModel& rides, const SimWorld& sim, const std::vector<rct3::park::track::TrackRules>& track_rules, const std::vector<std::optional<rct3::park::track::TrackCourse>>& courses, const rct3::park::pools::PoolMap& pools, std::optional<save::model::RideIndex> building, rct3::park::AttractionRef ref, save::model::AttractionState state)
    {
        if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
        {
            if (state == save::model::AttractionState::Open && rides.stalls.at(ref.index).items.empty())
                return "CantOpenStallNoItems";
            return std::nullopt;
        }
        const usize ride = ref.index;
        const rct3::park::track::TrackRules rules = ride < track_rules.size() ? track_rules[ride] : rct3::park::track::TrackRules { };
        const auto course = ride < courses.size() && courses[ride] ? core::maybe(&*courses[ride]) : std::nullopt;
        // A pool ride's doors are its stations joined to a pool complex. A slide's station joins where the paving, a
        // pool path or the top of path stairs meets a side of its platform off the track, at the deck's height; a lazy
        // river's, which has no platform, where the paving lies across its track, each run of station stretches one
        // station.
        // shortcut: a river's only along the course's own stretch, not a split track's other stretches with
        // stations; upgrade when a pool ride may be split.
        const save::model::Ride& r = rides.rides.at(ride);
        std::vector<std::optional<rct3::park::pools::SlidePlatform>> platforms;
        if (rules.pool_ride && r.track)
            for (const save::model::TrackPiece& p : r.track->pieces)
                platforms.push_back(p.platform ? std::optional(rct3::park::pools::SlidePlatform { .col = p.platform->tile_x, .row = p.platform->tile_z, .height = p.platform->height, .turn = rct3::park::ModelTurn::from(p.platform->direction) }) : std::nullopt);
        usize joined = rct3::park::pools::joined_stations(pools, platforms);
        if (rules.pool_ride && course && course->points.size() == course->kinds.size() && std::ranges::none_of(platforms, [](const auto& p) { return p.has_value(); }))
            for (usize k = 0; k + 1 < course->kinds.size(); ++k)
            {
                if (course->kinds[k] != rct3::park::track::CourseKind::Station)
                    continue;
                std::vector<glm::vec3> run { course->points[k] };
                for (; k + 1 < course->kinds.size() && course->kinds[k] == rct3::park::track::CourseKind::Station; ++k)
                    run.push_back(course->points[k + 1]);
                joined += pool_beside(sim, run).has_value() ? 1 : 0;
            }
        return rct3::park::track::open_refusal(r, rules, state, building == ref.ride(), course, joined);
    }

} // namespace rct3::simulation::actions
