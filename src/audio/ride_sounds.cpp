// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/ride_sounds.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rct3::audio
{

    namespace
    {

        // Speed bands, world units a second: the slow loop alone up to kSlowOnly, gone by kSlowGone; the
        // fast loop from kFastFrom, alone by kFastOnly; the medium one fills between. Full loud from kLoud.
        constexpr float kSlowOnly = 6;
        constexpr float kSlowGone = 16;
        constexpr float kFastFrom = 12;
        constexpr float kFastOnly = 24;
        constexpr float kLoud = 14;

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

        float ramp(float x, float from, float to)
        {
            return std::clamp((x - from) / (to - from), 0.0f, 1.0f);
        }

    } // namespace

    std::string_view file_of(TrackSoundSet set)
    {
        constexpr std::array<std::string_view, kTrackSoundSets> kFiles { "", "Steel", "Wood", "Inverted", "Water", "GoKart", "HopOnTram", "AirBoat", "PaddleSteamer", "SafariTransport", "ElephantTransport" };
        return kFiles[static_cast<std::size_t>(set)];
    }

    std::string_view name_of(TrackSound sound)
    {
        constexpr std::array<std::string_view, kTrackSounds> kNames { "Slow", "Med", "Fast", "Chain", "Start", "Stop", "WaterSplash", "WaterPlume" };
        return kNames[static_cast<std::size_t>(sound)];
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

    RunningMix running_mix(float speed, bool has_med, bool has_fast)
    {
        speed = std::abs(speed);
        const float loud = ramp(speed, 0, kLoud);
        RunningMix mix;
        mix.slow = 1 - ramp(speed, kSlowOnly, kSlowGone);
        mix.fast = ramp(speed, kFastFrom, kFastOnly);
        mix.med = std::max(0.0f, 1 - mix.slow - mix.fast);
        if (!has_fast)
            mix.med += std::exchange(mix.fast, 0.0f);
        if (!has_med)
            mix.slow += std::exchange(mix.med, 0.0f);
        mix.slow *= loud;
        mix.med *= loud;
        mix.fast *= loud;
        return mix;
    }

} // namespace rct3::audio
