// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic audio only. Mixing is checked with the offline backend, which needs no device.
#include "audio/audio.hpp"

#include "../ovl/builder.hpp"
#include "ovl/overlay.hpp"
#include "ovl/sound.hpp"

#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

using namespace rct3::audio;

namespace
{

    constexpr std::uint32_t kRate = 48000;

    EngineConfig offline(std::uint32_t voices = 8)
    {
        EngineConfig c;
        c.backend = Backend::Offline;
        c.sample_rate = kRate;
        c.channels = 2;
        c.max_voices = voices;
        return c;
    }

    AudioEngine make_engine(const EngineConfig& c = offline())
    {
        auto e = AudioEngine::create(c);
        REQUIRE(e.has_value());
        return std::move(*e);
    }

    // `frames` of a constant mono 16-bit signal. 16384 is exactly 0.5 in float.
    SoundBuffer constant_mono(std::int16_t value, std::size_t frames, std::uint32_t rate = kRate)
    {
        const std::vector<std::int16_t> s(frames, value);
        return make_pcm16(s, 1, rate);
    }

    // The mixer's pitch stage delays the signal by one frame, so assertions look at frame 4 (indices
    // kF, kF + 1) rather than frame 0.
    constexpr std::size_t kF = 8;

    std::vector<float> render_frames(AudioEngine& e, std::size_t frames)
    {
        std::vector<float> out(frames * e.channels(), -7.0f);
        CHECK(e.render(out) == frames);
        return out;
    }

    std::size_t count_nonzero_frames(const std::vector<float>& stereo)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i + 1 < stereo.size(); i += 2)
        {
            if (std::fabs(stereo[i]) > 1e-4f || std::fabs(stereo[i + 1]) > 1e-4f)
                ++n;
        }
        return n;
    }

} // namespace

TEST_CASE("buffers report frames and duration, and validate their shape")
{
    const std::vector<std::int16_t> s(960, 0);
    const SoundBuffer mono = make_pcm16(s, 1, 48000);
    CHECK(mono.valid());
    CHECK(mono.frame_count() == 960);
    CHECK(mono.duration_seconds() == doctest::Approx(0.02));

    const SoundBuffer stereo = make_pcm16(s, 2, 48000);
    CHECK(stereo.frame_count() == 480);

    const std::vector<float> f(10, 0.0f);
    const SoundBuffer fb = make_float(f, 2, 22050);
    CHECK(fb.valid());
    CHECK(fb.frame_count() == 5);
    CHECK(fb.data.size() == 40);

    SUBCASE("zero rate, zero channels, no data and partial frames are invalid")
    {
        SoundBuffer b = mono;
        b.sample_rate = 0;
        CHECK_FALSE(b.valid());
        b = mono;
        b.channels = 0;
        CHECK_FALSE(b.valid());
        b = mono;
        b.data.clear();
        CHECK_FALSE(b.valid());
        b = stereo;
        b.data.pop_back();
        CHECK_FALSE(b.valid());
        b = mono;
        b.channels = kMaxChannels + 1;
        CHECK_FALSE(b.valid());
    }
}

