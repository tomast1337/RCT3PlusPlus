// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Guests of honour (VIPs). Some scenario goals send a celebrity on a visit: the player is told a month and a
// week ahead, the VIP comes in at the gate on the day with their bodyguards, stays the months the goal says and
// then heads home. They come with stops in mind, what they want to see: the coasters exciting enough, most
// exciting first, then a pool. They walk to each in turn, the bodyguards beside them; when one is closed, broken
// down or crashed, or there is no way to it, they complain, complain again a while later, and in the end give
// it up for the next. Between stops and after the last they go round as other guests do.
// While in the park they look round now and then: litter on their path and time on a broken or crashed ride
// wear their patience down, a happy mood builds it back, and they tick off what they came to see (different
// exciting coasters, fireworks, a themed area). The visit went well when they leave with patience left and
// everything seen; the goal is then met for good. A visit that went badly is tried again some months on.
//
// Only the goals of the level being played send anyone; the others' dates roll on a month at a time.

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::simulation
{
    /// What holds a VIP up on the way to their next stop.
    enum class VipSnag : u8
    {
        None,
        Closed,
        Broken,
        Crashed,
        NoWay, ///< No walk over the paths reaches it
    };

    /// A guest of honour a scenario's goal sent. They go home only once their visit is over or
    /// their patience has run out.
    struct Vip
    {
        u64 goal = 0;                           ///< The goal's condition, by its id
        u8 who = 0;                             ///< Which celebrity, as the goal numbers them
        units::Share patience { 1 };            ///< Litter and breakdowns wear it down; at 0 they leave unhappy
        std::vector<entt::entity> coasters { }; ///< Different coasters they rode that were exciting enough
        bool fireworks = false;
        bool themed = false;
        f32 came = 0;            ///< Calendar time they came in
        units::Seconds look { }; ///< Until they next look round
        f32 happiness_sum = 0;   ///< Over their looks, for their average
        u32 looks = 0;
        bool leaving = false;
        units::Seconds poll { };             ///< Until they next look for fans about them
        u32 fans = 0;                        ///< Gathered round them for an autograph now
        u32 signatures = 0;                  ///< Autographs given since their last look round
        bool pool = false;                   ///< Swam in a pool
        std::vector<entt::entity> stops { }; ///< What they came to see, in turn: coasters, a pool's changing room
        usize next_stop = 0;                 ///< Of stops, where they go next
        VipSnag snag = VipSnag::None;        ///< What holds them up at it
        units::Seconds snagged { };          ///< How long it has since they last said so
        bool nagged = false;                 ///< Said so again already
    };

    /// One of a VIP's bodyguards, who walk with them in their party.
    struct Bodyguard
    {
        entt::entity vip = entt::null;
    };

    /// Text key of celebrity `who`'s name; empty for a number with none.
    [[nodiscard]] std::string_view vip_name_key(u8 who) noexcept;

    /// How much of what `vip` came to see they have seen, 0..1: each coaster wanted counts once, as do
    /// fireworks and a themed area. With nothing to see, how happy they are.
    [[nodiscard]] f32 vip_opinion(const Vip& vip, const save::model::VipTarget& target, f32 happiness);

    /// What the park round a VIP holds that the simulation does not know itself.
    struct VipSurroundings
    {
        std::function<std::string(u8 who)> name { };                       ///< Shown name of celebrity `who`
        std::function<i32(glm::vec3 at, i32 theme, i32 reach)> themed { }; ///< Scenery of `theme` within `reach` tiles of `at`
    };

    /// Moves the VIP goals of `set` on to calendar `time`, `game_seconds` after the last call: tells the player
    /// of a visit coming, lets the VIP in on the day with their bodyguards, sends them on to their stops, has them
    /// look round, and judges the visit as they leave, posting each to `messages`.
    void advance_vips(SimWorld& world, save::model::ObjectiveSet& set, f32 time, f32 game_seconds, park::ParkMessages& messages, const VipSurroundings& around);

} // namespace rct3::simulation
