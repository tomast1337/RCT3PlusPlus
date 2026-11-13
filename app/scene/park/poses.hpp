// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the park scene's people are posed each frame (scene/park/refresh.hpp): the motion each plays and where,
// from walking, queueing and chatting to sitting on benches, eating, swimming and sliding, with what they hold
// and what they wear.
#pragma once

#include "park/person/mesh.hpp"
#include "scene/park/scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rct3::scene::park
{

    /// Shown person `i` posed as they are at `clock`, as the park draws them, at the finest level; with
    /// `simulate`, a simulated guest where and as the simulation has them. Nothing for those not drawn
    /// standing: gone home, or seated on a ride.
    [[nodiscard]] std::optional<rct3::park::person::PlacedPerson> pose_person(const ParkScene& scene, std::size_t i, float clock, bool simulate);

    /// The motion a person sliding down a slope plays, by name: on their front facing down it (`heading`, as
    /// simulation::Pose has it, against their `drift`), on their back facing up it, else sitting; staff, who
    /// have no sliding motions, flail.
    [[nodiscard]] std::string_view slide_motion(float heading, glm::vec2 drift, bool staff);

    /// `shown` as they look now: in their swimwear inside a pool complex, riding its slides or held or
    /// falling out of it, their clothes elsewhere (simulation::in_swimwear).
    rct3::park::person::PlacedPerson dressed(const ShownPerson& shown);

    /// Where a guest sits on a bench in the park, or stands to fire a water cannon: the seat their place on it
    /// comes to on its model. Nothing once the item is gone, or when its model has no such seat.
    /// shortcut: looks for the bench among all the park's objects, upgrade to a lookup if sitters show in profiles.
    std::optional<glm::mat4> bench_seat(const ParkScene& scene, entt::entity guest);

    /// Whether their swimwear shows a float of their own, an optional body piece whose picture names it.
    bool own_float(const rct3::park::person::PersonLibrary& library, const rct3::park::person::PlacedPerson& person);

    /// How guests go on foot in each gait (simulation::Gait), as the game's motions name them.
    extern const std::array<std::int32_t, kGaits> kGaitMotions;

} // namespace rct3::scene::park
