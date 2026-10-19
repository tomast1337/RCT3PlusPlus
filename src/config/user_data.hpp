// SPDX-License-Identifier: GPL-3.0-or-later
//
// The folder that holds everything the program keeps for the user: its configuration, the games and designs
// the player saves, the pictures taken in the park and the music the user adds. There is a folder for each
// kind of file the original game keeps in the player's Documents, so each has its place before its tool comes. Every folder and file in it is registered once in the tables in user_data.cpp,
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
        Config,          ///< config/: settings, game options, keys
        Screenshots,     ///< screenshots/
        Music,           ///< music/: the user's own music for the park
        Parks,           ///< parks/: saved sandbox parks
        Scenarios,       ///< scenarios/: saved custom scenarios
        NewScenarios,    ///< new_scenarios/: scenarios to start a custom scenario from
        Careers,         ///< careers/: the player's careers and their games (career/career_file.hpp)
        Photos,          ///< photos/: what the park's cameras take of guests
        Coasters,        ///< coasters/: saved ride designs
        Structures,      ///< structures/: saved scenery structures
        Guests,          ///< guests/: guests the player made
        Pools,           ///< pools/: saved pool designs
        Fireworks,       ///< fireworks/: firework displays
        FireworkEffects, ///< firework_effects/: the player's own fireworks
        LaserWriting,    ///< laser_writing/: laser show texts
        LaserEffects,    ///< laser_effects/: the player's own laser effects
        WaterJetEffects, ///< water_jet_effects/: the player's own water jet effects
        Logs,            ///< logs/: a log of each run, ending with a crash report when it crashed

        Count_, ///< Number of folders; not a folder
    };
    inline constexpr std::size_t kUserFolderCount = static_cast<std::size_t>(UserFolder::Count_);

    enum class UserFile : std::uint8_t
    {
        Settings,    ///< The program's settings (settings.hpp)
        GameOptions, ///< What the options window sets (game_options.hpp)
        Keys,        ///< The keys of the game's and the interface's commands (key_map.hpp)
        RideRatings, ///< The user's own ride rating model, when they keep one (simulation/ratings.hpp)

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
