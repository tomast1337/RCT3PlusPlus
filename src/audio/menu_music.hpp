// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which of the game's music files plays behind the menus. One track a run, looped: while the player lets the
// menu music cycle, each start plays the next track of a fixed list, passing over files that are missing
// (the rest of the run keeps the track it started with). The place in the list is kept in the options. On
// 31 October one of two eerie tracks plays instead, still moving the place on. With cycling off, or when
// nothing of the list is found, one quiet track plays every time.
//
// Unlike the game, the menus follow the music the player picked for the park, as soon as it is picked: a
// track of the music folder plays that track, shuffling plays one at random, and no music leaves the menus
// quiet. Only while nothing is picked does the cycle above choose.
//
// The menu music plays at half the music volume.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rct3::audio
{

    /// The cycle's tracks, in the game's music folder, in order.
    inline constexpr std::array<std::string_view, 13> kMenuTracks = {
        "Tribal Wild.wma", "Bush Baby.wma", "Bubble House.wma", "Summer Air.wma", "Lush Canopy.wma", "Galaxy Trooper.wma", "Mermaid.wma", "Savannah.wma", "Buccaneer.wma", "Flat Head Four.wma", "JJ's House.wma", "Prairie Rider.wma", "Rock Like A Dude.wma",
    };
    /// Without cycling, for a place past the list, and when none of the tried tracks is there.
    inline constexpr std::string_view kQuietMenuTrack = "Summer Air.wma";
    /// For 31 October, at even and odd places.
    inline constexpr std::array<std::string_view, 2> kHalloweenMenuTracks = { "Haunted House.wma", "Gothic Moon.wma" };
    /// Tracks tried before giving up for the quiet one.
    inline constexpr int kMenuTrackTries = 10;

    inline constexpr float kMenuMusicLevel = 0.5f; ///< Of the music volume

    struct MenuMusicPick
    {
        std::string_view track;
        std::optional<std::size_t> file; ///< Of the files given, the track's; nothing when it is missing too
        std::optional<int> next_place;   ///< Where the cycle carries on next run; nothing without cycling
    };

    /// Where the menu's music comes from.
    enum class MenuMusicSource : std::uint8_t
    {
        Cycle,   ///< The run's track of the cycle
        Track,   ///< The track the player picked
        Shuffle, ///< A track at random
        Silent,
    };

    struct MenuMusicWish
    {
        MenuMusicSource source = MenuMusicSource::Cycle;
        std::size_t file = 0; ///< Of the files given, for a picked track
        friend bool operator==(const MenuMusicWish&, const MenuMusicWish&) = default;
    };

    /// What the menus play for the player's pick of park music: `shuffle` on, else `picked` (empty for no
    /// music, nothing when never picked). A picked track that is not among `files` leaves it to the cycle.
    [[nodiscard]] MenuMusicWish menu_music_wish(bool shuffle, const std::optional<std::string>& picked, std::span<const std::string> files);

    /// Picks this run's track. `place` is where the cycle stands, nothing when it is off; `files` are the
    /// names in the music folder, matched in any case.
    [[nodiscard]] MenuMusicPick pick_menu_music(std::optional<int> place, bool halloween, std::span<const std::string> files);

    /// Whether `when` falls on 31 October, by the UTC calendar.
    [[nodiscard]] bool is_halloween(std::chrono::system_clock::time_point when);

} // namespace rct3::audio