TEST_CASE("sound items convert to playable buffers")
{
    // Bytes of four 16-bit samples: 1, -2, 256, 32767 (little endian).
    const std::array<std::byte, 8> pcm16 { std::byte { 1 }, std::byte { 0 }, std::byte { 0xFE }, std::byte { 0xFF }, std::byte { 0 }, std::byte { 1 }, std::byte { 0xFF }, std::byte { 0x7F } };
    rct3::ovl::SoundInfo info;
    info.format_tag = 1;
    info.channels = 2;
    info.sample_rate = 22050;
    info.bits_per_sample = 16;
    info.samples = std::span<const std::byte>(pcm16);

    SUBCASE("16-bit PCM is copied")
    {
        const auto b = from_ovl_sound(info);
        REQUIRE(b.has_value());
        CHECK(b->format == SampleFormat::Pcm16);
        CHECK(b->channels == 2);
        CHECK(b->sample_rate == 22050);
        CHECK(b->frame_count() == 2);
        std::int16_t last = 0;
        std::memcpy(&last, b->data.data() + 6, 2);
        CHECK(last == 32767);
    }

    SUBCASE("8-bit PCM is unsigned and widened")
    {
        const std::array<std::byte, 3> u8 { std::byte { 128 }, std::byte { 255 }, std::byte { 0 } };
        info.bits_per_sample = 8;
        info.channels = 1;
        info.samples = std::span<const std::byte>(u8);
        const auto b = from_ovl_sound(info);
        REQUIRE(b.has_value());
        REQUIRE(b->frame_count() == 3);
        std::int16_t v[3];
        std::memcpy(v, b->data.data(), 6);
        CHECK(v[0] == 0);
        CHECK(v[1] == 127 * 256);
        CHECK(v[2] == -128 * 256);
    }

    SUBCASE("a trailing partial frame is dropped")
    {
        info.channels = 1;
        info.samples = std::span<const std::byte>(pcm16.data(), 7);
        const auto b = from_ovl_sound(info);
        REQUIRE(b.has_value());
        CHECK(b->frame_count() == 3);
    }

    SUBCASE("unusable descriptors are reported")
    {
        auto bad = info;
        bad.format_tag = 2;
        CHECK(from_ovl_sound(bad).error() == ConvertError::NotPcm);
        bad = info;
        bad.bits_per_sample = 24;
        CHECK(from_ovl_sound(bad).error() == ConvertError::UnsupportedDepth);
        bad = info;
        bad.channels = 0;
        CHECK(from_ovl_sound(bad).error() == ConvertError::BadChannelCount);
        bad = info;
        bad.sample_rate = 0;
        CHECK(from_ovl_sound(bad).error() == ConvertError::BadSampleRate);
        bad = info;
        bad.samples.reset();
        CHECK(from_ovl_sound(bad).error() == ConvertError::MissingSamples);
        bad = info;
        bad.samples = std::span<const std::byte>(pcm16.data(), 3);
        CHECK(from_ovl_sound(bad).error() == ConvertError::Empty);
    }
}

TEST_CASE("play parameters are range checked")
{
    CHECK(is_valid(PlayParams { }));
    CHECK(is_valid(PlayParams { 0.0f, -1.0f, 0.5f }));
    CHECK_FALSE(is_valid(PlayParams { -0.1f, 0.0f, 1.0f }));
    CHECK_FALSE(is_valid(PlayParams { 1.0f, 1.5f, 1.0f }));
    CHECK_FALSE(is_valid(PlayParams { 1.0f, 0.0f, 0.0f }));
    CHECK_FALSE(is_valid(PlayParams { 1.0f, 0.0f, kMaxPitch + 1.0f }));
    CHECK_FALSE(is_valid(PlayParams { std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f }));
    CHECK_FALSE(is_valid(PlayParams { 1.0f, std::numeric_limits<float>::infinity(), 1.0f }));
}

TEST_CASE("engine configuration is validated")
{
    EngineConfig c = offline();
    c.max_voices = 0;
    CHECK(AudioEngine::create(c).error() == EngineError::BadConfig);
    c = offline();
    c.max_voices = kMaxVoices + 1;
    CHECK(AudioEngine::create(c).error() == EngineError::BadConfig);
    c = offline();
    c.channels = 3;
    CHECK(AudioEngine::create(c).error() == EngineError::BadConfig);
    c = offline();
    c.sample_rate = 100;
    CHECK(AudioEngine::create(c).error() == EngineError::BadConfig);
    c = offline();
    c.master_volume = -1.0f;
    CHECK(AudioEngine::create(c).error() == EngineError::BadConfig);
}

TEST_CASE("play rejects bad input without starting a voice")
{
    AudioEngine e = make_engine();
    CHECK(e.play(SoundBuffer { }).error() == PlayError::InvalidBuffer);
    CHECK(e.play(constant_mono(100, 10), PlayParams { 2.0f, 5.0f, 1.0f }).error() == PlayError::InvalidParams);
    CHECK(e.active_voices() == 0);
}

TEST_CASE("an idle engine renders silence")
{
    AudioEngine e = make_engine();
    for (float v : render_frames(e, 64))
        CHECK(v == 0.0f);
}

TEST_CASE("volume scales the sound")
{
    AudioEngine e = make_engine();
    const auto h = e.play(constant_mono(16384, 256), PlayParams { 0.5f, 0.0f, 1.0f });
    REQUIRE(h.has_value());
    CHECK(e.is_playing(*h));
    const auto out = render_frames(e, 128);
    CHECK(out[kF] == doctest::Approx(0.25f));
    CHECK(out[kF + 1] == doctest::Approx(0.25f));
    CHECK(out[2 * 100] == doctest::Approx(0.25f));
}

