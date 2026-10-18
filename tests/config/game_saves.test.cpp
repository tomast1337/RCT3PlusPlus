// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game_saves.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <system_error>

using namespace rct3::config;
namespace fs = std::filesystem;

namespace
{

    /// A made-up Steam library with the game in it and Proton prefixes beside it.
    struct Library
    {
        fs::path root = fs::temp_directory_path() / "rct3pp_fake_library";
        fs::path install = root / "steamapps" / "common" / "Some Game";

        Library()
        {
            std::error_code error;
            fs::remove_all(root, error);
            fs::create_directories(install);
            // A prefix of another game, without the folder.
            fs::create_directories(root / "steamapps" / "compatdata" / "111" / "pfx" / "drive_c" / "users" / "steamuser" / "Documents");
        }
        ~Library()
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
        Library(const Library&) = delete;
        Library& operator=(const Library&) = delete;
        Library(Library&&) = delete;
        Library& operator=(Library&&) = delete;

        fs::path add_saves(std::string_view id) const
        {
            const fs::path saves = root / "steamapps" / "compatdata" / id / "pfx" / "drive_c" / "users" / "steamuser" / "Documents" / "RCT3";
            fs::create_directories(saves);
            return saves;
        }
    };

} // namespace

TEST_CASE("the game's saves come from the variable first")
{
    const Library library;
    library.add_saves("222");
    const auto env = Environment::from_values({ { EnvVar::GameSaves, "/somewhere/RCT3" } });
    CHECK(find_game_saves(env, library.install) == fs::path("/somewhere/RCT3"));
}

TEST_CASE("the game's saves are found in a Proton prefix of the install's Steam library")
{
    const Library library;
    CHECK_FALSE(find_game_saves({ }, library.install).has_value());
    const fs::path saves = library.add_saves("222");
    CHECK(find_game_saves({ }, library.install) == saves);
    CHECK(find_game_saves({ }, library.install / "") == saves); // A trailing separator
}

TEST_CASE("an install outside a Steam library has no game saves next to it")
{
    const Library library;
    library.add_saves("222");
    CHECK_FALSE(find_game_saves({ }, library.root / "elsewhere").has_value());
}

TEST_CASE("our folders of games match the game's")
{
    CHECK(game_saves_folder(UserFolder::Parks) == "Parks");
    CHECK(game_saves_folder(UserFolder::Scenarios) == "Scenarios");
    CHECK(game_saves_folder(UserFolder::NewScenarios) == "Start New Scenarios");
    CHECK(game_saves_folder(UserFolder::Guests) == "Peeps");
    CHECK_FALSE(game_saves_folder(UserFolder::Photos).has_value());
    CHECK_FALSE(game_saves_folder(UserFolder::Config).has_value());
    CHECK_FALSE(game_saves_folder(UserFolder::Careers).has_value());
}
