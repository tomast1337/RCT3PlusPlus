// SPDX-License-Identifier: GPL-3.0-or-later
#include "video/recording.hpp"

#include "image/png_encoder.hpp"
#include "logging/log.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <format>
#include <system_error>

#ifdef _WIN32
    #define popen _popen
    #define pclose _pclose
constexpr const char* kWritePipe = "wb";
#else
constexpr const char* kWritePipe = "w"; // POSIX pipes have no binary mode
#endif

namespace rct3::video
{

    namespace
    {
        // A whole command line as the platform's shell runs it. cmd.exe drops the first and last quote of a
        // line that starts with one, so the line gets a pair of its own there.
        std::string shell_line(const std::string& command)
        {
#ifdef _WIN32
            return std::format("\"{}\"", command);
#else
            return command;
#endif
        }

#ifdef _WIN32
        constexpr std::string_view kQuiet = " >NUL 2>&1";
#else
        constexpr std::string_view kQuiet = " >/dev/null 2>&1";
#endif
    } // namespace

    units::Seconds frame_step(std::optional<u32> recording_fps, FramePace pace, units::Seconds wall)
    {
        if (recording_fps)
            return { 1.0f / static_cast<f32>(*recording_fps) };
        return pace == FramePace::Script ? units::Seconds { 1.0f / 60 } : wall;
    }

    std::array<u8, kWavHeaderBytes> wav_header(u32 sample_rate, u16 channels, u32 data_bytes)
    {
        std::array<u8, kWavHeaderBytes> h { };
        usize at = 0;
        const auto text = [&](std::string_view s) {
            std::memcpy(h.data() + at, s.data(), 4);
            at += 4;
        };
        const auto little = [&](u32 value, usize bytes) {
            for (usize i = 0; i < bytes; ++i)
                h[at++] = static_cast<u8>(value >> (8 * i));
        };
        constexpr u16 kBytesPerSample = 2;
        text("RIFF");
        little(static_cast<u32>(kWavHeaderBytes - 8) + data_bytes, 4); // What follows this field
        text("WAVE");
        text("fmt ");
        little(16, 4); // The format block's size
        little(1, 2);  // PCM
        little(channels, 2);
        little(sample_rate, 4);
        little(sample_rate * channels * kBytesPerSample, 4); // Bytes a second
        little(channels * kBytesPerSample, 2);               // Bytes a frame
        little(8 * kBytesPerSample, 2);                      // Bits a sample
        text("data");
        little(data_bytes, 4);
        return h;
    }

    i16 pcm16_sample(f32 sample)
    {
        return static_cast<i16>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    }

    std::string shell_quoted(const std::string& text)
    {
#ifdef _WIN32
        return std::format("\"{}\"", text); // Windows paths cannot hold a quote
#else
        std::string quoted = "'";
        for (const char c : text)
            quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return quoted + "'";
#endif
    }

    bool ffmpeg_runs(const std::filesystem::path& program)
    {
        return !program.empty() && std::system(shell_line(std::format("{} -version{}", shell_quoted(program.string()), kQuiet)).c_str()) == 0;
    }

    void Recorder::PipeClose::operator()(std::FILE* pipe) const
    {
        pclose(pipe);
    }

    std::filesystem::path Recorder::video_part() const
    {
        return std::filesystem::path(settings_.path).replace_extension(".video.mkv");
    }

    std::filesystem::path Recorder::sound_part() const
    {
        return std::filesystem::path(settings_.path).replace_extension(".wav");
    }

    std::filesystem::path Recorder::frames_folder() const
    {
        std::filesystem::path folder = settings_.path;
        return folder.replace_filename(std::format("{}_frames", settings_.path.stem().string()));
    }

    std::expected<Recorder, std::string> Recorder::start(RecordSettings settings, u32 sample_rate, u16 channels)
    {
        if (settings.fps == 0 || settings.fps > kMaxCaptureFps || settings.playback_fps == 0 || settings.playback_fps > kMaxCaptureFps)
            return std::unexpected(std::format("recording: frame rates go from 1 to {}", kMaxCaptureFps));
        if (sample_rate == 0 || channels == 0)
            return std::unexpected("recording: no sound format");
        Recorder r;
        r.settings_ = std::move(settings);
        r.sample_rate_ = sample_rate;
        r.channels_ = channels;
        std::error_code ec;
        if (r.settings_.path.has_parent_path())
            std::filesystem::create_directories(r.settings_.path.parent_path(), ec);
        if (r.settings_.sound)
        {
            r.wav_.reset(std::fopen(r.sound_part().string().c_str(), "wb"));
            const auto header = wav_header(sample_rate, channels, 0);
            if (!r.wav_ || std::fwrite(header.data(), 1, header.size(), r.wav_.get()) != header.size())
                return std::unexpected(std::format("recording: cannot write {}", r.sound_part().string()));
        }
        if (r.settings_.ffmpeg.empty())
        {
            std::filesystem::create_directories(r.frames_folder(), ec);
            if (ec)
                return std::unexpected(std::format("recording: cannot make {}: {}", r.frames_folder().string(), ec.message()));
        }
#ifndef _WIN32
        // An encoder that quits would otherwise end the game on the next frame written to it.
        std::signal(SIGPIPE, SIG_IGN);
#endif
        logging::logger("record").info("recording {} at {} frames a game second, played at {}{}", r.settings_.path.string(), r.settings_.fps, r.settings_.playback_fps, r.settings_.sound ? ", with sound" : "");
        return r;
    }

