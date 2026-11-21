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

#include "core/types.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::audio
{

    enum class TrackSoundSet : u8
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
    inline constexpr usize kTrackSoundSets = 11;

    /// The sounds of a set, as the set's overlay names them.
    enum class TrackSound : u8
    {
        Slow,
        Med,
        Fast,
        Chain,
        Start,
        Stop,
        // Water: a splash as a car splashes through water, louder the faster, then the plume loop for a few
        // seconds.
        WaterSplash,
        WaterPlume,
    };
    inline constexpr usize kTrackSounds = 8;

    inline constexpr units::Distance kRideNear { 15 }; ///< A train's sounds are full loud this close
    inline constexpr units::Distance kRideFar { 180 }; ///< And gone this far
    inline constexpr usize kHeardTrains = 4;           ///< Nearest trains whose loops play, to spare voices

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
        f32 slow = 0, med = 0, fast = 0;
    };
    /// Which running loops beside the slow one a set has.
    struct RunningLoops
    {
        bool med = false;
        bool fast = false;
    };
    [[nodiscard]] RunningMix running_mix(f32 speed, RunningLoops loops);

    inline constexpr units::Distance kMusicNear { 15 }; ///< Chosen by ear: a ride's music is full loud this close to its entrance
    inline constexpr units::Distance kMusicFar { 90 };  ///< And gone this far, so a path between rides hears the nearer
    inline constexpr usize kHeardTracks = 3;            ///< Chosen by ear: the loudest tracks played at once, more is noise

    /// How loud a ride's music is heard `away` from it, 0..1: full near, then falling off steeply at first and ever
    /// more gently to nothing far, as the square of the share of the way still to go.
    [[nodiscard]] f32 ride_music_level(units::Distance away);

    /// A ride keeps its tune as a file of the game's music folder; its window lists the folder's tunes by name.
    inline constexpr std::string_view kTuneExtension = ".wma";

    /// The place among `tunes` (the music folder's tunes by name) of `file`, a ride's tune as kept, from 1; 0 for
    /// none or one not there. A folder before the name is passed over, and case does not count.
    [[nodiscard]] usize tune_of(std::string_view file, std::span<const std::u16string> tunes);
    /// The file a ride keeps for tune `tune` of `tunes`, from 1; empty for 0, none.
    [[nodiscard]] std::string tune_file(usize tune, std::span<const std::u16string> tunes);

    /// Puts `tunes` (the music folder's tunes by name) in the order a ride's window lists them: style by style, as
    /// `styles` gives them (the music folder's list of lines `\\File.wma*Style*y`, `#` starting a comment), then by
    /// name, those it gives no style last. Returns what the window shows for each: "Style - Name", or the name alone.
    [[nodiscard]] std::vector<std::u16string> order_by_style(std::vector<std::u16string>& tunes, std::string_view styles);

} // namespace rct3::audio
