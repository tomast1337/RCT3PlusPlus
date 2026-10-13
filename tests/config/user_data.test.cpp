// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/user_data.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <set>

using namespace rct3::config;
namespace fs = std::filesystem;

TEST_CASE("every folder and file of the user's data is registered once, in id order")
{
    std::set<std::string_view> names;
    const auto folders = user_folders_table();
    REQUIRE(folders.size() == kUserFolderCount);
    for (std::size_t i = 0; i < folders.size(); ++i)
    {
        CHECK(static_cast<std::size_t>(folders[i].folder) == i);
        CHECK(names.insert(folders[i].name).second);
    }
    const auto files = user_files_table();
    REQUIRE(files.size() == kUserFileCount);
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        CHECK(static_cast<std::size_t>(files[i].file) == i);
        CHECK(names.insert(files[i].name).second);
    }
}

TEST_CASE("the user's data lives in the data folder")
{
    using E = EnvVar;
    const auto root = [](std::initializer_list<std::pair<EnvVar, std::string_view>> values) {
        const auto data = UserData::locate(Environment::from_values(values));
        return data ? std::optional(data->root()) : std::nullopt;
    };
    CHECK(root({ { E::DataFolder, "/a/mine" }, { E::Home, "/users/someone" } }) == fs::path("/a/mine"));
#ifndef _WIN32
    CHECK(root({ { E::DataHome, "/data" }, { E::Home, "/users/someone" } }) == fs::path("/data/rct3pp"));
    CHECK(root({ { E::DataHome, "relative" }, { E::Home, "/users/someone" } }) == fs::path("/users/someone/.local/share/rct3pp"));
    CHECK(root({ { E::Home, "/users/someone" } }) == fs::path("/users/someone/.local/share/rct3pp"));
    CHECK_FALSE(root({ }));
#endif
}

TEST_CASE("files sit in their folders, which are made when asked for")
{
    const fs::path dir = fs::temp_directory_path() / "rct3pp_user_data_test";
    fs::remove_all(dir);
    const UserData data(dir);
    CHECK(data.file(UserFile::Settings) == dir / "config" / "settings.json");
    CHECK(data.file(UserFile::Keys) == dir / "config" / "keys.json");
    CHECK(data.folder(UserFolder::Screenshots) == dir / "screenshots");
    CHECK_FALSE(fs::exists(data.folder(UserFolder::Music)));
    const auto made = data.make_folder(UserFolder::Music);
    REQUIRE(made);
    CHECK(fs::is_directory(*made));
    fs::remove_all(dir);
}
