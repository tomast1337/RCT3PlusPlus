// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game install checks against a fake install built in a temp directory: empty folders and files with
// invented names, no game data.
#include "config/game_install.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

using namespace rct3::config;
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
    SUBCASE("variable not set")
    {
        const auto install = find_game_install(Environment());
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
