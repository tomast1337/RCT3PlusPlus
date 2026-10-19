// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/music_player.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rct3::audio
{

    namespace
    {

        constexpr std::size_t kChunkFrames = 4096;

    } // namespace

    MusicPlayer::MusicPlayer(WmaStream stream, VoiceHandle voice, bool loop)
        : stream_(std::move(stream))
        , voice_(voice)
        , chunk_(kChunkFrames * stream_.channels())
        , loop_(loop)
    {
    }

    std::expected<MusicPlayer, std::string> MusicPlayer::start(AudioEngine& engine, const std::filesystem::path& file, float volume, bool loop)
    {
        auto stream = WmaStream::open(file);
        if (!stream)
            return std::unexpected(stream.error());
        const StreamFormat format {
            .channels = stream->channels(),
            .sample_rate = stream->sample_rate(),
            .buffer_frames = static_cast<std::uint32_t>(std::lround(kMusicAheadSeconds * static_cast<float>(stream->sample_rate()))),
        };
        auto voice = engine.play_stream(format, { .volume = volume });
        if (!voice)
            return std::unexpected(std::string("music: ") + to_string(voice.error()));
        MusicPlayer player(std::move(*stream), *voice, loop);
        if (auto first = player.update(engine); !first)
        {
            player.stop(engine);
            return std::unexpected(first.error());
        }
        return player;
    }

    std::expected<void, std::string> MusicPlayer::update(AudioEngine& engine)
    {
        if (ended_)
            return { };
        const std::size_t channels = stream_.channels();
        bool rewound = false;
        for (std::size_t space = engine.stream_space(voice_); space > 0; space = engine.stream_space(voice_))
        {
            const std::size_t want = std::min(space, kChunkFrames);
            auto read = stream_.read(std::span(chunk_).first(want * channels));
            if (!read)
            {
                ended_ = true;
                engine.end_stream(voice_);
                return std::unexpected(read.error());
            }
            if (*read == 0)
            {
                // The end: round again, unless playing once or the file gave nothing since the last time round.
                if (rewound || !loop_)
                {
                    ended_ = true;
                    engine.end_stream(voice_);
                    return { };
                }
                rewound = true;
                if (auto back = stream_.rewind(); !back)
                {
                    ended_ = true;
                    engine.end_stream(voice_);
                    return std::unexpected(back.error());
                }
                continue;
            }
            rewound = false;
            engine.feed(voice_, std::span(chunk_).first(*read * channels));
        }
        return { };
    }

    bool MusicPlayer::set_volume(AudioEngine& engine, float volume)
    {
        return engine.set_voice_volume(voice_, volume);
    }

    void MusicPlayer::stop(AudioEngine& engine)
    {
        engine.stop(voice_);
        ended_ = true;
    }

} // namespace rct3::audio
