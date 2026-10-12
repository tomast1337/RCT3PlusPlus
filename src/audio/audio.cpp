// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/audio.hpp"

#include "miniaudio/miniaudio_config.h"

#include <algorithm>
#include <array>
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
        return finite_in(p.volume, 0.0f, kMaxVolume) && finite_in(p.pan, -1.0f, 1.0f) && finite_in(p.pitch, 0.0f, kMaxPitch) && p.pitch > 0.0f;
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

        struct Voice
        {
            PcmSource source;
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
        std::unique_ptr<Voice[]> voices; // never moves: miniaudio keeps pointers into a started voice
        std::size_t voice_count = 0;

        ~Impl()
        {
            for (std::size_t i = 0; i < voice_count; ++i)
                release(voices[i]);
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
            ma_data_source_uninit(&v.source.base);
            v.source.data = { };
            v.in_use = false;
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
        m.collect();
        Voice* slot = nullptr;
        for (std::size_t i = 0; i < m.voice_count && slot == nullptr; ++i)
        {
            if (!m.voices[i].in_use)
                slot = &m.voices[i];
        }
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

        // Sounds are not spatialised: the engine would otherwise apply listener attenuation on top of pan.
        // The source reports its own sample rate, so the engine resamples it to the mixing rate.
        if (ma_sound_init_from_data_source(&m.engine, &src, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &v.sound) != MA_SUCCESS)
        {
            ma_data_source_uninit(&src.base);
            src.data = { };
            return std::unexpected(PlayError::Failed);
        }

        v.in_use = true;
        v.generation = v.generation == 0xFFFFFFFFu ? 1 : v.generation + 1;
        ma_sound_set_volume(&v.sound, params.volume);
        ma_sound_set_pan(&v.sound, params.pan);
        ma_sound_set_pitch(&v.sound, params.pitch);

        if (ma_sound_start(&v.sound) != MA_SUCCESS)
        {
            Impl::release(v);
            return std::unexpected(PlayError::Failed);
        }
        return VoiceHandle { static_cast<std::uint32_t>(&v - m.voices.get()), v.generation };
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
