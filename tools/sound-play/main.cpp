// SPDX-License-Identifier: GPL-3.0-or-later
//
// sound-play: play one sound item of an OVL common/unique pair, for manual checks of the audio path.
//
//   sound-play [--null] [--volume V] [--pan P] [--pitch P] PATH            list the sound items
//   sound-play [--null] [--volume V] [--pan P] [--pitch P] PATH NAME       play NAME
//   sound-play [--null] [--volume V] [--pan P] [--pitch P] --tone HZ       play a 0.5 s sine, no file
//
// PATH names either file of the pair or the stem, as for ovl-dump. --null runs the mixer on miniaudio's
// null backend (no device needed, nothing audible). Without it the default playback device is used, and
// the null backend takes over when there is none. Prints the sound's format, length and the backend used.
#include "audio/audio.hpp"
#include "ovl/overlay.hpp"
#include "ovl/sound.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

    using rct3::audio::SoundBuffer;

    int usage(const char* argv0)
    {
        std::fprintf(
            stderr,
            "usage: %s [--null] [--volume V] [--pan P] [--pitch P] PATH [NAME]\n"
            "       %s [--null] [--volume V] [--pan P] [--pitch P] --tone HZ\n",
            argv0, argv0);
        return 2;
    }

    const char* backend_name(rct3::audio::Backend b)
    {
        switch (b)
        {
            case rct3::audio::Backend::Default:
                return "default device";
            case rct3::audio::Backend::Null:
                return "null backend";
            case rct3::audio::Backend::Offline:
                return "offline";
        }
        return "?";
    }

    SoundBuffer make_tone(float hz)
    {
        constexpr std::uint32_t kRate = 22050;
        std::vector<std::int16_t> s(kRate / 2);
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const double t = static_cast<double>(i) / kRate;
            s[i] = static_cast<std::int16_t>(12000.0 * std::sin(2.0 * std::numbers::pi * hz * t));
        }
        return rct3::audio::make_pcm16(s, 1, kRate);
    }

} // namespace

int main(int argc, char** argv)
{
    bool use_null = false;
    bool tone = false;
    float tone_hz = 440.0f;
    rct3::audio::PlayParams params;
    std::vector<const char*> positional;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        if (a == "--null")
            use_null = true;
        else if (a == "--volume" && i + 1 < argc)
            params.volume = std::strtof(argv[++i], nullptr);
        else if (a == "--pan" && i + 1 < argc)
            params.pan = std::strtof(argv[++i], nullptr);
        else if (a == "--pitch" && i + 1 < argc)
            params.pitch = std::strtof(argv[++i], nullptr);
        else if (a == "--tone" && i + 1 < argc)
        {
            tone = true;
            tone_hz = std::strtof(argv[++i], nullptr);
        }
        else if (!a.starts_with("--"))
            positional.push_back(argv[i]);
        else
            return usage(argv[0]);
    }
    if (!rct3::audio::is_valid(params))
    {
        std::fprintf(stderr, "volume must be 0..%g, pan -1..1, pitch above 0 and at most %g\n", static_cast<double>(rct3::audio::kMaxVolume), static_cast<double>(rct3::audio::kMaxPitch));
        return 2;
    }

    SoundBuffer buffer;
    if (tone)
    {
        if (!positional.empty() || !(tone_hz > 20.0f && tone_hz < 10000.0f))
            return usage(argv[0]);
        buffer = make_tone(tone_hz);
    }
    else
    {
        if (positional.empty() || positional.size() > 2)
            return usage(argv[0]);
        auto overlay = rct3::ovl::Overlay::load(positional[0]);
        if (!overlay)
        {
            std::fprintf(stderr, "%s: %s\n", positional[0], rct3::ovl::to_string(overlay.error()));
            return 1;
        }

        std::vector<rct3::ovl::SoundItem> items = rct3::ovl::read_sounds(*overlay, overlay->common());
        if (const auto unique = overlay->unique())
        {
            auto more = rct3::ovl::read_sounds(*overlay, *unique);
            items.insert(items.end(), more.begin(), more.end());
        }

        if (positional.size() == 1)
        {
            for (const auto& item : items)
            {
                const std::string name(item.name);
                if (!item.info)
                    std::printf("%-32s <unreadable>\n", name.c_str());
                else
                    std::printf("%-32s %u ch %u Hz %u bit %u bytes\n", name.c_str(), item.info->channels, item.info->sample_rate, item.info->bits_per_sample, item.info->data_size);
            }
            return 0;
        }

        const std::string_view wanted = positional[1];
        const rct3::ovl::SoundItem* found = nullptr;
        for (const auto& item : items)
        {
            if (item.name == wanted)
            {
                found = &item;
                break;
            }
        }
        if (found == nullptr || !found->info)
        {
            std::fprintf(stderr, "no readable sound item named '%s'\n", positional[1]);
            return 1;
        }
        auto converted = rct3::audio::from_ovl_sound(*found->info);
        if (!converted)
        {
            std::fprintf(stderr, "%s: %s\n", positional[1], rct3::audio::to_string(converted.error()));
            return 1;
        }
        buffer = std::move(*converted);
    }

    rct3::audio::EngineConfig config;
    config.backend = use_null ? rct3::audio::Backend::Null : rct3::audio::Backend::Default;
    auto engine = rct3::audio::AudioEngine::create(config);
    if (!engine)
    {
        std::fprintf(stderr, "audio: %s\n", rct3::audio::to_string(engine.error()));
        return 1;
    }

    auto voice = engine->play(buffer, params);
    if (!voice)
    {
        std::fprintf(stderr, "play: %s\n", rct3::audio::to_string(voice.error()));
        return 1;
    }
    const double seconds = buffer.duration_seconds() / static_cast<double>(params.pitch);
    std::printf("playing %zu frames, %u ch, %u Hz, %.3f s on the %s (%u Hz mix)\n", buffer.frame_count(), buffer.channels, buffer.sample_rate, seconds, backend_name(engine->backend()), engine->sample_rate());

    while (engine->is_playing(*voice))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Let the device drain what the mixer already handed over.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
