// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opening and closing the park's rides and stalls: why one cannot be put in a state (a ride by its track and doors,
// a stall by its menu), and putting it there with the running park.
#pragma once

#include "park/attraction_objects.hpp"
#include "park/pools/pools.hpp"
#include "park/track/course.hpp"
#include "park/track/rules.hpp"
#include "save/model/rides.hpp"
#include "simulation/actions/world.hpp"
#include "simulation/sim_world.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rct3::simulation::actions
{

    /// Puts an attraction in a state: rides from their window, the construction panel or the park's list; stalls
    /// from theirs or the list.
    struct SetAttractionState
    {
        save::model::ObjectId attraction = 0;
        save::model::AttractionState state = save::model::AttractionState::Closed;
        bool tell = true; ///< Whether the player is told why, when it cannot be; the park's list puts each it can
    };

    [[nodiscard]] Outcome check(const World& world, const SetAttractionState& action);
    void apply(World& world, const SetAttractionState& action);

    /// Puts an attraction in `state`, the running park with it: open lets guests on, testing runs it empty. A broken
    /// one stays shut until mended, opening once it is.
    void put_in_state(SimWorld& sim, save::model::AttractionCommon& attraction, save::model::AttractionState state);

    /// Why attraction `ref` cannot be put in `state`, as a string key: a ride by its track and doors (`building` the
    /// ride under construction, if any), a stall when its menu is empty; nothing when it can.
    [[nodiscard]] std::optional<std::string> state_refusal(const save::model::RideModel& rides, const SimWorld& sim, const std::vector<rct3::park::track::TrackRules>& track_rules, const std::vector<std::optional<rct3::park::track::TrackCourse>>& courses, const rct3::park::pools::PoolMap& pools, std::optional<save::model::RideIndex> building, rct3::park::AttractionRef ref, save::model::AttractionState state);

} // namespace rct3::simulation::actions
