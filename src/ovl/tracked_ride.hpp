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

    /// How a kind of tracked ride runs unless the player changes it. Speeds are metres a second.
    struct RideOperatingDefaults
    {
        std::uint32_t mode = 0;          ///< Operating mode, numbered as the save's ride operating mode
        std::uint32_t allowed_modes = 0; ///< Bit (1 << mode) for each mode the player may pick
        float launch_speed = 0;
        float lift_speed = 0;                 ///< Lift chain speed
        float brake_speed = 0;                ///< Speed brakes slow trains to
        float block_brake_speed = 0;          ///< Speed a train leaves a block brake at
        std::optional<float> fixed_speed;     ///< The one speed of rides that never vary it, such as transport rides
        float lift_min = 0, lift_max = 0;     ///< Range the player may set the lift speed in
        float launch_min = 0, launch_max = 0; ///< Range the player may set the launch speed in
        std::int32_t cost = 0;                ///< Base build cost, hundredths
    };

    struct TrackedRide
    {
        /// The fields shared by all attractions: at the start of version 1 items, further on in later ones,
        /// past the list of track sections.
        AttractionInfo info;
        std::string_view name; ///< Name string stored in the item, version 4 and 5 files only
        /// "tks" items for the four special track sections of the ride, such as its lift hill and station.
        /// Empty slots are normal. Version 4 and 5 files only.
        std::array<std::string_view, 4> track_sections;
        /// "spl" items, version 4 and 5 files only. Version 4 files have three.
        std::array<std::string_view, 4> splines;
        /// Nothing when the item is too short to hold them.
        std::optional<RideOperatingDefaults> operating;
    };

    struct TrackedRideItem
    {
        std::string_view name;           ///< Symbol name without the tag
        std::optional<TrackedRide> ride; ///< Nothing when the start of the record does not fit its block
    };

    /// Tracked rides of one part of an overlay: pointer symbols tagged "trr". The start of each item and its
    /// operating defaults are read; the rest of its ride parameters are not interpreted yet. Views in the result
    /// point into the overlay's bytes.
    [[nodiscard]] std::vector<TrackedRideItem> read_tracked_rides(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
