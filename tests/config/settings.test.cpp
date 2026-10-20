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

TEST_CASE("a file sets values by name")
{
    const auto settings = Settings::parse(R"({ "game_path": "/games/park", "skip_intro": true, "interface_scale": null })");
    REQUIRE(settings.get(Setting::GamePath).has_value());
    CHECK(*settings.get(Setting::GamePath) == "/games/park");
    CHECK(settings.path(Setting::GamePath) == fs::path("/games/park"));
    CHECK(settings.flag(Setting::SkipIntro));
    CHECK(settings.is_set(Setting::SkipIntro));
    CHECK_FALSE(settings.is_set(Setting::InterfaceScale));

    SUBCASE("flags and scales may be written as text")
    {
        const auto text = Settings::parse(R"({ "skip_intro": "yes", "interface_scale": "2" })");
        CHECK(text.flag(Setting::SkipIntro));
        CHECK(*text.get(Setting::InterfaceScale) == "2");
    }
    SUBCASE("an empty file is no settings")
    {
        CHECK_FALSE(Settings::parse(" \n").is_set(Setting::SkipIntro));
    }
}

TEST_CASE("unset settings give their defaults")
{
    const Settings settings;
    CHECK_FALSE(settings.get(Setting::GamePath).has_value());
    CHECK_FALSE(settings.flag(Setting::SkipIntro));
    REQUIRE(settings.get(Setting::InterfaceScale).has_value());
    CHECK(*settings.get(Setting::InterfaceScale) == "auto");

    Settings changed = Settings::parse(R"({ "skip_intro": true })");
    changed.set(Setting::SkipIntro, "");
    CHECK_FALSE(changed.is_set(Setting::SkipIntro));
}

TEST_CASE("bad values are reported and skipped, the rest still apply")
{
    std::vector<FileProblem> problems;
    const auto settings = Settings::parse(R"({ "game_path": 3, "future_option": 3, "skip_intro": true, "interface_scale": 1.5 })", problems);
    CHECK(settings.flag(Setting::SkipIntro));
    CHECK_FALSE(settings.is_set(Setting::GamePath));
    CHECK_FALSE(settings.is_set(Setting::InterfaceScale));
    REQUIRE(problems.size() == 3);
    CHECK(problems[1].message.find("future_option") != std::string::npos);

    SUBCASE("a file that is not a JSON object is reported as a whole")
    {
        problems.clear();
        CHECK_FALSE(Settings::parse("skip_intro = yes\n", problems).is_set(Setting::SkipIntro));
        CHECK(problems.size() == 1);
        problems.clear();
        (void)Settings::parse("[1, 2]", problems);
        CHECK(problems.size() == 1);
    }
}

TEST_CASE("the written file reads back the same, keeping unknown settings")
{
    Settings settings = Settings::parse(R"({ "future_option": { "a": [1, 2] } })");
    settings.set(Setting::GamePath, "/games/park");
    settings.set(Setting::SkipIntro, "yes");
    settings.set(Setting::InterfaceScale, "2");
    const std::string text = settings.text();

    CHECK(text.find(R"("game_path": "/games/park",)") != std::string::npos);
    CHECK(text.find(R"("skip_intro": true,)") != std::string::npos);
    CHECK(text.find(R"("interface_scale": 2,)") != std::string::npos);
    CHECK(text.find(R"("language": null,)") != std::string::npos); // At its default
    CHECK(text.find(R"("future_option": {)") != std::string::npos);

    std::vector<FileProblem> problems;
    const auto again = Settings::parse(text, problems);
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
    const fs::path file = dir / "sub" / "settings.json";

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
