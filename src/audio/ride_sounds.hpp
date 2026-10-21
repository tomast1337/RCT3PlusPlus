// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a ride's trains sound, heard from the camera: our own rules, not the original game's.
//
// The game keeps its tracked rides' sounds in sets, one overlay each under tracks/TrackSounds, shared by rides of
// a kind: steel coasters, wooden ones, water rides and so on. A set holds a running loop for each of three
// speeds, one for the lift chain, and a sound for a train setting off and one for it braking in; a set may lack
// any of them. A train's running loops cross over with its speed, slow to fast, and grow louder the faster it
// goes, silent when it stands. The chain loop plays while any of the train is on a lift.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rct3::audio
{

    enum class TrackSoundSet : std::uint8_t
    {
        None, ///< Silent
        Steel,
        Wood,
        Inverted,
        Water,
        GoKart,
        HopOnTram,
        AirBoat,
        PaddleSteamer,
        SafariTransport,
        ElephantTransport,
    };
    inline constexpr std::size_t kTrackSoundSets = 11;

    /// The sounds of a set, as the set's overlay names them.
    enum class TrackSound : std::uint8_t
    {
        Slow,
        Med,
        Fast,
        Chain,
        Start,
        Stop,
        // Water: not played yet. A splash as a car enters water, louder the faster, then the plume loop for a
        // few seconds; waits on the track model knowing which pieces are water.
        WaterSplash,
        WaterPlume,
    };
    inline constexpr std::size_t kTrackSounds = 8;

    inline constexpr float kRideNear = 15;         ///< World units: a train's sounds are full loud this close
    inline constexpr float kRideFar = 180;         ///< And gone this far
    inline constexpr std::size_t kHeardTrains = 4; ///< Nearest trains whose loops play, to spare voices

    /// The overlay name of `set`, as in tracks/TrackSounds/<name>.common.ovl; empty for None.
    [[nodiscard]] std::string_view file_of(TrackSoundSet set);
    /// The item name of `sound` in a set's overlay.
    [[nodiscard]] std::string_view name_of(TrackSound sound);

    /// The set a tracked ride sounds like, by the overlay it comes from (as a save keeps it, such as
    /// "tracks/coasters/WoodenRC/WoodenRC"). Coasters not known to be otherwise sound steel; other rides
    /// are silent unless known.
    [[nodiscard]] TrackSoundSet track_sound_set(std::string_view overlay);

    /// How loud each running loop of a train going at `speed` (world units a second) is, 0..1, with the
    /// game volume left out. A set with no fast loop plays its medium one in its place, and one with no medium
    /// either its slow one.
    struct RunningMix
    {
        float slow = 0, med = 0, fast = 0;
    };
    [[nodiscard]] RunningMix running_mix(float speed, bool has_med, bool has_fast);

} // namespace rct3::audio
