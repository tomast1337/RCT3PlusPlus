// SPDX-License-Identifier: GPL-3.0-or-later
//
// One-shot sound playback with volume, pan and pitch, and a small mixer of concurrent voices.
//
// The module is a thin layer over the vendored miniaudio library. miniaudio is only included by
// audio.cpp, so nothing else pays for its header.
//
//   Data in:   SoundBuffer is plain data (interleaved 8/16-bit PCM or 32-bit float). `from_ovl_sound`
//              builds one from a decoded sound item of an overlay.
//   Playback:  AudioEngine owns the output (an audio device, miniaudio's null backend, or no device at
//              all) and a fixed number of voices. `play` copies the samples, so the buffer may be
//              destroyed right away.
//   Streams:   `play_stream` starts a voice for a sound too long to hold, music for one. Its owner hands it
//              float frames a little at a time with `feed`, as `stream_space` allows, and says when no more
//              will come with `end_stream`. A stream that runs dry before its end plays silence and goes on.
//
// Backends. `Backend::Default` opens the system's default playback device and, when that fails and
// `fall_back_to_null` is set, runs on the null backend instead: it consumes and discards audio at
// real-time speed, which keeps headless machines (CI, containers) working. `Backend::Null` asks for that
// directly. `Backend::Offline` creates no device and nothing consumes audio until the caller pulls
// frames with `render`; it is deterministic and is what the tests use.
//
// Mixing. Sounds are mixed in 32-bit float at `EngineConfig::sample_rate` with `EngineConfig::channels`
// channels. A voice contributes `sample * volume * pan_gain`, where pan is a balance control: -1 is full
// left, 0 is centre, +1 is full right; moving away from the centre attenuates the opposite channel
// linearly and leaves the near channel untouched. Pitch is a playback speed factor (2 is an octave up).
// The master volume scales the sum. Nothing clips or limits: the mix is float, and the output device
// clamps.
//
// Threading. Call the methods of one AudioEngine from one thread. The engine's own audio thread (when a
// device is open) never touches voice slots that the control thread is not done with.
#pragma once