TEST_CASE("pan is a balance: the far channel is attenuated, the near one is not")
{
    AudioEngine e = make_engine();

    SUBCASE("left")
    {
        REQUIRE(e.play(constant_mono(16384, 256), PlayParams { 1.0f, -1.0f, 1.0f }).has_value());
        const auto out = render_frames(e, 16);
        CHECK(out[kF] == doctest::Approx(0.5f));
        CHECK(out[kF + 1] == doctest::Approx(0.0f));
    }
    SUBCASE("right")
    {
        REQUIRE(e.play(constant_mono(16384, 256), PlayParams { 1.0f, 1.0f, 1.0f }).has_value());
        const auto out = render_frames(e, 16);
        CHECK(out[kF] == doctest::Approx(0.0f));
        CHECK(out[kF + 1] == doctest::Approx(0.5f));
    }
    SUBCASE("centre")
    {
        REQUIRE(e.play(constant_mono(16384, 256), PlayParams { 1.0f, 0.0f, 1.0f }).has_value());
        const auto out = render_frames(e, 16);
        CHECK(out[kF] == doctest::Approx(0.5f));
        CHECK(out[kF + 1] == doctest::Approx(0.5f));
    }
    SUBCASE("halfway to the right halves the left channel")
    {
        REQUIRE(e.play(constant_mono(16384, 256), PlayParams { 1.0f, 0.5f, 1.0f }).has_value());
        const auto out = render_frames(e, 16);
        CHECK(out[kF] == doctest::Approx(0.25f));
        CHECK(out[kF + 1] == doctest::Approx(0.5f));
    }
}

TEST_CASE("a placed sound is heard from the listener: on its side, and fading to silence with distance")
{
    AudioEngine e = make_engine();
    e.set_listener({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f });
    const auto at = [](glm::vec3 where) { return PlayParams { .at = where, .near = 2.0f, .far = 10.0f }; };
    // The mixer smooths a placed sound's gains over its first frames, so look further in.
    constexpr std::size_t kLate = 2 * 400;

    SUBCASE("to the right")
    {
        REQUIRE(e.play(constant_mono(16384, 1024), at({ 1.0f, 0.0f, 0.0f })).has_value());
        const auto out = render_frames(e, 512);
        CHECK(out[kLate + 1] > 0.0f);
        CHECK(out[kLate] < out[kLate + 1]);
    }
    SUBCASE("to the left")
    {
        REQUIRE(e.play(constant_mono(16384, 1024), at({ -1.0f, 0.0f, 0.0f })).has_value());
        const auto out = render_frames(e, 512);
        CHECK(out[kLate] > out[kLate + 1]);
    }
    SUBCASE("halfway out is half as loud as near, and past the far end silent")
    {
        const auto near = e.play(constant_mono(16384, 1024), at({ 0.0f, 0.0f, -1.0f }));
        REQUIRE(near.has_value());
        const float loud = render_frames(e, 512)[kLate];
        e.stop(*near);
        REQUIRE(e.play(constant_mono(16384, 1024), at({ 0.0f, 0.0f, -6.0f })).has_value());
        CHECK(render_frames(e, 512)[kLate] == doctest::Approx(loud / 2).epsilon(0.05));
        e.stop_all();
        const auto far = e.play(constant_mono(16384, 1024), at({ 0.0f, 0.0f, -11.0f }));
        REQUIRE(far.has_value());
        CHECK(render_frames(e, 512)[kLate] == doctest::Approx(0.0f));
        CHECK(e.set_voice_place(*far, { 0.0f, 0.0f, -1.0f }));
        CHECK(render_frames(e, 512)[kLate] > 0.1f);
    }
    SUBCASE("an unplaced voice cannot be moved")
    {
        const auto h = e.play(constant_mono(16384, 1024));
        REQUIRE(h.has_value());
        CHECK_FALSE(e.set_voice_place(*h, { 1.0f, 0.0f, 0.0f }));
    }
    CHECK_FALSE(is_valid(PlayParams { .at = glm::vec3 { 0.0f }, .near = 5.0f, .far = 5.0f }));
}

TEST_CASE("concurrent voices are summed and master volume scales the sum")
{
    AudioEngine e = make_engine();
    REQUIRE(e.play(constant_mono(8192, 256)).has_value());  // 0.25
    REQUIRE(e.play(constant_mono(16384, 256)).has_value()); // 0.5
    CHECK(e.active_voices() == 2);

    SUBCASE("sum")
    {
        const auto out = render_frames(e, 8);
        CHECK(out[kF] == doctest::Approx(0.75f));
    }
    SUBCASE("master volume")
    {
        CHECK(e.set_master_volume(0.5f));
        CHECK(e.master_volume() == 0.5f);
        const auto out = render_frames(e, 8);
        CHECK(out[kF] == doctest::Approx(0.375f));
        CHECK_FALSE(e.set_master_volume(-1.0f));
        CHECK(e.master_volume() == 0.5f);
    }
}

