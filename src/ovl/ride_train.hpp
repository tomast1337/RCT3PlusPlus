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

    struct RideTrain
    {
        std::string_view name;         ///< Name string stored in the item. Empty in version 4 and 5 files.
        std::string_view display_name; ///< English name string stored next to it
        /// "ric" items, in file order, up to six. The first is the car the train starts with, unused slots are
        /// empty, and a slot may repeat the car before it.
        std::array<std::string_view, 6> cars;
        /// Every reference in the fixed part of the record, in file order.
        std::vector<ItemReference> references;
        /// The fixed part of the record, 184 bytes. From byte 32 it is single-precision numbers, whose
        /// meanings are unconfirmed. More data follows it in the file: pointers to arrays of per-car data and
        /// texture references, which are not interpreted.
        std::span<const std::byte> record;
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
