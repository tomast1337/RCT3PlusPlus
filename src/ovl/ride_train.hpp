// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride trains: which cars a train of a tracked ride is made of and how they are spaced.
#pragma once

#include "core/types.hpp"
#include "ovl/item_record.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// How the view riding along in one of a train's cars moves. Lengths in metres, times in seconds. Every
    /// train the game ships sets them; most share one set (looking 8 m ahead, a head sway of 0.5 m per g up to
    /// 0.4 m), spinning cars look nowhere ahead, and a few cars such as karts look less far.
    struct RideCameraTuning
    {
        f32 look_ahead = 0;   ///< How far along the track the rider looks; none when 0
        f32 sway_per_g = 0;   ///< The head's sway against the car's pull, per g of it
        f32 sway_most = 0;    ///< And the most it sways
        f32 sway_settle = 0;  ///< The time the sway takes to follow the pull
        f32 shake_from = 0;   ///< The speed in metres a second the view starts shaking at
        f32 shake = 0;        ///< Its shake per (speed over that)², in radians
        f32 shake_across = 0; ///< The most it shakes side to side
        f32 shake_up = 0;     ///< And up and down
        f32 chain_shake = 0;  ///< The same three on a lift chain
        f32 chain_shake_across = 0;
        f32 chain_shake_up = 0;
        f32 turn_settle = 0; ///< The time the view takes to follow the car's turns
        f32 look_tilt = 0;   ///< How far looking ahead into a turn leans the view into it, 0 to 1
    };

    /// How a boat of this train handles off its track, out on open water. Turns in radians, speeds in metres a
    /// second. Most boats the game ships share one set (turning at most 1 a second, leaning at most 0.2); jet skis
    /// turn and lean harder, bumper boats turn quicker and never lean.
    struct BoatHandling
    {
        f32 turn_most = 0;  ///< Radians a second it turns at most
        f32 turn_gain = 0;  ///< Radians a second it turns per radian its mark lies off its bow
        f32 speed_up = 0;   ///< m/s² it speeds up by at most; it slows by up to twice that
        f32 speed_gain = 0; ///< m/s² it speeds up by per m/s short of the speed it wants
        f32 turn_slow = 0;  ///< m/s² it slows by per radian its mark lies off its bow
        f32 brake = 0;      ///< m/s² it slows by near the end of its way and per stretch of water ahead it cannot go on
        f32 lean_gain = 0;  ///< Radians it leans per radian a second it turns
        f32 lean_most = 0;  ///< And the most it leans
    };

    struct RideTrain
    {
        std::string_view name;         ///< Name string stored in the item. Empty in version 4 and 5 files.
        std::string_view display_name; ///< English name string stored next to it
        /// "ric" items, in file order, up to six. The first is the car the train starts with, unused slots are
        /// empty, and a slot may repeat the car before it.
        std::array<std::string_view, 6> cars;
        /// Every reference in the fixed part of the record, in file order.
        std::vector<ItemReference> references;
        /// Passenger cars a train of this kind may have, fewest and most, and how many it has unless set
        /// otherwise. Every train the game ships has fewest <= usual <= most.
        u32 fewest_cars = 0;
        u32 most_cars = 0;
        u32 usual_cars = 0;
        /// What holds it back: rolling, as this much of its weight on the track; standing still, up to this much
        /// of it; and the air, as this much of its speed squared, in newtons. Bytes 44, 48 and 52 of the record;
        /// most coaster trains have 0.04, 0.06 and 0.02, every pool slide's 0.06, 0.04 and 0.005.
        f32 rolling = 0;
        f32 resting = 0;
        f32 air = 0;
        /// The fixed part of the record, 184 bytes. Bytes 32 to 43 are the car counts above; from byte 44 it is
        /// single-precision numbers, the ride camera's among them, whose meanings are unconfirmed. More data follows it in the file: pointers to arrays of per-car data and
        /// texture references, which are not interpreted.
        std::span<const std::byte> record;
        RideCameraTuning camera; ///< Thirteen numbers from byte 56 of the record
        BoatHandling boat;       ///< Eight numbers from byte 108, right after the camera's
        /// The station platform type its ride's stations are built with, right after the fixed part; empty when
        /// the ride's own type applies.
        std::string_view platform;
    };

    struct RideTrainItem
    {
        std::string_view name;          ///< Symbol name without the tag
        std::optional<RideTrain> train; ///< Nothing when the record does not fit its block
    };

    /// Ride trains of one part of an overlay: pointer symbols tagged "rit". Views in the result point into
    /// the overlay's bytes.
    [[nodiscard]] std::vector<RideTrainItem> read_ride_trains(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
