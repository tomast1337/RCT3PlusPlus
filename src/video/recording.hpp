// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recording a video of the game from inside it, with its sound.
//
// While recording, each frame drawn moves the game on by exactly one frame of the video in game time, however
// long it took to draw, read back and encode, so the video plays smoothly on any machine. Frames go as raw
// pixels to an ffmpeg process through its standard input; the mixer's sound, pulled a video frame's worth at a
// time, goes to a WAV file beside it. When the recording stops, one more ffmpeg pass joins the two into the
// file asked for and removes them.
//
// No encoder is built in: good ones are large and some formats carry patents. Without ffmpeg the frames are
// kept as numbered PNG files beside the WAV, and the log says how to join them.
//
// Slow motion: frames are taken `fps` times a game second and the file plays them `playback_fps` times a
// second, so 240 taken and played at 30 is 8 times slower, each frame a real one. Its sound, when kept, is
// slowed with the picture (lower in pitch).
#pragma once

#include "core/types.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::video
{

    /// The most frames a recording takes a game second; past it a second of game time is too many frames to
    /// hold and encode.
    inline constexpr u32 kMaxCaptureFps = 1000;

    struct RecordSettings
    {
        std::filesystem::path path { };   ///< The video file, .mp4
        u32 fps = 30;                     ///< Frames taken a second of game time, 1..kMaxCaptureFps
        u32 playback_fps = 30;            ///< Frames the file shows a second; fewer than `fps` is slow motion
        bool sound = true;                ///< The mixer's sound in the file; slowed with the picture in slow motion
        std::filesystem::path ffmpeg { }; ///< The program that encodes; empty when there is none, for PNG frames
    };

    /// How fast a recording plays against the game: below 1 is slow motion (0.125 is 8 times slower).
    [[nodiscard]] constexpr f64 playback_speed(const RecordSettings& s)
    {
        return static_cast<f64>(s.playback_fps) / s.fps;
    }

    /// What a finished recording wrote.
    struct RecordResult
    {
        std::filesystem::path path { }; ///< The video, or the folder of its PNG frames without ffmpeg
        u64 frames = 0;
        u64 samples = 0;                        ///< Sound frames (one sample per channel) of game time
        u32 sample_rate = 0;                    ///< Of the sound
        units::Seconds length { };              ///< How long the file plays
        std::optional<std::string> problem { }; ///< Why it stopped early or was not joined, as also logged
    };

    enum class FramePace : u8
    {
        Wall,   ///< The time since the last frame, as the player sees it
        Script, ///< A sixtieth of a second, so what a script waits for in frames is the same game time on any machine
    };

    /// The game time a drawn frame moves the game on by: one frame of the video while recording at
    /// `recording_fps`, else as `pace` says, `wall` being the real time since the last frame.
    [[nodiscard]] units::Seconds frame_step(std::optional<u32> recording_fps, FramePace pace, units::Seconds wall);

    /// Sound frames from the start of a recording to the start of video frame `frame`. Each video frame gets the
    /// difference to the next, so the rest of a rate that does not divide (48000 at 144 fps) is carried on and
    /// none is lost or added: frame `n` of a second always starts at the same sample.
    [[nodiscard]] constexpr u64 sound_frames_before(u64 frame, u32 sample_rate, u32 fps)
    {
        return frame * sample_rate / fps;
    }

    inline constexpr usize kWavHeaderBytes = 44;

    /// The header of a 16-bit PCM WAV file of `data_bytes` of samples.
    [[nodiscard]] std::array<u8, kWavHeaderBytes> wav_header(u32 sample_rate, u16 channels, u32 data_bytes);

    /// A mixed sample as a 16-bit one, clipped to its range.
    [[nodiscard]] i16 pcm16_sample(f32 sample);

    /// `text` as one argument of a shell command line.
    [[nodiscard]] std::string shell_quoted(const std::string& text);

    /// Whether `program` runs as ffmpeg: a path, or a name looked for on the PATH.
    [[nodiscard]] bool ffmpeg_runs(const std::filesystem::path& program);

    /// One recording, from its first frame to the joined file.
    class Recorder
    {
    public:
        /// Opens the sound file (with `sample_rate` and `channels` from the mixer) and logs what it will write.
        [[nodiscard]] static std::expected<Recorder, std::string> start(RecordSettings settings, u32 sample_rate, u16 channels);

        /// Adds a frame. The first one sets the video's size, even in both directions as encoders want it; later
        /// ones of another size are cropped or padded to it. An error stops the recording.
        [[nodiscard]] std::expected<void, std::string> add_frame(u32 width, u32 height, std::span<const u8> rgba);
        /// How many sound frames the next video frame takes, as sound_frames_before carries the rest on.
        [[nodiscard]] usize take_sound_frames();
        /// Adds the mixer's sound, interleaved float, for the frames take_sound_frames gave.
        void add_sound(std::span<const f32> samples);
        /// Ends the recording and joins the video with its sound; also after an error, with what was written.
        [[nodiscard]] RecordResult finish(std::optional<std::string> problem = std::nullopt);

        [[nodiscard]] const RecordSettings& settings() const
        {
            return settings_;
        }
        [[nodiscard]] u32 sample_rate() const
        {
            return sample_rate_;
        }
        [[nodiscard]] u16 channels() const
        {
            return channels_;
        }

    private:
        struct PipeClose
        {
            void operator()(std::FILE* pipe) const;
        };
        struct FileClose
        {
            void operator()(std::FILE* file) const
            {
                std::fclose(file);
            }
        };

        Recorder() = default;
        [[nodiscard]] std::filesystem::path video_part() const;
        [[nodiscard]] std::filesystem::path sound_part() const;
        [[nodiscard]] std::filesystem::path frames_folder() const;

        RecordSettings settings_;
        u32 sample_rate_ = 0;
        u16 channels_ = 0;
        u32 width_ = 0; ///< Of the video, from the first frame
        u32 height_ = 0;
        u64 frames_ = 0;
        u64 frames_with_sound_ = 0; ///< Video frames take_sound_frames gave sound for
        u64 sound_frames_ = 0;
        std::unique_ptr<std::FILE, PipeClose> encoder_; ///< ffmpeg's standard input
        std::unique_ptr<std::FILE, FileClose> wav_;
        std::vector<u8> frame_;  ///< The frame at the video's size, kept between frames
        std::vector<i16> sound_; ///< Kept between frames
    };

} // namespace rct3::video
