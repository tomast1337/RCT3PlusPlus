// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride trains: which cars a train of a tracked ride is made of and how they are spaced.
#pragma once

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
        float look_ahead = 0;   ///< How far along the track the rider looks; none when 0
        float sway_per_g = 0;   ///< The head's sway against the car's pull, per g of it
        float sway_most = 0;    ///< And the most it sways
        float sway_settle = 0;  ///< The time the sway takes to follow the pull
        float shake_from = 0;   ///< The speed in metres a second the view starts shaking at
        float shake = 0;        ///< Its shake per (speed over that)², in radians
        float shake_across = 0; ///< The most it shakes side to side
        float shake_up = 0;     ///< And up and down
        float chain_shake = 0;  ///< The same three on a lift chain
        float chain_shake_across = 0;
        float chain_shake_up = 0;
        float turn_settle = 0; ///< The time the view takes to follow the car's turns
        float look_tilt = 0;   ///< How far looking ahead into a turn leans the view into it, 0 to 1
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
        std::uint32_t fewest_cars = 0;
        std::uint32_t most_cars = 0;
        std::uint32_t usual_cars = 0;
        /// The fixed part of the record, 184 bytes. Bytes 32 to 43 are the car counts above; from byte 44 it is
        /// single-precision numbers, the ride camera's among them, whose meanings are unconfirmed. More data follows it in the file: pointers to arrays of per-car data and
        /// texture references, which are not interpreted.
        std::span<const std::byte> record;
        RideCameraTuning camera; ///< Thirteen numbers from byte 56 of the record
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
