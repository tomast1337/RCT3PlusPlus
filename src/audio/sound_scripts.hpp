// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a flat ride is heard: the sound scripts its scenery item gives each of its animations (see
// ovl/scenery_item.hpp), and the screams of its riders.
//
// A sound entry of the item has a script for each animation. When the ride's animation changes, or the one
// playing comes round again, the new animation's script runs from its start: at the times it names it starts one
// of the entry's sounds, stops the last one started, or glides the entry's volume or pitch to a level by a time,
// in a straight line. The volume and pitch carry over from one animation to the next, so a motor started as the
// ride starts can run on through its running animation and die away in its stopping one.
//
// Riders scream while the ride runs, if it is wild enough: a few voices at once, each a lone rider's scream or a
// group's, men's or women's as many of each ride.
#pragma once

#include "ovl/scenery_item.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rct3::audio
{

    /// A level that runs in a straight line from `from` at `start` to `to` at `until`, and stays there.
    struct Glide
    {
        float from = 1;
        float to = 1;
        units::Seconds start { };
        units::Seconds until { };
        [[nodiscard]] float at(units::Seconds now) const;
    };

    /// How far one sound entry's script has run, and the entry's volume and pitch.
    struct ScriptPlay
    {
        std::size_t next_step = 0;
        Glide volume { };
        Glide pitch { };
    };

    /// Runs the steps of `script` due by `now` (seconds into its animation) up to the next that starts or stops a
    /// sound, and returns that one's cue; nothing once no more are due. Volume and pitch steps set `play`'s glides.
    [[nodiscard]] std::optional<ovl::SoundCue> next_cue(std::span<const ovl::SoundStep> script, ScriptPlay& play, units::Seconds now);

    /// Starts the script over for a new animation, the volume and pitch held where they were at `was` into the old.
    void restart_script(ScriptPlay& play, units::Seconds was);

    inline constexpr units::Share kScreamingIntensity { 0.06f }; ///< Chosen by ear: gentler rides, a carousel, are ridden quietly
    inline constexpr std::size_t kScreamVoices = 3;              ///< Chosen by ear: a few at a time sound like a crowd, more like a wall

    /// Which of the screams a voice plays, in the order of ParkSounds::screams.
    enum class Screamers : std::uint8_t
    {
        Man,
        Woman,
        Men,
        Women,
    };

    /// The riders of a ride who are screaming, by sex.
    struct ScreamingRiders
    {
        std::size_t men = 0;
        std::size_t women = 0;
    };

    /// Who a new voice is: the men or the women, picked by `roll` (0..1) as many of each scream; a group of them
    /// when more than one, after which one of them is left to scream on. Some must be screaming.
    [[nodiscard]] Screamers pick_screamers(ScreamingRiders& riders, float roll);

} // namespace rct3::audio
