// SPDX-License-Identifier: GPL-3.0-or-later
//
// One piece of music played round and round from its file (or once, for a movie's sound), read a little ahead of where it plays
// (wma/stream.hpp) and handed to the engine as a stream. The owner calls update() often, every frame; the
// engine holds a second of music, so a stall shorter than that is not heard.
#pragma once

#include "audio/audio.hpp"
#include "audio/wma/stream.hpp"
#include "core/types.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace rct3::audio
{

    inline constexpr f32 kMusicAheadSeconds = 1.0f;

    class MusicPlayer
    {
    public:
        [[nodiscard]] static std::expected<MusicPlayer, std::string> start(AudioEngine& engine, const std::filesystem::path& file, f32 volume, bool loop = true);

        /// Reads on into the engine, back to the start at the end when looping. An error means the file cannot be read
        /// any more: the music stops when what was read has played, and later calls do nothing.
        [[nodiscard]] std::expected<void, std::string> update(AudioEngine& engine);
        bool set_volume(AudioEngine& engine, f32 volume);
        bool set_pan(AudioEngine& engine, f32 pan)
        {
            return engine.set_voice_pan(voice_, pan);
        }
        bool fade(AudioEngine& engine, f32 level, f32 seconds)
        {
            return engine.fade_voice(voice_, level, seconds);
        }
        void stop(AudioEngine& engine);

        [[nodiscard]] bool playing(const AudioEngine& engine) const
        {
            return engine.is_playing(voice_);
        }

    private:
        MusicPlayer(wma::WmaStream stream, VoiceHandle voice, bool loop);

        wma::WmaStream stream_;
        VoiceHandle voice_;
        std::vector<f32> chunk_;
        bool loop_ = true;
        bool ended_ = false;
    };

} // namespace rct3::audio
