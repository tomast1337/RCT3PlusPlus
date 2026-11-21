// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/ride_sounds.hpp"

#include "core/types.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <ranges>
#include <tuple>
#include <utility>

namespace rct3::audio
{

    namespace
    {

        // Speed bands, world units a second: the slow loop alone up to kSlowOnly, gone by kSlowGone; the
        // fast loop from kFastFrom, alone by kFastOnly; the medium one fills between. Full loud from kLoud.
        constexpr f32 kSlowOnly = 6;
        constexpr f32 kSlowGone = 16;
        constexpr f32 kFastFrom = 12;
        constexpr f32 kFastOnly = 24;
        constexpr f32 kLoud = 14;

        bool contains_ignoring_case(std::string_view text, std::string_view part)
        {
            return !std::ranges::search(text, part, [](char x, char y) { return strings::ascii_lower(x) == strings::ascii_lower(y); }).empty();
        }

        // By a word in the ride's folder name; the first match wins, so the narrower words come first.
        // shortcut: the ride's data does not say which set it uses (none found yet), so it is told by its
        // folder's name; upgrade when the link from a ride to its set is found in its files.
        constexpr std::array<std::pair<std::string_view, TrackSoundSet>, 17> kByFolder { {
            { "Mono", TrackSoundSet::None }, // Monorails glide
            { "ElephantTransport", TrackSoundSet::ElephantTransport },
            { "SafariT", TrackSoundSet::SafariTransport },
            { "PaddleSteamer", TrackSoundSet::PaddleSteamer },
            { "AirBoat", TrackSoundSet::AirBoat },
            { "Tram", TrackSoundSet::HopOnTram },
            { "Kart", TrackSoundSet::GoKart },
            { "Wooden", TrackSoundSet::Wood },
            { "Invert", TrackSoundSet::Inverted },
            { "Suspended", TrackSoundSet::Inverted },
            { "Water", TrackSoundSet::Water },
            { "Flume", TrackSoundSet::Water },
            { "Soaker", TrackSoundSet::Water },
            { "Dingy", TrackSoundSet::Water },
            { "Rapids", TrackSoundSet::Water },
            { "SplashBoats", TrackSoundSet::Water },
            { "Rafts", TrackSoundSet::Water },
        } };

        f32 ramp(f32 x, f32 from, f32 to)
        {
            return std::clamp((x - from) / (to - from), 0.0f, 1.0f);
        }

    } // namespace

    std::string_view file_of(TrackSoundSet set)
    {
        constexpr std::array<std::string_view, kTrackSoundSets> kFiles { "", "Steel", "Wood", "Inverted", "Water", "GoKart", "HopOnTram", "AirBoat", "PaddleSteamer", "SafariTransport", "ElephantTransport" };
        return kFiles[static_cast<usize>(set)];
    }

    std::string_view name_of(TrackSound sound)
    {
        constexpr std::array<std::string_view, kTrackSounds> kNames { "Slow", "Med", "Fast", "Chain", "Start", "Stop", "WaterSplash", "WaterPlume" };
        return kNames[static_cast<usize>(sound)];
    }

    TrackSoundSet track_sound_set(std::string_view overlay)
    {
        // The folder is the last but one part of the path, whichever slash it is written with.
        const auto last = overlay.find_last_of("/\\");
        const std::string_view up = last == std::string_view::npos ? std::string_view { } : overlay.substr(0, last);
        const std::string_view folder = up.substr(up.find_last_of("/\\") + 1);
        const bool coaster = contains_ignoring_case(up, "coasters");
        for (const auto& [word, set] : kByFolder)
            if (contains_ignoring_case(folder, word))
                return set;
        return coaster ? TrackSoundSet::Steel : TrackSoundSet::None;
    }

    f32 ride_music_level(units::Distance away)
    {
        const f32 left = std::clamp((kMusicFar.value - away.value) / (kMusicFar.value - kMusicNear.value), 0.0f, 1.0f);
        return left * left;
    }

    usize tune_of(std::string_view file, std::span<const std::u16string> tunes)
    {
        file = file.substr(file.find_last_of("/\\") == std::string_view::npos ? 0 : file.find_last_of("/\\") + 1);
        const auto found = std::ranges::find_if(tunes, [&](const std::u16string& tune) { return strings::equals_ignoring_case(tune_file(1, std::span(&tune, 1)), file); });
        return found == tunes.end() ? 0 : static_cast<usize>(found - tunes.begin()) + 1;
    }

    std::string tune_file(usize tune, std::span<const std::u16string> tunes)
    {
        return tune == 0 || tune > tunes.size() ? std::string { } : std::format("{}{}", strings::to_utf8(tunes[tune - 1]), kTuneExtension);
    }

    std::vector<std::u16string> order_by_style(std::vector<std::u16string>& tunes, std::string_view styles)
    {
        std::map<std::string, std::string, strings::LessIgnoringCase> style_of;
        for (const auto part : std::views::split(styles, '\n'))
        {
            std::string_view line(part.begin(), part.end());
            if (line.starts_with('#'))
                continue;
            const usize star = line.find('*');
            const usize end = star == std::string_view::npos ? star : line.find('*', star + 1);
            if (end == std::string_view::npos || end == star + 1)
                continue;
            const std::string_view file = line.substr(0, star);
            style_of.emplace(file.substr(file.find_last_of("/\\") == std::string_view::npos ? 0 : file.find_last_of("/\\") + 1), line.substr(star + 1, end - star - 1));
        }
        const auto style = [&](const std::u16string& tune) {
            const auto found = style_of.find(tune_file(1, std::span(&tune, 1)));
            return found == style_of.end() ? std::string { } : found->second;
        };
        std::ranges::sort(tunes, [&](const std::u16string& a, const std::u16string& b) {
            const std::string sa = style(a);
            const std::string sb = style(b);
            return std::tuple(sa.empty(), sa, a) < std::tuple(sb.empty(), sb, b);
        });
        std::vector<std::u16string> shown;
        for (const std::u16string& tune : tunes)
            shown.push_back(style(tune).empty() ? tune : strings::to_utf16(style(tune)) + u" - " + tune);
        return shown;
    }

    RunningMix running_mix(f32 speed, RunningLoops loops)
    {
        speed = std::abs(speed);
        const f32 loud = ramp(speed, 0, kLoud);
        RunningMix mix;
        mix.slow = 1 - ramp(speed, kSlowOnly, kSlowGone);
        mix.fast = ramp(speed, kFastFrom, kFastOnly);
        mix.med = std::max(0.0f, 1 - mix.slow - mix.fast);
        if (!loops.fast)
            mix.med += std::exchange(mix.fast, 0.0f);
        if (!loops.med)
            mix.slow += std::exchange(mix.med, 0.0f);
        mix.slow *= loud;
        mix.med *= loud;
        mix.fast *= loud;
        return mix;
    }

} // namespace rct3::audio
