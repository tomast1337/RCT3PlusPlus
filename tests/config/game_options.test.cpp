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
    CHECK(options.text().find('\n') == options.text().size() - 1); // The heading only
    options.set(GameOption::Fullscreen, 1);
    options.set(GameOption::MusicVolume, 0.25f);
    const std::string text = options.text();
    CHECK(text.find("fullscreen = 1\n") != std::string::npos);
    CHECK(text.find("music_volume = 0.25\n") != std::string::npos);
    CHECK(GameOptions::parse(text) == options);
}

TEST_CASE("bad lines are reported and skipped")
{
    std::vector<GameOptionsProblem> problems;
    const auto options = GameOptions::parse("clouds = 0\nfog = 1\nshadows = lots\nno equals\n", &problems);
    CHECK_FALSE(options.flag(GameOption::Clouds));
    CHECK(options.choice(GameOption::Shadows) == 2);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0].line == 2);
    CHECK(problems[1].line == 3);
    CHECK(problems[2].line == 4);
}

TEST_CASE("options are saved beside the settings file")
{
    CHECK(game_options_path(Environment::from_values({ { EnvVar::SettingsFile, "/config/rct3pp/settings.ini" } })) == fs::path("/config/rct3pp/options.ini"));

    const fs::path folder = fs::temp_directory_path() / "rct3pp-game-options-test";
    fs::remove_all(folder);
    GameOptions options;
    options.set(GameOption::CameraMode, 1);
    REQUIRE(options.save(folder / "options.ini"));
    const auto loaded = GameOptions::load(folder / "options.ini");
    REQUIRE(loaded);
    CHECK(*loaded == options);
    CHECK(GameOptions::load(folder / "missing.ini") == GameOptions { });
    fs::remove_all(folder);
}
