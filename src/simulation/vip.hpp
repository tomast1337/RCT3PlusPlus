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
#include "simulation/park/messages.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::simulation
{
    /// A guest of honour a scenario's goal sent. They go home only once their visit is over or
    /// their patience has run out.
    struct Vip
    {
        std::uint64_t goal = 0;                 ///< The goal's condition, by its id
        std::uint8_t who = 0;                   ///< Which celebrity, as the goal numbers them
        units::Share patience { 1 };            ///< Litter and breakdowns wear it down; at 0 they leave unhappy
        std::vector<entt::entity> coasters { }; ///< Different coasters they rode that were exciting enough
        bool fireworks = false;
        bool themed = false;
        float came = 0;          ///< Calendar time they came in
        units::Seconds look { }; ///< Until they next look round
        float happiness_sum = 0; ///< Over their looks, for their average
        std::uint32_t looks = 0;
        bool leaving = false;
        units::Seconds poll { };      ///< Until they next look for fans about them
        std::uint32_t fans = 0;       ///< Gathered round them for an autograph now
        std::uint32_t signatures = 0; ///< Autographs given since their last look round
    };

    /// Text key of celebrity `who`'s name; empty for a number with none.
    [[nodiscard]] std::string_view vip_name_key(std::uint8_t who) noexcept;

    /// How much of what `vip` came to see they have seen, 0..1: each coaster wanted counts once, as do
    /// fireworks and a themed area. With nothing to see, how happy they are.
    [[nodiscard]] float vip_opinion(const Vip& vip, const save::model::VipTarget& target, float happiness);

    /// What the park round a VIP holds that the simulation does not know itself.
    struct VipSurroundings
    {
        std::function<std::string(std::uint8_t who)> name { };                                        ///< Shown name of celebrity `who`
        std::function<std::int32_t(glm::vec3 at, std::int32_t theme, std::int32_t reach)> themed { }; ///< Scenery of `theme` within `reach` tiles of `at`
    };

    /// Moves the VIP goals of `set` on to calendar `time`, `game_seconds` after the last call: tells the player
    /// of a visit coming, lets the VIP in on the day, has them look round, and judges the visit as they leave,
    /// posting each to `messages`.
    void advance_vips(SimWorld& world, save::model::ObjectiveSet& set, float time, float game_seconds, park::ParkMessages& messages, const VipSurroundings& around);

} // namespace rct3::simulation
