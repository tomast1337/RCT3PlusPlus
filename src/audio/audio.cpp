// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/audio.hpp"

#include "miniaudio/miniaudio_config.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace rct3::audio
{

    // --- SoundBuffer ---------------------------------------------------------------------------------

    namespace
    {

        std::size_t frame_bytes(const SoundBuffer& b) noexcept
        {
            return b.channels * bytes_per_sample(b.format);
        }

        bool finite_in(float v, float lo, float hi) noexcept
        {
            return std::isfinite(v) && v >= lo && v <= hi;
        }

        // Under water: little above a low hum gets through, and that a little quieter.
        constexpr double kMuffledCutoff = 500; // Hz
        constexpr float kMuffledVolume = 0.7f;
        constexpr ma_uint32 kMuffleOrder = 2;
        // Open, the filter passes all but the very top of the band.
        double clear_cutoff(std::uint32_t sample_rate) noexcept
        {
            return 0.45 * sample_rate;
        }

    } // namespace

    std::size_t SoundBuffer::frame_count() const noexcept
    {
        const std::size_t fb = frame_bytes(*this);
        return fb == 0 ? 0 : data.size() / fb;
    }

    double SoundBuffer::duration_seconds() const noexcept
    {
        return sample_rate == 0 ? 0.0 : static_cast<double>(frame_count()) / sample_rate;
    }

    bool SoundBuffer::valid() const noexcept
    {
        if (channels == 0 || channels > kMaxChannels)
            return false;
        if (sample_rate == 0 || sample_rate > kMaxSampleRate)
            return false;
        const std::size_t fb = frame_bytes(*this);
        return !data.empty() && data.size() % fb == 0;
    }

    SoundBuffer make_pcm16(std::span<const std::int16_t> samples, std::uint16_t channels, std::uint32_t sample_rate)
    {
        SoundBuffer b { SampleFormat::Pcm16, channels, sample_rate, { } };
        b.data.resize(samples.size_bytes());
        if (!samples.empty())
            std::memcpy(b.data.data(), samples.data(), samples.size_bytes());
        return b;
    }

    SoundBuffer make_float(std::span<const float> samples, std::uint16_t channels, std::uint32_t sample_rate)
    {
        SoundBuffer b { SampleFormat::Float32, channels, sample_rate, { } };
        b.data.resize(samples.size_bytes());
        if (!samples.empty())
            std::memcpy(b.data.data(), samples.data(), samples.size_bytes());
        return b;
    }

    const char* to_string(ConvertError e) noexcept
    {
        switch (e)
        {
            case ConvertError::NotPcm:
                return "not uncompressed PCM";
            case ConvertError::UnsupportedDepth:
                return "unsupported bits per sample";
            case ConvertError::BadChannelCount:
                return "bad channel count";
            case ConvertError::BadSampleRate:
                return "bad sample rate";
            case ConvertError::MissingSamples:
                return "sample data missing";
            case ConvertError::Empty:
                return "no complete frame";
        }
        return "unknown";
    }

    std::expected<SoundBuffer, ConvertError> from_ovl_sound(const ovl::SoundInfo& info)
    {
        if (info.format_tag != 1)
            return std::unexpected(ConvertError::NotPcm);
        if (info.bits_per_sample != 8 && info.bits_per_sample != 16)
            return std::unexpected(ConvertError::UnsupportedDepth);
        if (info.channels == 0 || info.channels > kMaxChannels)
            return std::unexpected(ConvertError::BadChannelCount);
        if (info.sample_rate == 0 || info.sample_rate > kMaxSampleRate)
            return std::unexpected(ConvertError::BadSampleRate);
        if (!info.samples)
            return std::unexpected(ConvertError::MissingSamples);

        const std::size_t in_sample = info.bits_per_sample / 8;
        const std::size_t frames = info.samples->size() / (in_sample * info.channels);
        if (frames == 0)
            return std::unexpected(ConvertError::Empty);
        const std::size_t count = frames * info.channels;

        SoundBuffer out { SampleFormat::Pcm16, info.channels, info.sample_rate, { } };
        out.data.resize(count * 2);
        const std::byte* src = info.samples->data();
        if (in_sample == 2)
        {
            std::memcpy(out.data.data(), src, count * 2); // file samples are little endian, as is every target
        }
        else
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto v = static_cast<std::int16_t>((static_cast<int>(std::to_integer<unsigned>(src[i])) - 128) * 256);
                std::memcpy(out.data.data() + i * 2, &v, 2);
            }
        }
        return out;
    }

    bool is_valid(const PlayParams& p) noexcept
    {
        const bool placed = !p.at || (std::isfinite(p.at->x) && std::isfinite(p.at->y) && std::isfinite(p.at->z) && std::isfinite(p.far) && p.near >= 0.0f && p.far > p.near);
        return finite_in(p.volume, 0.0f, kMaxVolume) && finite_in(p.pan, -1.0f, 1.0f) && finite_in(p.pitch, 0.0f, kMaxPitch) && p.pitch > 0.0f && placed;
    }

    const char* to_string(EngineError e) noexcept
    {
        switch (e)
        {
            case EngineError::BadConfig:
                return "bad engine configuration";
            case EngineError::NoDevice:
                return "no audio backend could be started";
            case EngineError::InitFailed:
                return "mixer initialisation failed";
        }
        return "unknown";
    }

    const char* to_string(PlayError e) noexcept
    {
        switch (e)
        {
            case PlayError::InvalidBuffer:
                return "invalid sound buffer";
            case PlayError::InvalidParams:
                return "invalid play parameters";
            case PlayError::VoiceLimit:
                return "all voices are busy";
            case PlayError::Failed:
                return "the mixer refused the sound";
        }
        return "unknown";
    }

    // --- AudioEngine ---------------------------------------------------------------------------------

    namespace
    {

        // Plays an owned block of interleaved PCM, handing the mixer 32-bit float frames.
        //
        // This is a custom source for two reasons, both about how the engine reads a sound shorter than its
        // read-ahead cache. It drops the cached tail of a sound as soon as a read reports the end, which happens
        // whenever the source is not float (the engine converts non-float sources in a loop that runs until the
        // cache is full), and miniaudio's own audio buffer reports the end together with the last frames. A
        // one-shot would be silent in both cases. A float source that returns frames with success and reports the
        // end only on a read that yields nothing is played to the last frame.
        struct PcmSource
        {
            ma_data_source_base base; // must be first
            bool is_float = false;    // stored samples: float when set, 16-bit otherwise
            ma_uint32 channels = 0;
            ma_uint32 sample_rate = 0;
            ma_uint64 frames = 0;
            ma_uint64 cursor = 0;
            std::vector<std::byte> data;
        };

        ma_result pcm_read(ma_data_source* ds, void* out, ma_uint64 count, ma_uint64* read)
        {
            auto* src = static_cast<PcmSource*>(ds);
            const ma_uint64 n = std::min(count, src->frames - src->cursor);
            if (out != nullptr && n > 0)
            {
                const std::size_t samples = static_cast<std::size_t>(n) * src->channels;
                const std::size_t first = static_cast<std::size_t>(src->cursor) * src->channels;
                auto* dst = static_cast<float*>(out);
                if (src->is_float)
                {
                    std::memcpy(dst, src->data.data() + first * sizeof(float), samples * sizeof(float));
                }
                else
                {
                    for (std::size_t i = 0; i < samples; ++i)
                    {
                        std::int16_t v = 0;
                        std::memcpy(&v, src->data.data() + (first + i) * sizeof v, sizeof v);
                        dst[i] = static_cast<float>(v) * (1.0f / 32768.0f);
                    }
                }
            }
            src->cursor += n;
            if (read != nullptr)
                *read = n;
            return n == 0 && count > 0 ? MA_AT_END : MA_SUCCESS;
        }

        ma_result pcm_seek(ma_data_source* ds, ma_uint64 frame)
        {
            auto* src = static_cast<PcmSource*>(ds);
            if (frame > src->frames)
                return MA_INVALID_ARGS;
            src->cursor = frame;
            return MA_SUCCESS;
        }

        ma_result pcm_format(ma_data_source* ds, ma_format* format, ma_uint32* channels, ma_uint32* rate, ma_channel* map, size_t map_cap)
        {
            const auto* src = static_cast<const PcmSource*>(ds);
            *format = ma_format_f32;
            *channels = src->channels;
            *rate = src->sample_rate;
            if (map != nullptr && map_cap > 0)
                ma_channel_map_init_standard(ma_standard_channel_map_default, map, map_cap, src->channels);
            return MA_SUCCESS;
        }

        ma_result pcm_cursor(ma_data_source* ds, ma_uint64* cursor)
        {
            *cursor = static_cast<const PcmSource*>(ds)->cursor;
            return MA_SUCCESS;
        }

        ma_result pcm_length(ma_data_source* ds, ma_uint64* length)
        {
            *length = static_cast<const PcmSource*>(ds)->frames;
            return MA_SUCCESS;
        }

        const ma_data_source_vtable kPcmVtable = { pcm_read, pcm_seek, pcm_format, pcm_cursor, pcm_length, nullptr, 0 };

        // Plays what its owner feeds into a ring buffer: the control thread writes, the audio thread reads.
        // Running dry plays silence instead of ending, so a late feed is a gap, not the end of the music; the
        // end comes once the owner has said so and the buffer is empty.
        struct StreamSource
        {
            ma_data_source_base base; // must be first
            ma_pcm_rb ring;
            bool ring_ready = false;
            ma_uint32 channels = 0;
            ma_uint32 sample_rate = 0;
            ma_uint64 cursor = 0;
            std::atomic<bool> ended = false;
        };

        ma_result stream_read(ma_data_source* ds, void* out, ma_uint64 count, ma_uint64* read)
        {
            auto* src = static_cast<StreamSource*>(ds);
            auto* dst = static_cast<float*>(out);
            const bool ended = src->ended.load(std::memory_order_acquire);
            ma_uint64 got = 0;
            while (got < count)
            {
                ma_uint32 n = static_cast<ma_uint32>(std::min<ma_uint64>(count - got, 0xFFFFFFFFu));
                void* buffer = nullptr;
                if (ma_pcm_rb_acquire_read(&src->ring, &n, &buffer) != MA_SUCCESS || n == 0)
                    break;
                if (dst != nullptr)
                    std::memcpy(dst + got * src->channels, buffer, static_cast<std::size_t>(n) * src->channels * sizeof(float));
                ma_pcm_rb_commit_read(&src->ring, n);
                got += n;
            }
            if (got < count && !ended)
            {
                if (dst != nullptr)
                    std::fill(dst + got * src->channels, dst + count * src->channels, 0.0f);
                got = count;
            }
            src->cursor += got;
            if (read != nullptr)
                *read = got;
            return got == 0 && count > 0 ? MA_AT_END : MA_SUCCESS;
        }

        ma_result stream_seek(ma_data_source*, ma_uint64)
        {
            return MA_NOT_IMPLEMENTED;
        }

        ma_result stream_format(ma_data_source* ds, ma_format* format, ma_uint32* channels, ma_uint32* rate, ma_channel* map, size_t map_cap)
        {
            const auto* src = static_cast<const StreamSource*>(ds);
            *format = ma_format_f32;
            *channels = src->channels;
            *rate = src->sample_rate;
            if (map != nullptr && map_cap > 0)
                ma_channel_map_init_standard(ma_standard_channel_map_default, map, map_cap, src->channels);
            return MA_SUCCESS;
        }

        ma_result stream_cursor(ma_data_source* ds, ma_uint64* cursor)
        {
            *cursor = static_cast<const StreamSource*>(ds)->cursor;
            return MA_SUCCESS;
        }

        ma_result stream_length(ma_data_source*, ma_uint64* length)
        {
            *length = 0; // Not known
            return MA_NOT_IMPLEMENTED;
        }

        const ma_data_source_vtable kStreamVtable = { stream_read, stream_seek, stream_format, stream_cursor, stream_length, nullptr, 0 };

        struct Voice
        {
            PcmSource source;
            StreamSource stream;
            bool streamed = false; // plays `stream`, not `source`
            ma_sound sound;
            std::uint32_t generation = 0; // bumped on every start, so stale handles never match
            bool in_use = false;
        };

    } // namespace

    struct AudioEngine::Impl
    {
        EngineConfig config;
        Backend active = Backend::Offline;
        float master = 1.0f;
        bool context_ready = false;
        bool engine_ready = false;
        ma_context context { };
        ma_engine engine { };
        ma_lpf_node muffle { }; // Placed voices play through it
        bool muffle_ready = false;
        bool muffled = false;
        std::unique_ptr<Voice[]> voices; // never moves: miniaudio keeps pointers into a started voice
        std::size_t voice_count = 0;

        ~Impl()
        {
            for (std::size_t i = 0; i < voice_count; ++i)
                release(voices[i]);
            if (muffle_ready)
                ma_lpf_node_uninit(&muffle, nullptr);
            if (engine_ready)
                ma_engine_uninit(&engine);
            if (context_ready)
                ma_context_uninit(&context);
        }

        static void release(Voice& v)
        {
            if (!v.in_use)
                return;
            ma_sound_uninit(&v.sound);
            release_source(v);
            v.in_use = false;
        }

        // The voice's source, once its sound is gone or was never made.
        static void release_source(Voice& v)
        {
            if (v.streamed)
            {
                ma_data_source_uninit(&v.stream.base);
                if (v.stream.ring_ready)
                    ma_pcm_rb_uninit(&v.stream.ring);
                v.stream.ring_ready = false;
            }
            else
            {
                ma_data_source_uninit(&v.source.base);
                v.source.data = { };
            }
            v.streamed = false;
        }

        Voice* find(VoiceHandle h)
        {
            if (!h.valid() || h.slot >= voice_count)
                return nullptr;
            Voice& v = voices[h.slot];
            return v.in_use && v.generation == h.generation ? &v : nullptr;
        }
        const Voice* find(VoiceHandle h) const
        {
            return const_cast<Impl*>(this)->find(h);
        }

        static bool finished(const Voice& v)
        {
            return ma_sound_at_end(&v.sound) != MA_FALSE;
        }

        Voice* free_slot()
        {
            collect();
            for (std::size_t i = 0; i < voice_count; ++i)
            {
                if (!voices[i].in_use)
                    return &voices[i];
            }
            return nullptr;
        }

        // Only placed sounds are spatialised: the engine would otherwise fade the rest by their distance from the
        // listener on top of pan. The source reports its own sample rate, so the engine resamples it to the mixing rate.
        std::expected<VoiceHandle, PlayError> start_voice(Voice& v, ma_data_source* source, const PlayParams& params)
        {
            if (ma_sound_init_from_data_source(&engine, source, params.at ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &v.sound) != MA_SUCCESS)
            {
                release_source(v);
                return std::unexpected(PlayError::Failed);
            }
            v.in_use = true;
            v.generation = v.generation == 0xFFFFFFFFu ? 1 : v.generation + 1;
            ma_sound_set_volume(&v.sound, params.volume);
            ma_sound_set_pan(&v.sound, params.pan);
            ma_sound_set_pitch(&v.sound, params.pitch);
            if (params.at)
            {
                if (muffle_ready)
                    ma_node_attach_output_bus(&v.sound, 0, &muffle, 0);
                ma_sound_set_position(&v.sound, params.at->x, params.at->y, params.at->z);
                ma_sound_set_attenuation_model(&v.sound, ma_attenuation_model_linear);
                ma_sound_set_min_distance(&v.sound, params.near);
                ma_sound_set_max_distance(&v.sound, params.far);
            }
            if (ma_sound_start(&v.sound) != MA_SUCCESS)
            {
                release(v);
                return std::unexpected(PlayError::Failed);
            }
            return VoiceHandle { static_cast<std::uint32_t>(&v - voices.get()), v.generation };
        }

        void collect()
        {
            for (std::size_t i = 0; i < voice_count; ++i)
            {
                if (voices[i].in_use && finished(voices[i]))
                    release(voices[i]);
            }
        }

        bool start(const EngineConfig& cfg, ma_context* ctx)
        {
            ma_engine_config ec = ma_engine_config_init();
            ec.channels = cfg.channels;
            ec.sampleRate = cfg.sample_rate;
            if (ctx == nullptr)
                ec.noDevice = MA_TRUE;
            else
                ec.pContext = ctx;
            return ma_engine_init(&ec, &engine) == MA_SUCCESS;
        }
    };

    std::expected<AudioEngine, EngineError> AudioEngine::create(const EngineConfig& config)
    {
        if (config.max_voices == 0 || config.max_voices > kMaxVoices)
            return std::unexpected(EngineError::BadConfig);
        if (config.channels == 0 || config.channels > 2)
            return std::unexpected(EngineError::BadConfig);
        if (config.sample_rate < 8000 || config.sample_rate > kMaxSampleRate)
            return std::unexpected(EngineError::BadConfig);
        if (!finite_in(config.master_volume, 0.0f, kMaxVolume))
            return std::unexpected(EngineError::BadConfig);

        auto impl = std::make_unique<Impl>();
        impl->config = config;
        impl->master = config.master_volume;
        impl->voices = std::make_unique<Voice[]>(config.max_voices);
        impl->voice_count = config.max_voices;

        if (config.backend == Backend::Offline)
        {
            if (!impl->start(config, nullptr))
                return std::unexpected(EngineError::InitFailed);
            impl->engine_ready = true;
            impl->active = Backend::Offline;
        }
        else
        {
            // Real backends, in miniaudio's preferred order for this platform, without the null backend.
            std::array<ma_backend, ma_backend_null + 1> all { };
            std::size_t n = 0;
            ma_get_enabled_backends(all.data(), all.size(), &n);
            std::vector<ma_backend> real;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (all[i] != ma_backend_null)
                    real.push_back(all[i]);
            }

            bool ok = false;
            if (config.backend == Backend::Default && !real.empty())
            {
                ok = ma_context_init(real.data(), static_cast<ma_uint32>(real.size()), nullptr, &impl->context) == MA_SUCCESS;
                if (ok)
                {
                    impl->context_ready = true;
                    ok = impl->start(config, &impl->context);
                    if (!ok)
                    {
                        ma_context_uninit(&impl->context);
                        impl->context_ready = false;
                    }
                }
                if (ok)
                    impl->active = Backend::Default;
            }
            if (!ok && (config.backend == Backend::Null || config.fall_back_to_null))
            {
                const ma_backend null_only[] = { ma_backend_null };
                ok = ma_context_init(null_only, 1, nullptr, &impl->context) == MA_SUCCESS;
                if (ok)
                {
                    impl->context_ready = true;
                    ok = impl->start(config, &impl->context);
                }
                if (ok)
                    impl->active = Backend::Null;
            }
            if (!ok)
                return std::unexpected(EngineError::NoDevice);
            impl->engine_ready = true;
        }

        ma_engine_set_volume(&impl->engine, impl->master);
        // Open until muffled; without it placed voices go straight to the output.
        const ma_lpf_node_config muffle = ma_lpf_node_config_init(config.channels, config.sample_rate, clear_cutoff(config.sample_rate), kMuffleOrder);
        impl->muffle_ready = ma_lpf_node_init(ma_engine_get_node_graph(&impl->engine), &muffle, nullptr, &impl->muffle) == MA_SUCCESS && ma_node_attach_output_bus(&impl->muffle, 0, ma_engine_get_endpoint(&impl->engine), 0) == MA_SUCCESS;
        return AudioEngine(std::move(impl));
    }

    AudioEngine::AudioEngine(std::unique_ptr<Impl> impl) noexcept
        : impl_(std::move(impl))
    {
    }
    AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
    AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;
    AudioEngine::~AudioEngine() = default;

    std::expected<VoiceHandle, PlayError> AudioEngine::play(const SoundBuffer& buffer, const PlayParams& params)
    {
        if (!buffer.valid())
            return std::unexpected(PlayError::InvalidBuffer);
        if (!is_valid(params))
            return std::unexpected(PlayError::InvalidParams);

        Impl& m = *impl_;
        Voice* slot = m.free_slot();
        if (slot == nullptr)
            return std::unexpected(PlayError::VoiceLimit);
        Voice& v = *slot;

        PcmSource& src = v.source;
        src = PcmSource { };
        src.is_float = buffer.format == SampleFormat::Float32;
        src.channels = buffer.channels;
        src.sample_rate = buffer.sample_rate;
        src.frames = buffer.frame_count();
        src.data = buffer.data;
        ma_data_source_config dc = ma_data_source_config_init();
        dc.vtable = &kPcmVtable;
        if (ma_data_source_init(&dc, &src.base) != MA_SUCCESS)
            return std::unexpected(PlayError::Failed);

        return m.start_voice(v, &src, params);
    }

    std::expected<VoiceHandle, PlayError> AudioEngine::play_stream(const StreamFormat& format, const PlayParams& params)
    {
        if (format.channels == 0 || format.channels > kMaxChannels || format.sample_rate == 0 || format.sample_rate > kMaxSampleRate)
            return std::unexpected(PlayError::InvalidBuffer);
        if (format.buffer_frames == 0 || format.buffer_frames > kMaxStreamFrames)
            return std::unexpected(PlayError::InvalidBuffer);
        if (!is_valid(params))
            return std::unexpected(PlayError::InvalidParams);

        Impl& m = *impl_;
        Voice* slot = m.free_slot();
        if (slot == nullptr)
            return std::unexpected(PlayError::VoiceLimit);
        Voice& v = *slot;

        StreamSource& src = v.stream;
        src.channels = format.channels;
        src.sample_rate = format.sample_rate;
        src.cursor = 0;
        src.ended.store(false, std::memory_order_relaxed);
        if (ma_pcm_rb_init(ma_format_f32, format.channels, format.buffer_frames, nullptr, nullptr, &src.ring) != MA_SUCCESS)
            return std::unexpected(PlayError::Failed);
        src.ring_ready = true;
        ma_data_source_config dc = ma_data_source_config_init();
        dc.vtable = &kStreamVtable;
        if (ma_data_source_init(&dc, &src.base) != MA_SUCCESS)
        {
            ma_pcm_rb_uninit(&src.ring);
            src.ring_ready = false;
            return std::unexpected(PlayError::Failed);
        }
        v.streamed = true;
        return m.start_voice(v, &src, params);
    }

    std::size_t AudioEngine::stream_space(VoiceHandle voice) const
    {
        const Voice* v = impl_->find(voice);
        if (v == nullptr || !v->streamed || v->stream.ended.load(std::memory_order_relaxed))
            return 0;
        return ma_pcm_rb_available_write(const_cast<ma_pcm_rb*>(&v->stream.ring));
    }

    std::size_t AudioEngine::feed(VoiceHandle voice, std::span<const float> samples)
    {
        Voice* v = impl_->find(voice);
        if (v == nullptr || !v->streamed || v->stream.ended.load(std::memory_order_relaxed))
            return 0;
        StreamSource& src = v->stream;
        const std::size_t frames = samples.size() / src.channels;
        std::size_t done = 0;
        while (done < frames)
        {
            ma_uint32 n = static_cast<ma_uint32>(std::min<std::size_t>(frames - done, 0xFFFFFFFFu));
            void* buffer = nullptr;
            if (ma_pcm_rb_acquire_write(&src.ring, &n, &buffer) != MA_SUCCESS || n == 0)
                break;
            std::memcpy(buffer, samples.data() + done * src.channels, static_cast<std::size_t>(n) * src.channels * sizeof(float));
            ma_pcm_rb_commit_write(&src.ring, n);
            done += n;
        }
        return done;
    }

    bool AudioEngine::end_stream(VoiceHandle voice)
    {
        Voice* v = impl_->find(voice);
        if (v == nullptr || !v->streamed)
            return false;
        v->stream.ended.store(true, std::memory_order_release);
        return true;
    }

    bool AudioEngine::stop(VoiceHandle voice)
    {
        Voice* v = impl_->find(voice);
        if (v == nullptr)
            return false;
        ma_sound_stop(&v->sound);
        Impl::release(*v);
        return true;
    }

    void AudioEngine::stop_all()
    {
        for (std::size_t i = 0; i < impl_->voice_count; ++i)
        {
            Voice& v = impl_->voices[i];
            if (v.in_use)
                ma_sound_stop(&v.sound);
            Impl::release(v);
        }
    }

    bool AudioEngine::set_voice_volume(VoiceHandle voice, float volume)
    {
        Voice* v = impl_->find(voice);
        if (v == nullptr || !finite_in(volume, 0.0f, kMaxVolume))
            return false;
        ma_sound_set_volume(&v->sound, volume);
        return true;
    }

    bool AudioEngine::set_voice_pan(VoiceHandle voice, float pan)
    {
        Voice* v = impl_->find(voice);
        if (v == nullptr || !finite_in(pan, -1.0f, 1.0f))
            return false;
        ma_sound_set_pan(&v->sound, pan);
        return true;
    }

    bool AudioEngine::set_voice_pitch(VoiceHandle voice, float pitch)
    {
        Voice* v = impl_->find(voice);
        if (v == nullptr || !finite_in(pitch, 0.0f, kMaxPitch) || pitch <= 0.0f)
            return false;
        ma_sound_set_pitch(&v->sound, pitch);
        return true;
    }

    bool AudioEngine::set_voice_place(VoiceHandle voice, glm::vec3 at, glm::vec3 velocity)
    {
        Voice* v = impl_->find(voice);
        const auto finite = [](glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
        if (v == nullptr || !finite(at) || !finite(velocity) || ma_sound_is_spatialization_enabled(&v->sound) == MA_FALSE)
            return false;
        ma_sound_set_position(&v->sound, at.x, at.y, at.z);
        ma_sound_set_velocity(&v->sound, velocity.x, velocity.y, velocity.z);
        return true;
    }

    void AudioEngine::set_listener(glm::vec3 at, glm::vec3 forward, glm::vec3 up)
    {
        ma_engine_listener_set_position(&impl_->engine, 0, at.x, at.y, at.z);
        ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
    }

    void AudioEngine::set_muffled(bool muffled)
    {
        Impl& m = *impl_;
        if (!m.muffle_ready || m.muffled == muffled)
            return;
        m.muffled = muffled;
        const ma_lpf_config filter = ma_lpf_config_init(ma_format_f32, m.config.channels, m.config.sample_rate, muffled ? kMuffledCutoff : clear_cutoff(m.config.sample_rate), kMuffleOrder);
        ma_lpf_node_reinit(&filter, &m.muffle);
        ma_node_set_output_bus_volume(&m.muffle, 0, muffled ? kMuffledVolume : 1.0f);
    }

    bool AudioEngine::is_playing(VoiceHandle voice) const
    {
        const Voice* v = impl_->find(voice);
        return v != nullptr && !Impl::finished(*v);
    }

    std::uint32_t AudioEngine::active_voices() const
    {
        std::uint32_t n = 0;
        for (std::size_t i = 0; i < impl_->voice_count; ++i)
        {
            if (impl_->voices[i].in_use && !Impl::finished(impl_->voices[i]))
                ++n;
        }
        return n;
    }

    std::uint32_t AudioEngine::max_voices() const noexcept
    {
        return static_cast<std::uint32_t>(impl_->voice_count);
    }

    bool AudioEngine::set_master_volume(float volume)
    {
        if (!finite_in(volume, 0.0f, kMaxVolume))
            return false;
        impl_->master = volume;
        ma_engine_set_volume(&impl_->engine, volume);
        return true;
    }

    float AudioEngine::master_volume() const noexcept
    {
        return impl_->master;
    }
    Backend AudioEngine::backend() const noexcept
    {
        return impl_->active;
    }
    std::uint32_t AudioEngine::sample_rate() const noexcept
    {
        return impl_->config.sample_rate;
    }
    std::uint16_t AudioEngine::channels() const noexcept
    {
        return impl_->config.channels;
    }

    std::size_t AudioEngine::render(std::span<float> out)
    {
        if (impl_->active != Backend::Offline)
            return 0;
        const std::size_t frames = out.size() / impl_->config.channels;
        // The mixer reports fewer frames than asked for when nothing is playing (or the last voice ends
        // inside the block); what it did not write is silence.
        std::ranges::fill(out, 0.0f);
        ma_uint64 read = 0;
        if (frames == 0)
            return 0;
        if (ma_engine_read_pcm_frames(&impl_->engine, out.data(), frames, &read) != MA_SUCCESS)
            return 0;
        return frames;
    }

} // namespace rct3::audio
