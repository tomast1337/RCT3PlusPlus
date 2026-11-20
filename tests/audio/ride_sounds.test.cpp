// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/ride_sounds.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::audio;

TEST_CASE("a tracked ride's sound set is told by its folder, coasters sounding steel unless known otherwise")
{
    CHECK(track_sound_set("tracks\\coasters\\WoodenRC\\WoodenRC") == TrackSoundSet::Wood);
    CHECK(track_sound_set("tracks/coasters/InvertedRollerCoaster/InvertedRollerCoaster") == TrackSoundSet::Inverted);
    CHECK(track_sound_set("tracks\\coasters\\Giga\\Giga") == TrackSoundSet::Steel);
    CHECK(track_sound_set("tracks\\TrackedRides\\LogFlume\\LogFlume") == TrackSoundSet::Water);
    CHECK(track_sound_set("tracks\\TrackedRides\\Kart\\Kart") == TrackSoundSet::GoKart);
    CHECK(track_sound_set("tracks\\TrackedRides\\SuspendedMono\\SuspendedMono") == TrackSoundSet::None);
    CHECK(track_sound_set("tracks\\TrackedRides\\Ghost\\Ghost") == TrackSoundSet::None);
    CHECK(track_sound_set("") == TrackSoundSet::None);
}

TEST_CASE("each set and sound has its overlay name")
{
    CHECK(file_of(TrackSoundSet::None).empty());
    CHECK(file_of(TrackSoundSet::ElephantTransport) == "ElephantTransport");
    CHECK(name_of(TrackSound::Slow) == "Slow");
    CHECK(name_of(TrackSound::WaterPlume) == "WaterPlume");
}

TEST_CASE("a train's running loops cross from slow to fast with its speed, silent when it stands")
{
    const RunningMix still = running_mix(0, { .med = true, .fast = true });
    CHECK(still.slow + still.med + still.fast == 0);

    const RunningMix crawl = running_mix(3, { .med = true, .fast = true });
    CHECK(crawl.slow > 0);
    CHECK(crawl.med == 0);
    CHECK(crawl.fast == 0);

    const RunningMix fast = running_mix(40, { .med = true, .fast = true });
    CHECK(fast.slow == 0);
    CHECK(fast.med == 0);
    CHECK(fast.fast == doctest::Approx(1));

    const RunningMix middling = running_mix(14, { .med = true, .fast = true });
    CHECK(middling.slow + middling.med + middling.fast == doctest::Approx(1));
    CHECK(running_mix(-14, { .med = true, .fast = true }).med == doctest::Approx(middling.med)); // Backwards as forwards

    SUBCASE("a set lacking loops plays the next slower in their place")
    {
        const RunningMix no_fast = running_mix(40, { .med = true });
        CHECK(no_fast.fast == 0);
        CHECK(no_fast.med == doctest::Approx(1));
        const RunningMix slow_only = running_mix(40, { });
        CHECK(slow_only.slow == doctest::Approx(1));
    }
}

TEST_CASE("a ride's music is full near its entrance and falls off by the square of the way left")
{
    CHECK(ride_music_level(rct3::units::Distance { 0 }) == 1);
    CHECK(ride_music_level(kMusicNear) == 1);
    const rct3::units::Distance halfway { (kMusicNear.value + kMusicFar.value) / 2 };
    CHECK(ride_music_level(halfway) == doctest::Approx(0.25f));
    CHECK(ride_music_level(kMusicFar) == 0);
    CHECK(ride_music_level(rct3::units::Distance { kMusicFar.value * 2 }) == 0);
}

TEST_CASE("a ride's tune is kept as its music file and listed by its place among the tunes")
{
    const std::vector<std::u16string> tunes { u"Calm Lake", u"Tea Party" };
    CHECK(tune_file(2, tunes) == "Tea Party.wma");
    CHECK(tune_file(0, tunes).empty());
    CHECK(tune_file(3, tunes).empty());
    CHECK(tune_of("Tea Party.wma", tunes) == 2);
    CHECK(tune_of("\\calm lake.WMA", tunes) == 1);
    CHECK(tune_of("", tunes) == 0);
    CHECK(tune_of("Marching Band.wma", tunes) == 0);
}
