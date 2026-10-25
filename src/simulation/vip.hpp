// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Guests of honour (VIPs). Some scenario goals send a celebrity on a visit: the player is told a month and a
// week ahead, the VIP comes in at the gate on the day, stays the months the goal says and then heads home.
// While in the park they look round now and then: litter on their path and time on a broken or crashed ride
// wear their patience down, a happy mood builds it back, and they tick off what they came to see (different
// exciting coasters, fireworks, a themed area). The visit went well when they leave with patience left and
// everything seen; the goal is then met for good. A visit that went badly is tried again some months on.
//
// Only the goals of the level being played send anyone; the others' dates roll on a month at a time.

#include "save/model/park.hpp"
#include "simulation/park_messages.hpp"
#include "simulation/sim_world.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rct3::simulation
{
    struct Vip;

    /// Text key of celebrity `who`'s name; empty for a number with none.
    [[nodiscard]] std::string_view vip_name_key(std::uint8_t who) noexcept;

    /// How much of what `vip` came to see they have seen, 0..1: each coaster wanted counts once, as do
    /// fireworks and a themed area. With nothing to see, how happy they are.
    [[nodiscard]] float vip_opinion(const Vip& vip, const save::VipTarget& target, float happiness);

    /// What the park round a VIP holds that the simulation does not know itself.
    struct VipSurroundings
    {
        std::function<std::string(std::uint8_t who)> name { };                                        ///< Shown name of celebrity `who`
        std::function<std::int32_t(glm::vec3 at, std::int32_t theme, std::int32_t reach)> themed { }; ///< Scenery of `theme` within `reach` tiles of `at`
    };

    /// Moves the VIP goals of `set` on to calendar `time`, `game_seconds` after the last call: tells the player
    /// of a visit coming, lets the VIP in on the day, has them look round, and judges the visit as they leave,
    /// posting each to `messages`.
    void advance_vips(SimWorld& world, save::ObjectiveSet& set, float time, float game_seconds, ParkMessages& messages, const VipSurroundings& around);

} // namespace rct3::simulation
