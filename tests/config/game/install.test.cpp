// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game install checks against a fake install built in a temp directory: empty folders and files with
// invented names, no game data.
#include "config/game/install.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <format>
#include <fstream>

using namespace rct3::config;
using namespace rct3::config::game;
namespace fs = std::filesystem;

namespace
{

    struct FakeInstall
    {
        fs::path root = fs::temp_directory_path() / "rct3pp_fake_install";

        FakeInstall()
        {
            fs::remove_all(root);
            fs::create_directories(root / "GUI" / "Sub");
            fs::create_directories(root / "style");
            std::ofstream(root / "GUI" / "Sub" / "Example.Common.ovl") << "x";
        }

        /// Makes `folder` (inside root) hold an install.
        explicit FakeInstall(const fs::path& folder)
        {
            fs::remove_all(root);
            fs::create_directories(root / folder / "gui");
            fs::create_directories(root / folder / "style");
        }
        ~FakeInstall()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

} // namespace

TEST_CASE("an install is found through the environment, whatever the case of its folders")
{
    FakeInstall fake;
    const auto install = find_game_install(Environment::from_values({ { EnvVar::GamePath, fake.root.string() } }));
    REQUIRE(install.has_value());
    CHECK(install->root() == fake.root);
}

TEST_CASE("paths inside the install resolve without regard to case or separator")
{
    FakeInstall fake;
    const auto install = open_game_install(fake.root);
    REQUIRE(install.has_value());

    const auto found = install->find("gui\\sub/example.common.OVL");
    REQUIRE(found.has_value());
    CHECK(*found == fake.root / "GUI" / "Sub" / "Example.Common.ovl");
    CHECK(install->find("") == fake.root);
    CHECK(install->find("./GUI//Sub") == fake.root / "GUI" / "Sub");
    CHECK_FALSE(install->find("gui/missing.ovl").has_value());
    CHECK_FALSE(install->find("gui/../style").has_value());
}

TEST_CASE("without the variable, the settings file names the install")
{
    FakeInstall fake;
    Settings settings;
    settings.set(Setting::GamePath, fake.root.string());
    const auto install = find_game_install(Environment(), settings);
    REQUIRE(install.has_value());
    CHECK(install->root() == fake.root);

    SUBCASE("the variable wins")
    {
        const auto env = Environment::from_values({ { EnvVar::GamePath, (fake.root / "nowhere").string() } });
        CHECK_FALSE(find_game_install(env, settings).has_value());
    }
    SUBCASE("a problem names the setting, not the variable")
    {
        settings.set(Setting::GamePath, (fake.root / "nowhere").string());
        const auto missing = find_game_install(Environment(), settings);
        REQUIRE_FALSE(missing.has_value());
        const std::string message = describe(missing.error());
        CHECK(message.find("game_path") != std::string::npos);
        CHECK(message.find("RCT3PP_GAME_PATH") == std::string::npos);
    }
}

TEST_CASE("problems are reported with what to fix")
{
    SUBCASE("variable not set, nothing found")
    {
        const auto install = find_game_install(Environment(), { }, InstallPlaces { });
        REQUIRE_FALSE(install.has_value());
        CHECK(install.error().error == InstallError::NotConfigured);
        CHECK(describe(install.error()).find("RCT3PP_GAME_PATH") != std::string::npos);
        CHECK(describe(install.error()).find("game_path") != std::string::npos);
    }
    SUBCASE("path does not exist")
    {
        const auto install = open_game_install(fs::temp_directory_path() / "rct3pp_no_such_install");
        REQUIRE_FALSE(install.has_value());
        CHECK(install.error().error == InstallError::NotFound);
    }
    SUBCASE("path is a file")
    {
        FakeInstall fake;
        const auto install = open_game_install(fake.root / "GUI" / "Sub" / "Example.Common.ovl");
        REQUIRE_FALSE(install.has_value());
        CHECK(install.error().error == InstallError::NotADirectory);
    }
    SUBCASE("folder is not an install")
    {
        FakeInstall fake;
        fs::remove_all(fake.root / "style");
        const auto install = open_game_install(fake.root);
        REQUIRE_FALSE(install.has_value());
        CHECK(install.error().error == InstallError::MissingContent);
        CHECK(install.error().detail == "Style");
        CHECK(describe(install.error()).find("Style") != std::string::npos);
    }
}

TEST_CASE("without the variable or the setting, the usual places are searched in order")
{
    constexpr std::string_view kGame = "RollerCoaster Tycoon 3 Complete Edition";

    SUBCASE("the program's folder")
    {
        FakeInstall fake("game");
        const auto install = find_game_install(Environment(), { }, { .program_folder = fake.root / "game" });
        REQUIRE(install.has_value());
        CHECK(install->root() == fake.root / "game");
        CHECK(install->source() == "the program's folder");
    }
    SUBCASE("the folder above the program's")
    {
        FakeInstall fake("game");
        fs::create_directories(fake.root / "game" / "rct3pp");
        const auto install = find_game_install(Environment(), { }, { .program_folder = fake.root / "game" / "rct3pp" });
        REQUIRE(install.has_value());
        CHECK(install->root() == fake.root / "game");
    }
    SUBCASE("a Steam install's own library")
    {
        FakeInstall fake(fs::path("steam") / "steamapps" / "common" / kGame);
        const auto install = find_game_install(Environment(), { }, { .program_folder = fake.root / "elsewhere", .steam_roots = { fake.root / "steam" } });
        REQUIRE(install.has_value());
        CHECK(install->root() == fake.root / "steam" / "steamapps" / "common" / kGame);
        CHECK(install->source().starts_with("Steam library"));
    }
    SUBCASE("a library listed in libraryfolders.vdf, its backslashes doubled")
    {
        // A backslash in a folder name tests the unescaping on every system; on Windows it is a separator.
        const fs::path library = "lib\\rary";
        FakeInstall fake(library / "steamapps" / "common" / kGame);
        const auto escaped = [](const fs::path& path) {
            std::string text;
            for (char c : path.string())
                text += c == '\\' ? std::string("\\\\") : std::string(1, c);
            return text;
        };
        fs::create_directories(fake.root / "steam" / "steamapps");
        std::ofstream(fake.root / "steam" / "steamapps" / "libraryfolders.vdf") << std::format("\"libraryfolders\"\n{{\n\t\"0\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t\t\"label\"\t\t\"\"\n\t}}\n\t\"1\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n}}\n", escaped(fake.root / "steam"), escaped(fake.root / library));
        const auto install = find_game_install(Environment(), { }, { .steam_roots = { fake.root / "steam" } });
        REQUIRE(install.has_value());
        CHECK(install->root() == fake.root / library / "steamapps" / "common" / kGame);
    }
    SUBCASE("a broken libraryfolders.vdf is read safely")
    {
        FakeInstall fake("steam");
        fs::create_directories(fake.root / "steam" / "steamapps");
        for (std::string_view text : { "\"path\"", "\"path\" \"", "\"path\" \"/unterminated\\", "\"path\" x \"path\"\t\"", "{ \"path\" }", "" })
        {
            std::ofstream(fake.root / "steam" / "steamapps" / "libraryfolders.vdf", std::ios::trunc) << text;
            const auto install = find_game_install(Environment(), { }, { .steam_roots = { fake.root / "steam" } });
            CHECK_FALSE(install.has_value());
        }
    }
    SUBCASE("a GOG folder")
    {
        FakeInstall fake("gog");
        const auto install = find_game_install(Environment(), { }, { .game_folders = { fake.root / "nowhere", fake.root / "gog" } });
        REQUIRE(install.has_value());
        CHECK(install->root() == fake.root / "gog");
        CHECK(install->source() == "GOG");
    }
    SUBCASE("the variable and the setting still win, and a broken one is not searched past")
    {
        FakeInstall fake("game");
        const InstallPlaces places { .program_folder = fake.root / "game" };
        Settings settings;
        settings.set(Setting::GamePath, (fake.root / "nowhere").string());
        const auto broken = find_game_install(Environment(), settings, places);
        REQUIRE_FALSE(broken.has_value());
        CHECK(broken.error().error == InstallError::NotFound);

        const auto env = Environment::from_values({ { EnvVar::GamePath, (fake.root / "game").string() } });
        const auto named = find_game_install(env, settings, { });
        REQUIRE(named.has_value());
        CHECK(named->source() == "RCT3PP_GAME_PATH");
    }
    SUBCASE("nothing found says it searched")
    {
        FakeInstall fake(fs::path("deep") / "inside");
        const auto install = find_game_install(Environment(), { }, { .program_folder = fake.root / "deep" / "x", .steam_roots = { fake.root }, .game_folders = { fake.root } });
        REQUIRE_FALSE(install.has_value());
        CHECK(install.error().error == InstallError::NotConfigured);
        CHECK(describe(install.error()).find("Steam") != std::string::npos);
    }
}

TEST_CASE("the usual places on Linux follow the home folder")
{
#ifndef _WIN32
    const auto places = usual_install_places(Environment::from_values({ { EnvVar::Home, "/home/someone" } }));
    CHECK(std::ranges::contains(places.steam_roots, fs::path("/home/someone/.local/share/Steam")));
    CHECK(std::ranges::contains(places.steam_roots, fs::path("/home/someone/.var/app/com.valvesoftware.Steam/.local/share/Steam")));
    CHECK_FALSE(places.program_folder.empty());
#endif
}
