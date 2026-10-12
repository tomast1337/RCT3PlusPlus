// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/settings.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace rct3::config;
namespace fs = std::filesystem;

TEST_CASE("every setting is registered once, in id order, with a description")
{
    const auto table = settings_table();
    REQUIRE(table.size() == kSettingCount);
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        CHECK(static_cast<std::size_t>(table[i].setting) == i);
        CHECK_FALSE(table[i].description.empty());
        CHECK(names.insert(table[i].name).second);
    }
    CHECK(setting_name(Setting::SkipIntro) == "skip_intro");
}

TEST_CASE("a file sets values by name, ignoring comments, blanks and spaces around them")
{
    const auto settings = Settings::parse("# a comment\n\n  game_path =  /games/park  \r\nskip_intro=yes\n");
    REQUIRE(settings.get(Setting::GamePath).has_value());
    CHECK(*settings.get(Setting::GamePath) == "/games/park");
    CHECK(settings.path(Setting::GamePath) == fs::path("/games/park"));
    CHECK(settings.flag(Setting::SkipIntro));
    CHECK(settings.is_set(Setting::SkipIntro));
    CHECK_FALSE(settings.is_set(Setting::InterfaceScale));

    SUBCASE("a later line wins")
    {
        CHECK_FALSE(Settings::parse("skip_intro = yes\nskip_intro = no\n").flag(Setting::SkipIntro));
    }
    SUBCASE("a value may contain '='")
    {
        CHECK(*Settings::parse("game_path = /a=b\n").get(Setting::GamePath) == "/a=b");
    }
}

TEST_CASE("unset settings give their defaults")
{
    const Settings settings;
    CHECK_FALSE(settings.get(Setting::GamePath).has_value());
    CHECK_FALSE(settings.flag(Setting::SkipIntro));
    REQUIRE(settings.get(Setting::InterfaceScale).has_value());
    CHECK(*settings.get(Setting::InterfaceScale) == "auto");

    Settings changed = Settings::parse("skip_intro = yes\n");
    changed.set(Setting::SkipIntro, "");
    CHECK_FALSE(changed.is_set(Setting::SkipIntro));
}

TEST_CASE("bad lines are reported and skipped, the rest still apply")
{
    std::vector<SettingsProblem> problems;
    const auto settings = Settings::parse("no equals sign\nfuture_option = 3\nskip_intro = yes\n", &problems);
    CHECK(settings.flag(Setting::SkipIntro));
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].line == 1);
    CHECK(problems[1].line == 2);
    CHECK(problems[1].message.find("future_option") != std::string::npos);
}

TEST_CASE("the written file reads back the same, keeping unknown settings")
{
    Settings settings = Settings::parse("future_option = 3\n");
    settings.set(Setting::GamePath, "/games/park");
    settings.set(Setting::InterfaceScale, "2");
    const std::string text = settings.text();

    CHECK(text.find("\ngame_path = /games/park\n") != std::string::npos);
    CHECK(text.find("\n# skip_intro = no\n") != std::string::npos); // At its default: commented out
    CHECK(text.find("\nfuture_option = 3\n") != std::string::npos);

    std::vector<SettingsProblem> problems;
    const auto again = Settings::parse(text, &problems);
    for (const SettingInfo& info : settings_table())
    {
        CHECK(again.is_set(info.setting) == settings.is_set(info.setting));
        CHECK(again.get(info.setting) == settings.get(info.setting));
    }
    CHECK(again.text() == text);
    CHECK(problems.size() == 1); // Only the unknown one
}

TEST_CASE("settings are saved to and loaded from a file")
{
    const fs::path dir = fs::temp_directory_path() / "rct3pp_settings_test";
    fs::remove_all(dir);
    const fs::path file = dir / "sub" / "settings.ini";

    CHECK(Settings::load(file).has_value()); // Not there yet: defaults

    Settings settings;
    settings.set(Setting::SkipIntro, "yes");
    REQUIRE(settings.save(file).has_value());
    CHECK_FALSE(fs::exists(file.string() + ".new"));
    const auto loaded = Settings::load(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->flag(Setting::SkipIntro));

    SUBCASE("a folder in its place is an error")
    {
        CHECK_FALSE(Settings::load(dir / "sub").has_value());
    }
    fs::remove_all(dir);
}

TEST_CASE("the settings file lives in the user's configuration folder")
{
    using E = EnvVar;
    CHECK(settings_path(Environment::from_values({ { E::SettingsFile, "/a/mine.ini" }, { E::Home, "/users/someone" } })) == fs::path("/a/mine.ini"));
#ifndef _WIN32
    CHECK(settings_path(Environment::from_values({ { E::ConfigHome, "/cfg" }, { E::Home, "/users/someone" } })) == fs::path("/cfg/rct3pp/settings.ini"));
    CHECK(settings_path(Environment::from_values({ { E::ConfigHome, "relative" }, { E::Home, "/users/someone" } })) == fs::path("/users/someone/.config/rct3pp/settings.ini"));
    CHECK(settings_path(Environment::from_values({ { E::Home, "/users/someone" } })) == fs::path("/users/someone/.config/rct3pp/settings.ini"));
    CHECK_FALSE(settings_path(Environment()).has_value());
#endif
}

TEST_CASE("interface scale is auto or a whole number from 1 to 8")
{
    const auto scale = [](std::string_view value) {
        const auto parsed = parse_interface_scale(value);
        REQUIRE(parsed.has_value());
        return *parsed;
    };
    CHECK_FALSE(scale("auto").has_value());
    CHECK_FALSE(scale("AUTO").has_value());
    CHECK_FALSE(scale("").has_value());
    CHECK(scale("2") == 2.0f);
    CHECK(scale(" 8 ") == 8.0f);
    for (const char* bad : { "0", "9", "1.5", "-1", "two", "2x" })
        CHECK_FALSE(parse_interface_scale(bad).has_value());
}