#include "ovl/sound.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rct3::audio
{

    enum class SampleFormat : std::uint8_t
    {
        Pcm16,   ///< Signed 16-bit integers, native endianness
        Float32, ///< 32-bit floats in [-1, 1]
    };

    /// Bytes of one sample in `format`.
    [[nodiscard]] constexpr std::size_t bytes_per_sample(SampleFormat format) noexcept
    {
        return format == SampleFormat::Pcm16 ? 2 : 4;
    }

    /// Decoded audio. Frames are interleaved: one sample per channel, channel 0 first.
    struct SoundBuffer
    {
        SampleFormat format = SampleFormat::Pcm16;
        std::uint16_t channels = 0;
        std::uint32_t sample_rate = 0;
        std::vector<std::byte> data; ///< Whole frames, `frame_count() * channels * bytes_per_sample(format)` bytes

        [[nodiscard]] std::size_t frame_count() const noexcept;
        [[nodiscard]] double duration_seconds() const noexcept;
        /// True when channels and rate are in range, the data holds at least one frame and no partial frame.
        [[nodiscard]] bool valid() const noexcept;
    };

    inline constexpr std::uint16_t kMaxChannels = 8;
    inline constexpr std::uint32_t kMaxSampleRate = 384000;

    /// Builds a 16-bit buffer from samples that are already in memory.
    [[nodiscard]] SoundBuffer make_pcm16(std::span<const std::int16_t> samples, std::uint16_t channels, std::uint32_t sample_rate);
    /// Builds a float buffer from samples that are already in memory.
    [[nodiscard]] SoundBuffer make_float(std::span<const float> samples, std::uint16_t channels, std::uint32_t sample_rate);

    enum class ConvertError
    {
        NotPcm,           ///< The descriptor's format tag is not uncompressed PCM
        UnsupportedDepth, ///< Bits per sample other than 8 or 16
        BadChannelCount,  ///< Zero channels or more than kMaxChannels
        BadSampleRate,    ///< Zero or above kMaxSampleRate
        MissingSamples,   ///< The descriptor's data pointer did not resolve to its bytes
        Empty,            ///< No complete frame
    };

    [[nodiscard]] const char* to_string(ConvertError e) noexcept;

    /// Converts a decoded sound item. 16-bit PCM is copied as is, 8-bit PCM (unsigned) is widened to 16 bit.
    /// A trailing partial frame is dropped.
    [[nodiscard]] std::expected<SoundBuffer, ConvertError> from_ovl_sound(const ovl::SoundInfo& info);

    struct PlayParams
    {
        float volume = 1.0f; ///< Linear gain, 0 or more (1 leaves the sound unchanged)
        float pan = 0.0f;    ///< -1 left, 0 centre, +1 right
        float pitch = 1.0f;  ///< Playback speed factor, more than 0 and at most kMaxPitch
        /// Where the sound is in the world. The listener then pans it and fades it, evenly from full loud at `near`
        /// to silence at `far`, and a moving voice (set_voice_place) rises and falls in pitch as it passes.
        std::optional<glm::vec3> at = std::nullopt;
        float near = 1.0f;
        float far = 1000.0f;
    };

    inline constexpr float kMaxPitch = 16.0f;
    inline constexpr float kMaxVolume = 16.0f;

    /// True when every field is finite and in range.
    [[nodiscard]] bool is_valid(const PlayParams& p) noexcept;

    /// Identifies one playing instance. Handles are never reused for a different sound: a stale handle is
    /// simply not valid any more (`AudioEngine::is_playing` is false, the setters return false).
    struct VoiceHandle
    {
        std::uint32_t slot = 0;
        std::uint32_t generation = 0; ///< 0 is never issued
        [[nodiscard]] bool valid() const noexcept
        {
            return generation != 0;
        }
        friend bool operator==(const VoiceHandle&, const VoiceHandle&) = default;
    };

    enum class Backend : std::uint8_t
    {
        Default,
        Null,
        Offline
    };

    struct EngineConfig
    {
        Backend backend = Backend::Default;
        bool fall_back_to_null = true;     ///< Only for Backend::Default
        std::uint32_t sample_rate = 48000; ///< Mixing rate; the device is resampled to it as needed
        std::uint16_t channels = 2;        ///< 1 or 2
        std::uint32_t max_voices = 32;     ///< Hard cap, 1..kMaxVoices
        float master_volume = 1.0f;
    };

    inline constexpr std::uint32_t kMaxVoices = 256;

    enum class EngineError
    {
        BadConfig,
        NoDevice,   ///< No backend could be started
        InitFailed, ///< The mixer could not be created
    };

    [[nodiscard]] const char* to_string(EngineError e) noexcept;

    enum class PlayError
    {
        InvalidBuffer,
        InvalidParams,
        VoiceLimit, ///< All voices are busy; nothing was started
        Failed,     ///< The mixer refused the sound
    };

    [[nodiscard]] const char* to_string(PlayError e) noexcept;

    /// A streamed voice's samples: float, interleaved.
    struct StreamFormat
    {
        std::uint16_t channels = 2;
        std::uint32_t sample_rate = 44100;
        std::uint32_t buffer_frames = 44100; ///< How far ahead the owner may feed, 1..kMaxStreamFrames
    };

    inline constexpr std::uint32_t kMaxStreamFrames = 1u << 22;

    class AudioEngine
    {
    public:
        [[nodiscard]] static std::expected<AudioEngine, EngineError> create(const EngineConfig& config = { });

        AudioEngine(AudioEngine&&) noexcept;
        AudioEngine& operator=(AudioEngine&&) noexcept;
        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;
        /// Stops every voice and releases the device.
        ~AudioEngine();

        /// Starts `buffer` once, from its first frame. Finished voices are collected first, so the cap only
        /// counts sounds that are still playing.
        [[nodiscard]] std::expected<VoiceHandle, PlayError> play(const SoundBuffer& buffer, const PlayParams& params = { });

        /// Starts a voice that plays what `feed` gives it. It goes on, silent when it runs dry, until stopped or
        /// until end_stream and the frames fed before have played.
        [[nodiscard]] std::expected<VoiceHandle, PlayError> play_stream(const StreamFormat& format, const PlayParams& params = { });
        /// Frames `voice` can take now; 0 for a stale handle or a voice that is not a stream.
        [[nodiscard]] std::size_t stream_space(VoiceHandle voice) const;
        /// Adds whole frames, channels interleaved, and returns how many it took: at most stream_space().
        std::size_t feed(VoiceHandle voice, std::span<const float> samples);
        /// No more frames will come. Returns false for a stale handle or a voice that is not a stream.
        bool end_stream(VoiceHandle voice);

        /// Stops a voice and frees its slot. Returns false for a stale or invalid handle.
        bool stop(VoiceHandle voice);
        void stop_all();

        /// Per-voice controls. They return false (and change nothing) for a stale handle or an out-of-range value.
        bool set_voice_volume(VoiceHandle voice, float volume);
        bool set_voice_pan(VoiceHandle voice, float pan);
        bool set_voice_pitch(VoiceHandle voice, float pitch);
        /// Moves a voice started `at` a place, going at `velocity` (world units a second).
        bool set_voice_place(VoiceHandle voice, glm::vec3 at, glm::vec3 velocity = { });

        /// Where placed voices are heard from: `forward` is where the listener looks, `up` its head's up.
        void set_listener(glm::vec3 at, glm::vec3 forward, glm::vec3 up);
        /// Placed voices heard dull and quieter, as through water, while `muffled`; other voices stay clear.
        void set_muffled(bool muffled);

        /// True while the voice has frames left to play.
        [[nodiscard]] bool is_playing(VoiceHandle voice) const;
        /// Voices that are still playing.
        [[nodiscard]] std::uint32_t active_voices() const;
        [[nodiscard]] std::uint32_t max_voices() const noexcept;

        /// Out-of-range values are rejected (false) and leave the volume unchanged.
        bool set_master_volume(float volume);
        [[nodiscard]] float master_volume() const noexcept;

        [[nodiscard]] Backend backend() const noexcept; ///< What is really running (Null after a fall back)
        [[nodiscard]] std::uint32_t sample_rate() const noexcept;
        [[nodiscard]] std::uint16_t channels() const noexcept;

        /// Offline backend only: mixes `out.size() / channels()` frames into `out` (interleaved float) and
        /// returns the number of frames written, 0 on any other backend.
        std::size_t render(std::span<float> out);

    private:
        struct Impl;
        explicit AudioEngine(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rct3::audio
