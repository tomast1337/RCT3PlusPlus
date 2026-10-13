// SPDX-License-Identifier: GPL-3.0-or-later
//
// The folder that holds everything the program keeps for the user: its configuration, and the screenshots
// and music the user adds. Every folder and file in it is registered once in the tables in user_data.cpp,
// and code asks for them by id, never by a path of its own.
//
// The folder is RCT3PP_DATA when set, else rct3pp/ in the user's data folder: XDG_DATA_HOME, or
// ~/.local/share without it, and APPDATA on Windows. Folders in it are made when first written to.
#pragma once

#include "config/environment.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rct3::config
{

    enum class UserFolder : std::uint8_t
    {
        Config,       ///< config/: settings, game options, keys
        Screenshots,  ///< screenshots/
        Music,        ///< music/: the user's own music for the park
        Parks,        ///< parks/: saved sandbox parks
        Scenarios,    ///< scenarios/: saved custom scenarios
        NewScenarios, ///< new_scenarios/: scenarios to start a custom scenario from
        Careers,      ///< careers/: the player's careers and their games (career/career_file.hpp)

        Count_, ///< Number of folders; not a folder
    };
    inline constexpr std::size_t kUserFolderCount = static_cast<std::size_t>(UserFolder::Count_);

    enum class UserFile : std::uint8_t
    {
        Settings,    ///< The program's settings (settings.hpp)
        GameOptions, ///< What the options window sets (game_options.hpp)
        Keys,        ///< The keys of the game's and the interface's commands (key_map.hpp)

        Count_, ///< Number of files; not a file
    };
    inline constexpr std::size_t kUserFileCount = static_cast<std::size_t>(UserFile::Count_);

    struct UserFolderInfo
    {
        UserFolder folder;
        std::string_view name;
        std::string_view description;
    };

    struct UserFileInfo
    {
        UserFile file;
        UserFolder folder;
        std::string_view name;
    };

    [[nodiscard]] std::span<const UserFolderInfo> user_folders_table();
    [[nodiscard]] std::span<const UserFileInfo> user_files_table();

    class UserData
    {
    public:
        explicit UserData(std::filesystem::path root);

        /// Where the environment puts the folder; nothing when it names no folder.
        [[nodiscard]] static std::optional<UserData> locate(const Environment& env);

        [[nodiscard]] const std::filesystem::path& root() const
        {
            return root_;
        }
        [[nodiscard]] std::filesystem::path folder(UserFolder folder) const;
        [[nodiscard]] std::filesystem::path file(UserFile file) const;
        /// Makes the folder if it is not there yet.
        [[nodiscard]] std::expected<std::filesystem::path, std::string> make_folder(UserFolder folder) const;

    private:
        std::filesystem::path root_;
    };

} // namespace rct3::config