TEST_CASE("the voice cap is hard and finished voices make room")
{
    AudioEngine e = make_engine(offline(2));
    const auto a = e.play(constant_mono(1000, 100));
    const auto b = e.play(constant_mono(1000, 100000));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(e.max_voices() == 2);
    CHECK(e.play(constant_mono(1000, 100)).error() == PlayError::VoiceLimit);
    CHECK(e.active_voices() == 2);

    // Let the short sound run out.
    render_frames(e, 512);
    CHECK_FALSE(e.is_playing(*a));
    CHECK(e.is_playing(*b));
    CHECK(e.active_voices() == 1);

    const auto c = e.play(constant_mono(1000, 100));
    REQUIRE(c.has_value());
    CHECK_FALSE(*c == *a); // slot reused, handle is not
    CHECK_FALSE(e.set_voice_volume(*a, 0.5f));
    CHECK(e.set_voice_volume(*c, 0.5f));
}

TEST_CASE("stop silences a voice and invalidates its handle")
{
    AudioEngine e = make_engine();
    const auto a = e.play(constant_mono(16384, 4096));
    const auto b = e.play(constant_mono(8192, 4096));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(e.stop(*a));
    CHECK_FALSE(e.stop(*a));
    CHECK_FALSE(e.is_playing(*a));
    CHECK(e.active_voices() == 1);
    CHECK_FALSE(e.set_voice_pan(*a, 0.0f));

    const auto out = render_frames(e, 8);
    CHECK(out[kF] == doctest::Approx(0.25f));

    e.stop_all();
    CHECK(e.active_voices() == 0);
    for (float v : render_frames(e, 8))
        CHECK(v == 0.0f);
    CHECK_FALSE(e.stop(VoiceHandle { }));
}

TEST_CASE("per-voice volume and pan change while playing")
{
    AudioEngine e = make_engine();
    const auto h = e.play(constant_mono(16384, 4096));
    REQUIRE(h.has_value());
    CHECK(e.set_voice_volume(*h, 0.5f));
    CHECK(e.set_voice_pan(*h, 1.0f));
    CHECK_FALSE(e.set_voice_volume(*h, -2.0f));
    CHECK_FALSE(e.set_voice_pan(*h, 2.0f));
    CHECK_FALSE(e.set_voice_pitch(*h, 0.0f));
    const auto out = render_frames(e, 8);
    CHECK(out[kF] == doctest::Approx(0.0f));
    CHECK(out[kF + 1] == doctest::Approx(0.25f));
}

TEST_CASE("stereo sources keep their channels and float buffers play")
{
    AudioEngine e = make_engine();
    std::vector<float> s;
    for (int i = 0; i < 64; ++i)
    {
        s.push_back(0.5f);
        s.push_back(-0.25f);
    }
    REQUIRE(e.play(make_float(s, 2, kRate)).has_value());
    const auto out = render_frames(e, 16);
    CHECK(out[kF] == doctest::Approx(0.5f));
    CHECK(out[kF + 1] == doctest::Approx(-0.25f));
}

TEST_CASE("a source at another rate plays for the right time")
{
    AudioEngine e = make_engine();
    // 1000 frames at half the mixing rate last 2000 mixed frames.
    REQUIRE(e.play(constant_mono(16384, 1000, kRate / 2)).has_value());
    const auto out = render_frames(e, 4000);
    const std::size_t n = count_nonzero_frames(out);
    CHECK(n >= 1990);
    CHECK(n <= 2010);
}

TEST_CASE("pitch changes the playing time")
{
    AudioEngine e = make_engine();
    PlayParams p;
    p.pitch = 2.0f;
    REQUIRE(e.play(constant_mono(16384, 2000), p).has_value());
    const auto out = render_frames(e, 4000);
    const std::size_t n = count_nonzero_frames(out);
    CHECK(n >= 990);
    CHECK(n <= 1010);
}

TEST_CASE("the null backend runs without a device and cannot be rendered by hand")
{
    EngineConfig c;
    c.backend = Backend::Null;
    auto e = AudioEngine::create(c);
    REQUIRE(e.has_value());
    CHECK(e->backend() == Backend::Null);
    const auto h = e->play(constant_mono(1000, 480));
    REQUIRE(h.has_value());
    CHECK(e->stop(*h));
    std::vector<float> scratch(16);
    CHECK(e->render(scratch) == 0);
}

