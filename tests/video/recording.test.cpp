// SPDX-License-Identifier: GPL-3.0-or-later
#include "video/recording.hpp"

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

using namespace rct3::video;
namespace fs = std::filesystem;

namespace
{
    u32 read_u32(std::span<const u8> bytes, usize at)
    {
        return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<u32>(bytes[at + 3]) << 24);
    }
    u16 read_u16(std::span<const u8> bytes, usize at)
    {
        return static_cast<u16>(bytes[at] | (bytes[at + 1] << 8));
    }
} // namespace

TEST_CASE("Each video frame gets its share of the sound, the rest carried on so none is lost or added")
{
    struct Rates
    {
        u32 sample_rate;
        u32 fps;
    };
    for (const Rates r : { Rates { 48000, 30 }, Rates { 44100, 60 }, Rates { 48000, 144 }, Rates { 44100, 7 }, Rates { 48000, 1000 } })
    {
        CAPTURE(r.sample_rate);
        CAPTURE(r.fps);
        const u64 frames = 10u * r.fps + 3; // Ten game seconds and a bit
        u64 total = 0;
        for (u64 f = 0; f < frames; ++f)
        {
            const u64 share = sound_frames_before(f + 1, r.sample_rate, r.fps) - sound_frames_before(f, r.sample_rate, r.fps);
            CHECK(share >= r.sample_rate / r.fps);
            CHECK(share <= r.sample_rate / r.fps + 1);
            total += share;
            if ((f + 1) % r.fps == 0)
                CHECK(total == (f + 1) / r.fps * r.sample_rate); // Every whole second is exactly a second of sound
        }
        CHECK(total == sound_frames_before(frames, r.sample_rate, r.fps));
    }
    CHECK(sound_frames_before(1, 48000, 30) == 1600);
    CHECK(sound_frames_before(1, 44100, 60) == 735);
}

TEST_CASE("While recording, a frame moves the game on by one frame of the video, whatever the wall time")
{
    CHECK(frame_step(30u, FramePace::Wall, { 0.5f }).value == doctest::Approx(1.0 / 30));
    CHECK(frame_step(240u, FramePace::Script, { 0.001f }).value == doctest::Approx(1.0 / 240));
    CHECK(frame_step(std::nullopt, FramePace::Script, { 0.5f }).value == doctest::Approx(1.0 / 60));
    CHECK(frame_step(std::nullopt, FramePace::Wall, { 0.25f }).value == doctest::Approx(0.25));
}

TEST_CASE("The WAV header describes 16-bit PCM and its data's size")
{
    const auto h = wav_header(44100, 2, 1000);
    CHECK(std::memcmp(h.data(), "RIFF", 4) == 0);
    CHECK(read_u32(h, 4) == 36 + 1000);
    CHECK(std::memcmp(h.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(read_u32(h, 16) == 16);
    CHECK(read_u16(h, 20) == 1);
    CHECK(read_u16(h, 22) == 2);
    CHECK(read_u32(h, 24) == 44100);
    CHECK(read_u32(h, 28) == 44100 * 4);
    CHECK(read_u16(h, 32) == 4);
    CHECK(read_u16(h, 34) == 16);
    CHECK(std::memcmp(h.data() + 36, "data", 4) == 0);
    CHECK(read_u32(h, 40) == 1000);
}

TEST_CASE("Mixed samples become 16-bit ones, clipped")
{
    CHECK(pcm16_sample(0.0f) == 0);
    CHECK(pcm16_sample(1.0f) == 32767);
    CHECK(pcm16_sample(-1.0f) == -32767);
    CHECK(pcm16_sample(3.0f) == 32767);
    CHECK(pcm16_sample(-3.0f) == -32767);
    CHECK(pcm16_sample(0.5f) == 16384);
}

TEST_CASE("Without ffmpeg, a recording keeps its frames as PNG files and its sound as a WAV file")
{
    const fs::path folder = fs::temp_directory_path() / "rct3pp_recording_test";
    fs::remove_all(folder);
    auto recorder = Recorder::start({ .path = folder / "clip.mp4", .fps = 30, .playback_fps = 30, .sound = true, .ffmpeg = { } }, 48000, 2);
    REQUIRE(recorder.has_value());
    // An odd size is cut to an even one.
    const std::vector<u8> pixels(3 * 3 * 4, 200);
    std::vector<f32> sound;
    for (i32 f = 0; f < 3; ++f)
    {
        REQUIRE(recorder->add_frame(3, 3, pixels).has_value());
        const usize due = recorder->take_sound_frames();
        CHECK(due == 1600);
        sound.assign(due * 2, f == 1 ? 0.5f : 0.0f);
        recorder->add_sound(sound);
    }
    const RecordResult result = recorder->finish();
    CHECK(result.frames == 3);
    CHECK(result.samples == 3 * 1600);
    CHECK(result.length.value == doctest::Approx(0.1));
    CHECK(fs::exists(folder / "clip_frames" / "000000.png"));
    CHECK(fs::exists(folder / "clip_frames" / "000002.png"));
    CHECK_FALSE(fs::exists(folder / "clip_frames" / "000003.png"));

    std::ifstream wav(folder / "clip.wav", std::ios::binary);
    const std::vector<u8> bytes { std::istreambuf_iterator<char>(wav), { } };
    REQUIRE(bytes.size() == kWavHeaderBytes + 3 * 1600 * 2 * 2);
    CHECK(read_u32(bytes, 40) == 3 * 1600 * 2 * 2);
    CHECK(read_u16(bytes, kWavHeaderBytes) == 0);
    const usize second_frame = kWavHeaderBytes + 1600 * 2 * 2;
    CHECK(static_cast<i16>(read_u16(bytes, second_frame)) == 16384);
    CHECK(static_cast<i16>(read_u16(bytes, second_frame - 2)) == 0);
    fs::remove_all(folder);
}

TEST_CASE("A recording takes from 1 to the most frames a game second")
{
    CHECK_FALSE(Recorder::start({ .path = "x.mp4", .fps = 0 }, 48000, 2).has_value());
    CHECK_FALSE(Recorder::start({ .path = "x.mp4", .fps = kMaxCaptureFps + 1 }, 48000, 2).has_value());
}
