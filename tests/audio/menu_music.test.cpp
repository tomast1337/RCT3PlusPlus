// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/menu_music.hpp"

#include <chrono>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

using namespace rct3::audio;

namespace
{
    std::vector<std::string> all_tracks()
    {
        std::vector<std::string> out;
        for (const auto t : kMenuTracks)
            out.emplace_back(t);
        for (const auto t : kHalloweenMenuTracks)
            out.emplace_back(t);
        return out;
    }
} // namespace

TEST_CASE("cycling plays the next track each run")
{
    const auto files = all_tracks();
    auto pick = pick_menu_music(0, false, files);
    CHECK(pick.track == kMenuTracks[0]);
    CHECK(pick.next_place == 1);
    pick = pick_menu_music(12, false, files);
    CHECK(pick.track == kMenuTracks[12]);
    CHECK(pick.next_place == 0);
}

TEST_CASE("without cycling the quiet track plays and nothing moves on")
{
    const auto pick = pick_menu_music(std::nullopt, true, all_tracks());
    CHECK(pick.track == kQuietMenuTrack);
    CHECK_FALSE(pick.next_place);
    CHECK(pick_menu_music(-1, false, all_tracks()).track == kQuietMenuTrack);
}

TEST_CASE("missing tracks are passed over, in any case of name")
{
    const std::vector<std::string> files { "BUBBLE HOUSE.WMA", "summer air.wma" };
    auto pick = pick_menu_music(0, false, files);
    CHECK(pick.track == kMenuTracks[2]);
    CHECK(pick.file == 0);
    CHECK(pick.next_place == 3);

    pick = pick_menu_music(4, false, std::span(files).last(1));
    CHECK(pick.track == kQuietMenuTrack);
    CHECK(pick.file == 0);
    CHECK(pick.next_place == (4 + kMenuTrackTries) % 13);

    pick = pick_menu_music(4, false, { });
    CHECK(pick.track == kQuietMenuTrack);
    CHECK_FALSE(pick.file); // No music at all
}

TEST_CASE("a place past the list plays the quiet track")
{
    const auto pick = pick_menu_music(40, false, all_tracks());
    CHECK(pick.track == kQuietMenuTrack);
    CHECK(pick.next_place == 41 % 13);
}

TEST_CASE("on 31 October an eerie track plays and the cycle still moves on")
{
    CHECK(pick_menu_music(4, true, all_tracks()).track == kHalloweenMenuTracks[0]);
    const auto pick = pick_menu_music(5, true, all_tracks());
    CHECK(pick.track == kHalloweenMenuTracks[1]);
    CHECK(pick.next_place == 6);

    using namespace std::chrono;
    CHECK(is_halloween(sys_days { year { 2031 } / October / 31 } + hours { 23 }));
    CHECK_FALSE(is_halloween(sys_days { year { 2031 } / October / 30 } + hours { 23 }));
    CHECK_FALSE(is_halloween(sys_days { year { 2031 } / November / 1 }));
}

TEST_CASE("the menus follow the music picked for the park")
{
    const std::vector<std::string> files { "Alpha.wma", "Beta.wma" };
    CHECK(menu_music_wish(false, std::nullopt, files) == MenuMusicWish { });
    CHECK(menu_music_wish(false, std::string("beta.WMA"), files) == MenuMusicWish { MenuMusicSource::Track, 1 });
    CHECK(menu_music_wish(false, std::string(), files).source == MenuMusicSource::Silent);
    CHECK(menu_music_wish(true, std::string("Beta.wma"), files).source == MenuMusicSource::Shuffle);
    // A pick that is gone from the folder leaves it to the cycle.
    CHECK(menu_music_wish(false, std::string("Gone.wma"), files).source == MenuMusicSource::Cycle);
}
