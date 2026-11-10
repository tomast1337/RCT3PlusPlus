// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride cars: one kind of vehicle of a tracked ride, such as a coaster car or a log. Ride trains put cars
// together.
#pragma once

#include "ovl/item_record.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// How a pool slide's car rides its open trough: swung up the trough's wall in a turn, it may go over the rim.
    struct TroughRide
    {
        float damping = 0;  ///< Per second, how fast its swing dies down
        float centring = 0; ///< Per second, how fast it is drawn back to the trough's middle
        float friction = 0; ///< Of the car on the wall, held back as it swings by this much of the weight pressing it there
    };

    struct RideCar
    {
        std::string_view name;         ///< Name string stored in the item. Empty in some version 4 and 5 files.
        std::string_view display_name; ///< English name string stored next to it
        /// How riders sit in the car, in its low byte, the same for cars of one kind of ride: as the cars'
        /// shapes show, 0 in stand-up coaster cars, 3 in lie-down ones, 4 on bikes, 11 behind a lap bar, 13
        /// with feet dangling under inverted cars (see park::rider_motions). 35 leaves it to the ride. Higher
        /// bits are set in some later cars; their meaning is unconfirmed.
        std::uint32_t kind = 0;
        std::string_view visual; ///< "svd" item that draws the car
        float mass = 0;          ///< Kilograms, empty: 40 a body slide's mat, 300 a raft blasted up by jets
        /// "svd" item of the part of the car that spins or swings on it, such as a spinning tub or a hanging
        /// seat; empty when the car is one piece. The two line up where each has a bone named "pivot".
        std::string_view part;
        /// "svd" items of the wheels: front right, front left, rear right, rear left; empty where it has none.
        std::array<std::string_view, 4> wheels;
        /// By wheel, whether its piece is drawn turned half round about the car's up, as where one side's wheel
        /// piece also serves the other side (the word after each wheel's reference is not 0).
        std::array<bool, 4> wheels_turned { };
        /// "svd" items of the front and rear axles, which may carry the wheels; empty where it has none.
        std::array<std::string_view, 2> axles;
        /// Of the visual's animations, by number, the ones a rowing boat rows by: straight on with both oars,
        /// turning to its left and turning to its right (as the car's model has its +X to its right). -1 where
        /// it has none, as in every car but rowing boats.
        std::array<std::int32_t, 3> rowing { -1, -1, -1 };
        /// Every reference in the record, in file order. Besides the visual it holds further "svd" items for
        /// parts of the car, such as seats and wheels.
        std::vector<ItemReference> references;
        /// The whole record: 240 bytes in version 1 files, 292 in version 4 and 336 in version 5. Starting at
        /// byte 16 it is mostly single-precision numbers and -1 markers. Their meanings are unconfirmed.
        std::span<const std::byte> record;
        /// Of a car that rides an open trough and can fly out of it (bit 0x200 of the word at byte 272, set in the
        /// pool slides' cars), its swing in it; nothing for every other car.
        std::optional<TroughRide> trough;
    };

    struct RideCarItem
    {
        std::string_view name;      ///< Symbol name without the tag
        std::optional<RideCar> car; ///< Nothing when the record does not fit its block
    };

    /// Ride cars of one part of an overlay: pointer symbols tagged "ric". Views in the result point into the
    /// overlay's bytes.
    [[nodiscard]] std::vector<RideCarItem> read_ride_cars(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
