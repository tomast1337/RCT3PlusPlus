// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/music_player.hpp"

#include "sine_wma.hpp"

#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace rct3::audio;
namespace fs = std::filesystem;

TEST_CASE("music plays round and round from its file")
{
    const fs::path path = fs::temp_directory_path() / "rct3pp_music.wma";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(rct3::test::kSineWma.data()), static_cast<std::streamsize>(rct3::test::kSineWma.size()));

    auto engine = AudioEngine::create({ .backend = Backend::Offline, .sample_rate = 48000, .channels = 2 });
    REQUIRE(engine);
    auto music = MusicPlayer::start(*engine, path, 1.0f);
    REQUIRE(music);

    // Four times the file's length, a frame at a time.
    std::vector<float> frame(2 * 800);
    float late_peak = 0;
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(music->update(*engine));
        CHECK(engine->render(frame) == 800);
        if (i >= 50)
        {
            for (const float v : frame)
                late_peak = std::max(late_peak, std::abs(v));
        }
    }
    CHECK(music->playing(*engine));
    CHECK(late_peak > 0.05f);

    CHECK(music->set_volume(*engine, 0.5f));
    music->stop(*engine);
    CHECK_FALSE(music->playing(*engine));
    CHECK_FALSE(MusicPlayer::start(*engine, fs::temp_directory_path() / "rct3pp_no_music.wma", 1.0f));

    // Once, as a movie's sound: over by four times the file's length.
    auto once = MusicPlayer::start(*engine, path, 1.0f, false);
    REQUIRE(once);
    for (int i = 0; i < 60; ++i)
    {
        REQUIRE(once->update(*engine));
        (void)engine->render(frame);
    }
    CHECK_FALSE(once->playing(*engine));
    fs::remove(path);
}
