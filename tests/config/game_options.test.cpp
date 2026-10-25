// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game_options.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <set>

using namespace rct3::config;
namespace fs = std::filesystem;

TEST_CASE("every game option is registered once, in id order, its default in range")
{
    const auto table = game_options_table();
    REQUIRE(table.size() == kGameOptionCount);
    std::set<std::string_view> names;
    const GameOptions defaults;
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        CHECK(static_cast<std::size_t>(table[i].option) == i);
        CHECK(names.insert(table[i].name).second);
        CHECK(table[i].minimum <= table[i].default_value);
        CHECK(table[i].default_value <= table[i].maximum);
        CHECK(defaults.get(table[i].option) == table[i].default_value);
    }
}

TEST_CASE("values are kept in range and on whole steps")
{
    GameOptions options;
    options.set(GameOption::Shadows, 7);
    CHECK(options.choice(GameOption::Shadows) == 2);
    options.set(GameOption::ImposterDistance, 512);
    CHECK(options.get(GameOption::ImposterDistance) == doctest::Approx(510));
    options.set(GameOption::Brightness, 0.37f);
    CHECK(options.get(GameOption::Brightness) == doctest::Approx(0.37f));
    options.set(GameOption::IsometricRotation, 0);
    CHECK(options.get(GameOption::IsometricRotation) == doctest::Approx(0.05f));
}

TEST_CASE("the file holds what differs from the defaults and reads back the same")
{
    GameOptions options;
    CHECK(options.text() == "{}\n");
    options.set(GameOption::Fullscreen, 1);
    options.set(GameOption::MusicVolume, 0.25f);
    const std::string text = options.text();
    CHECK(text == "{\n    \"fullscreen\": 1,\n    \"music_volume\": 0.25\n}\n");
    CHECK(GameOptions::parse(text) == options);
    CHECK(GameOptions::parse(R"({ "fullscreen": true })") == GameOptions::parse(R"({ "fullscreen": 1 })"));
}

TEST_CASE("the park's music is kept by file name, empty for none")
{
    GameOptions options;
    CHECK_FALSE(options.background_music());
    options.set_background_music("Track One.wma");
    CHECK(GameOptions::parse(options.text()).background_music() == "Track One.wma");
    options.set_background_music("");
    CHECK(GameOptions::parse(options.text()).background_music() == "");
    options.set(GameOption::ShuffleBackgroundMusic, 1);
    CHECK(GameOptions::parse(options.text()) == options);

    std::vector<FileProblem> problems;
    CHECK_FALSE(GameOptions::parse(R"({ "background_music": 3 })", problems).background_music());
    CHECK(problems.size() == 1);
}

TEST_CASE("bad values are reported and skipped")
{
    std::vector<FileProblem> problems;
    const auto options = GameOptions::parse(R"({ "clouds": 0, "fog": 1, "shadows": "lots" })", problems);
    CHECK_FALSE(options.flag(GameOption::Clouds));
    CHECK(options.choice(GameOption::Shadows) == 2);
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].message.find("fog") != std::string::npos);
    CHECK(problems[1].message.find("shadows") != std::string::npos);

    problems.clear();
    CHECK(GameOptions::parse("clouds = 0", problems) == GameOptions { });
    CHECK(problems.size() == 1);
}

TEST_CASE("retired options are skipped without a word")
{
    std::vector<FileProblem> problems;
    CHECK(GameOptions::parse(R"({ "live_content": 0 })", problems) == GameOptions { });
    CHECK(problems.empty());
}

TEST_CASE("options are saved to and loaded from a file")
{
    const fs::path folder = fs::temp_directory_path() / "rct3pp-game-options-test";
    fs::remove_all(folder);
    GameOptions options;
    options.set(GameOption::CameraMode, 1);
    REQUIRE(options.save(folder / "options.json"));
    const auto loaded = GameOptions::load(folder / "options.json");
    REQUIRE(loaded);
    CHECK(*loaded == options);
    CHECK(GameOptions::load(folder / "missing.json") == GameOptions { });
    fs::remove_all(folder);
}
