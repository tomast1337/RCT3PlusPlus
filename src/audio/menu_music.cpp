// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/menu_music.hpp"

#include <algorithm>
#include <cctype>

namespace rct3::audio
{

    namespace
    {

        bool same_name(std::string_view a, std::string_view b)
        {
            return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
        }

        std::optional<std::size_t> find(std::string_view track, std::span<const std::string> files)
        {
            const auto it = std::ranges::find_if(files, [&](const std::string& f) { return same_name(f, track); });
            if (it == files.end())
                return std::nullopt;
            return static_cast<std::size_t>(it - files.begin());
        }

        std::string_view track_at(int place, bool halloween)
        {
            if (halloween)
                return kHalloweenMenuTracks.at(static_cast<std::size_t>(place % 2));
            if (place < static_cast<int>(kMenuTracks.size()))
                return kMenuTracks.at(static_cast<std::size_t>(place));
            return kQuietMenuTrack;
        }

    } // namespace

    MenuMusicPick pick_menu_music(std::optional<int> place, bool halloween, std::span<const std::string> files)
    {
        if (!place || *place < 0)
            return { kQuietMenuTrack, find(kQuietMenuTrack, files), std::nullopt };
        const int count = static_cast<int>(kMenuTracks.size());
        int at = *place;
        for (int tries = 0; tries < kMenuTrackTries; ++tries)
        {
            const std::string_view track = track_at(at, halloween);
            at = (at + 1) % count;
            if (const auto file = find(track, files))
                return { track, file, at };
        }
        return { kQuietMenuTrack, find(kQuietMenuTrack, files), at };
    }

    MenuMusicWish menu_music_wish(bool shuffle, const std::optional<std::string>& picked, std::span<const std::string> files)
    {
        if (shuffle)
            return { MenuMusicSource::Shuffle, 0 };
        if (!picked)
            return { };
        if (picked->empty())
            return { MenuMusicSource::Silent, 0 };
        if (const auto file = find(*picked, files))
            return { MenuMusicSource::Track, *file };
        return { };
    }

    bool is_halloween(std::chrono::system_clock::time_point when)
    {
        const std::chrono::year_month_day day { std::chrono::floor<std::chrono::days>(when) };
        return day.month() == std::chrono::October && day.day() == std::chrono::day { 31 };
    }

} // namespace rct3::audio