    std::expected<void, std::string> Recorder::add_frame(u32 width, u32 height, std::span<const u8> rgba)
    {
        if (rgba.size() < usize { width } * height * 4)
            return std::unexpected("recording: a frame with too few pixels");
        if (frames_ == 0)
        {
            width_ = width & ~1u;
            height_ = height & ~1u;
            if (width_ == 0 || height_ == 0)
                return std::unexpected(std::format("recording: a frame of {}x{} is too small", width, height));
            frame_.assign(usize { width_ } * height_ * 4, 0);
            if (!settings_.ffmpeg.empty())
            {
                const std::string command = std::format("{} -loglevel error -y -f rawvideo -pix_fmt rgba -video_size {}x{} -framerate {} -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p {}", shell_quoted(settings_.ffmpeg.string()), width_, height_, settings_.playback_fps, shell_quoted(video_part().string()));
                encoder_.reset(popen(shell_line(command).c_str(), kWritePipe));
                if (!encoder_)
                    return std::unexpected(std::format("recording: cannot start {}", settings_.ffmpeg.string()));
            }
        }
        // Rows and columns past the video's size are cut off; a smaller frame leaves the rest as it was.
        const usize row = usize { std::min(width, width_) } * 4;
        for (u32 y = 0; y < std::min(height, height_); ++y)
            std::memcpy(frame_.data() + usize { y } * width_ * 4, rgba.data() + usize { y } * width * 4, row);
        if (encoder_)
        {
            if (std::fwrite(frame_.data(), 1, frame_.size(), encoder_.get()) != frame_.size())
                return std::unexpected(std::format("recording: {} stopped taking frames", settings_.ffmpeg.string()));
        }
        else if (auto saved = image::save_png(frames_folder() / std::format("{:06}.png", frames_), { width_, height_, frame_ }); !saved)
            return std::unexpected(std::format("recording: {}", saved.error()));
        ++frames_;
        return { };
    }

    usize Recorder::take_sound_frames()
    {
        const u64 frame = frames_with_sound_++;
        return static_cast<usize>(sound_frames_before(frame + 1, sample_rate_, settings_.fps) - sound_frames_before(frame, sample_rate_, settings_.fps));
    }

    void Recorder::add_sound(std::span<const f32> samples)
    {
        sound_frames_ += samples.size() / channels_;
        if (!wav_)
            return;
        sound_.resize(samples.size());
        std::ranges::transform(samples, sound_.begin(), pcm16_sample);
        // shortcut: native byte order, which is little-endian on every platform the game builds for.
        std::fwrite(sound_.data(), sizeof(i16), sound_.size(), wav_.get());
    }

    RecordResult Recorder::finish(std::optional<std::string> problem)
    {
        const auto log = logging::logger("record");
        RecordResult result { .path = settings_.path, .frames = frames_, .samples = sound_frames_, .sample_rate = sample_rate_, .length = { static_cast<f32>(static_cast<f64>(frames_) / settings_.playback_fps) }, .problem = std::move(problem) };
        if (encoder_ && pclose(encoder_.release()) != 0 && !result.problem)
            result.problem = std::format("recording: {} failed encoding the frames", settings_.ffmpeg.string());
        if (wav_)
        {
            const auto header = wav_header(sample_rate_, channels_, static_cast<u32>(sound_frames_ * channels_ * sizeof(i16)));
            std::fseek(wav_.get(), 0, SEEK_SET);
            std::fwrite(header.data(), 1, header.size(), wav_.get());
            wav_.reset();
        }
        // In slow motion the sound is played slower by the same share, which lowers it.
        const f64 speed = playback_speed(settings_);
        const std::string slowed = speed == 1 ? std::string() : std::format(" -af asetrate={},aresample={}", std::lround(sample_rate_ * speed), sample_rate_);
        const std::string sound_in = settings_.sound ? std::format(" -i {}", shell_quoted(sound_part().string())) : std::string();
        const std::string sound_out = settings_.sound ? std::format(" -c:a aac -b:a 192k{}", slowed) : std::string();
        if (settings_.ffmpeg.empty())
        {
            result.path = frames_folder();
            const std::string join = std::format("ffmpeg -framerate {} -i {}{} -c:v libx264 -pix_fmt yuv420p{} {}", settings_.playback_fps, shell_quoted((frames_folder() / "%06d.png").string()), sound_in, sound_out, shell_quoted(settings_.path.string()));
            log.warning("recording: no ffmpeg, so {} frames are kept as PNG files in {}; join them with: {}", frames_, frames_folder().string(), join);
            return result;
        }
        if (frames_ == 0)
        {
            std::error_code ec;
            std::filesystem::remove(sound_part(), ec);
            result.problem = result.problem.value_or("recording: no frames");
            log.warning("{}", *result.problem);
            return result;
        }
        const std::string join = std::format("{} -loglevel error -y -i {}{} -map 0:v{} -c:v copy{} {}", shell_quoted(settings_.ffmpeg.string()), shell_quoted(video_part().string()), sound_in, settings_.sound ? " -map 1:a" : "", sound_out, shell_quoted(settings_.path.string()));
        if (result.problem || std::system(shell_line(join).c_str()) != 0)
        {
            result.problem = result.problem.value_or("recording: ffmpeg could not join the video with its sound");
            log.warning("{}; the parts are kept, join them with: {}", *result.problem, join);
            return result;
        }
        std::error_code ec;
        std::filesystem::remove(video_part(), ec);
        std::filesystem::remove(sound_part(), ec);
        log.info("recorded {}: {} frames, {:.2f} s", settings_.path.string(), frames_, result.length.value);
        return result;
    }

} // namespace rct3::video
