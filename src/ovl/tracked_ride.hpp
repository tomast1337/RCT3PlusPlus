// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tracked rides: the kinds of ride built from track sections, such as a coaster or a log flume.
#pragma once

#include "ovl/attraction.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct TrackedRide
    {
        /// Version 1 files start with the fields shared by all attractions. Later files do not have them in
        /// this item, so `info` is zero and empty there.
        AttractionInfo info;
        std::string_view name; ///< Name string stored in the item, version 4 and 5 files only
        /// "tks" items for the four special track sections of the ride, such as its lift hill and station.
        /// Empty slots are normal. Version 4 and 5 files only.
        std::array<std::string_view, 4> track_sections;
        /// "spl" items, version 4 and 5 files only. Version 4 files have three.
        std::array<std::string_view, 4> splines;
    };

    struct TrackedRideItem
    {
        std::string_view name;           ///< Symbol name without the tag
        std::optional<TrackedRide> ride; ///< Nothing when the start of the record does not fit its block
    };

    /// Tracked rides of one part of an overlay: pointer symbols tagged "trr". Only the start of each item
    /// is read, the rest is a large set of ride parameters that is not interpreted yet. Views in the result
    /// point into the overlay's bytes.
    [[nodiscard]] std::vector<TrackedRideItem> read_tracked_rides(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
