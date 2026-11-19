// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_scripts.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::audio;
using rct3::ovl::SoundCue;
using rct3::ovl::SoundStep;
using rct3::units::Seconds;

namespace
{
    // A motor: starts at 0.5 s, swells from where it is to 0.8 by 2.5 s, rises in pitch at once, stops at 4 s.
    constexpr std::array kMotor {
        SoundStep { .at = 0.5f, .cue = SoundCue::Start },
        SoundStep { .at = 0.5f, .cue = SoundCue::Volume, .value = 0.8f, .until = 2.5f },
        SoundStep { .at = 1.0f, .cue = SoundCue::Pitch, .value = 1.5f, .until = 0.0f },
        SoundStep { .at = 4.0f, .cue = SoundCue::Stop },
    };
} // namespace

TEST_CASE("a sound script starts and stops its sounds when they are due, each once")
{
    ScriptPlay play { .volume = { .from = 0.2f, .to = 0.2f } };
    CHECK_FALSE(next_cue(kMotor, play, Seconds { 0.4f }).has_value());
    CHECK(next_cue(kMotor, play, Seconds { 0.6f }) == SoundCue::Start);
    CHECK_FALSE(next_cue(kMotor, play, Seconds { 0.6f }).has_value());
    CHECK_FALSE(next_cue(kMotor, play, Seconds { 3.0f }).has_value());
    // A long gap runs every step due in it.
    CHECK(next_cue(kMotor, play, Seconds { 9.0f }) == SoundCue::Stop);
    CHECK_FALSE(next_cue(kMotor, play, Seconds { 9.0f }).has_value());
}

TEST_CASE("a script glides its volume in a straight line from where it was, and jumps when the time is past")
{
    ScriptPlay play { .volume = { .from = 0.2f, .to = 0.2f } };
    (void)next_cue(kMotor, play, Seconds { 0.6f });
    (void)next_cue(kMotor, play, Seconds { 0.6f });
    CHECK(play.volume.at(Seconds { 0.5f }) == doctest::Approx(0.2f));
    CHECK(play.volume.at(Seconds { 1.5f }) == doctest::Approx(0.5f));
    CHECK(play.volume.at(Seconds { 2.5f }) == doctest::Approx(0.8f));
    CHECK(play.volume.at(Seconds { 30.0f }) == doctest::Approx(0.8f));
    (void)next_cue(kMotor, play, Seconds { 1.0f });
    CHECK(play.pitch.at(Seconds { 1.0f }) == doctest::Approx(1.5f));
}

TEST_CASE("a new animation's script starts over, keeping the volume and pitch reached")
{
    ScriptPlay play { };
    while (next_cue(kMotor, play, Seconds { 9.0f }))
    {
    }
    restart_script(play, Seconds { 1.5f });
    CHECK(play.next_step == 0);
    CHECK(play.volume.at(Seconds { 0.0f }) == doctest::Approx(0.9f));
    CHECK(play.pitch.at(Seconds { 100.0f }) == doctest::Approx(1.5f));
    CHECK(next_cue(kMotor, play, Seconds { 0.5f }) == SoundCue::Start);
}

TEST_CASE("a silent script does nothing")
{
    ScriptPlay play { };
    CHECK_FALSE(next_cue({ }, play, Seconds { 10.0f }).has_value());
    CHECK(play.volume.at(Seconds { 10.0f }) == 1.0f);
}

TEST_CASE("screamers are picked as many of each, a group thinning to one")
{
    ScreamingRiders lone { .men = 1, .women = 0 };
    CHECK(pick_screamers(lone, 0.99f) == Screamers::Man);
    ScreamingRiders women { .men = 0, .women = 4 };
    CHECK(pick_screamers(women, 0.0f) == Screamers::Women);
    CHECK(women.women == 1);
    CHECK(pick_screamers(women, 0.0f) == Screamers::Woman);
    // Three men to one woman: a low roll picks the men, a high one the woman.
    ScreamingRiders mixed { .men = 3, .women = 1 };
    CHECK(pick_screamers(mixed, 0.1f) == Screamers::Men);
    ScreamingRiders mixed_again { .men = 3, .women = 1 };
    CHECK(pick_screamers(mixed_again, 0.9f) == Screamers::Woman);
}