TEST_CASE("the default backend falls back to the null backend when there is no device")
{
    EngineConfig c;
    c.backend = Backend::Default;
    c.fall_back_to_null = true;
    auto e = AudioEngine::create(c);
    REQUIRE(e.has_value());
    CHECK((e->backend() == Backend::Default || e->backend() == Backend::Null));
}

TEST_CASE("engines are movable and shut down with voices running")
{
    AudioEngine a = make_engine();
    REQUIRE(a.play(constant_mono(1000, 4096)).has_value());
    AudioEngine b = std::move(a);
    CHECK(b.active_voices() == 1);
}

TEST_CASE("a sound item read from an overlay plays through the mixer")
{
    using namespace rct3::test;
    // 64 mono 16-bit samples of the value 0x2000 (0.25) after the name "Tone:snd".
    std::vector<std::byte> strings = bytes_of_string(std::string("Tone:snd\0", 9));
    for (int i = 0; i < 64; ++i)
        append_u16(strings, 0x2000);

    std::vector<std::byte> info;
    append_u16(info, 1); // PCM
    append_u16(info, 1); // mono
    append_u32(info, kRate);
    append_u32(info, kRate * 2);
    append_u16(info, 2);
    append_u16(info, 16);
    for (int i = 0; i < 2 + 9 + 1; ++i)
        append_u32(info, 0);
    append_u32(info, 9);   // data address
    append_u32(info, 128); // data size
    append_u32(info, 0);
    append_u32(info, 0);
    REQUIRE(info.size() == rct3::ovl::kSoundDescriptorSize);

    std::vector<std::byte> row;
    append_u32(row, 0);
    append_u32(row, static_cast<std::uint32_t>(strings.size()));
    append_u16(row, 1);
    append_u16(row, 0xFFFF);
    append_u32(row, 0);

    auto overlay = rct3::ovl::Overlay::from_bytes(build_file(5, { { strings }, { info }, { row } }, { }), std::nullopt);
    REQUIRE(overlay.has_value());
    const auto items = rct3::ovl::read_sounds(*overlay, overlay->common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].info.has_value());
    const auto buffer = from_ovl_sound(*items[0].info);
    REQUIRE(buffer.has_value());
    CHECK(buffer->frame_count() == 64);

    AudioEngine e = make_engine();
    REQUIRE(e.play(*buffer).has_value());
    const auto out = render_frames(e, 16);
    CHECK(out[kF] == doctest::Approx(0.25f));
    CHECK(out[kF + 1] == doctest::Approx(0.25f));
}

TEST_CASE("a stream plays what it is fed, is silent when dry and ends when told")
{
    AudioEngine e = make_engine();
    REQUIRE_FALSE(e.play_stream({ .channels = 0 }));
    REQUIRE_FALSE(e.play_stream({ .buffer_frames = 0 }));
    const auto h = e.play_stream({ .channels = 2, .sample_rate = kRate, .buffer_frames = 512 }, PlayParams { 0.5f, 0.0f, 1.0f });
    REQUIRE(h.has_value());
    CHECK(e.stream_space(*h) == 512);

    const std::vector<float> half(2 * 600, 0.5f);
    CHECK(e.feed(*h, half) == 512); // As much as fits
    CHECK(e.stream_space(*h) == 0);
    auto out = render_frames(e, 256);
    CHECK(out[2 * 100] == doctest::Approx(0.25f));
    CHECK(e.stream_space(*h) >= 256);

    out = render_frames(e, 1024); // Runs dry
    CHECK(out[2 * 1000] == 0.0f);
    CHECK(e.is_playing(*h));
    CHECK(e.feed(*h, std::span(half).first(2 * 64)) == 64);
    out = render_frames(e, 1024); // The mixer reads a little ahead, so they come out a little later
    CHECK(count_nonzero_frames(out) == 64);

    CHECK(e.end_stream(*h));
    CHECK(e.feed(*h, half) == 0);
    render_frames(e, 1024);
    CHECK_FALSE(e.is_playing(*h));

    const auto one_shot = e.play(constant_mono(16384, 64));
    REQUIRE(one_shot.has_value());
    CHECK(e.stream_space(*one_shot) == 0); // Not a stream
    CHECK_FALSE(e.end_stream(*one_shot));
}
