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
        /// Names of the "rit" trains the ride may run, the one it starts with first: a count at 0x08 and a
        /// pointer at 0x0C to as many string pointers.
        std::vector<std::string_view> trains;
        /// The station platform type, string at 0x38: an overlay of that name in a folder of tracks/Platforms.
        std::string_view platform;
    };

    /// Which piece a piece pattern entry takes, and where. Kinds: 0 any piece of the group `name`
    /// (an empty name meaning pieces of no group), 1 the piece `name`, 2 any piece.
    struct PieceTest
    {
        std::uint8_t kind = 2;
        std::string_view name; ///< A group, or a "tks" item
        bool negate = false;   ///< Takes the pieces the kind and name do not
        bool forward = false;  ///< Only a piece laid as drawn
        bool backward = false; ///< Only a piece laid backwards
        bool first = false;    ///< Only the first piece of its track
        bool last = false;     ///< Only the last piece of its track
    };

    /// One piece of a piece pattern: which piece it takes and which it becomes. A `becomes` of kind 2
    /// leaves the piece as it is; one of kind 1 swaps it for that piece, laid as `backward` says.
    struct PatternEntry
    {
        PieceTest when;
        PieceTest becomes;
    };

    /// What building a ride's track must keep to. Version 4 and 5 files only; version 1 rides take these
    /// defaults.
    struct TrackRules
    {
        /// Bits: 0x10 and 0x4000 the ride is never tested, 0x200 its track may be in several pieces.
        std::uint32_t flags = 0;
        std::optional<std::uint32_t> min_pieces; ///< Fewest pieces it opens with
        bool circuit_only = false;               ///< Opens only as a closed circuit
        /// How many entrances and exits it needs: 0 one per station, 1 any number, 2 exactly one, 3 at
        /// least one, 4 none.
        std::uint32_t entrances = 3;
        std::uint32_t exits = 3;
        /// Runs of pieces swapped for others after every change to the track, so a station's ends or a
        /// waterfall's parts are the right pieces: each pattern takes consecutive pieces, one per entry.
        std::vector<std::vector<PatternEntry>> patterns;
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
        /// Guests the ride makes room for in the park while it is open and working.
        std::optional<std::uint32_t> guests;
        TrackRules rules;
    };

    struct TrackedRideItem
    {
        std::string_view name;           ///< Symbol name without the tag
        std::optional<TrackedRide> ride; ///< Nothing when the start of the record does not fit its block
    };

    /// Tracked rides of one part of an overlay: pointer symbols tagged "trr". The start of each item, its
    /// operating defaults, the guests it makes room for and its track rules are read; the rest of its ride
    /// parameters are not interpreted yet. Views in the result
    /// point into the overlay's bytes.
    [[nodiscard]] std::vector<TrackedRideItem> read_tracked_rides(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
