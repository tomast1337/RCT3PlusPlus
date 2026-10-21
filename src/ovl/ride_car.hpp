// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride cars: one kind of vehicle of a tracked ride, such as a coaster car or a log. Ride trains put cars
// together.
#pragma once

#include "ovl/item_record.hpp"
#include "ovl/overlay.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct RideCar
    {
        std::string_view name;         ///< Name string stored in the item. Empty in some version 4 and 5 files.
        std::string_view display_name; ///< English name string stored next to it
        /// Small number, 0 to 27, that stays equal for cars of one kind of ride. Meaning unconfirmed.
        std::uint32_t kind = 0;
        std::string_view visual; ///< "svd" item that draws the car
        /// "svd" item of the part of the car that spins or swings on it, such as a spinning tub or a hanging
        /// seat; empty when the car is one piece. The two line up where each has a bone named "pivot".
        std::string_view part;
        /// Every reference in the record, in file order. Besides the visual it holds further "svd" items for
        /// parts of the car, such as seats and wheels.
        std::vector<ItemReference> references;
        /// The whole record: 240 bytes in version 1 files, 292 in version 4 and 336 in version 5. Starting at
        /// byte 16 it is mostly single-precision numbers and -1 markers. Their meanings are unconfirmed.
        std::span<const std::byte> record;
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
