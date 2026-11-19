// SPDX-License-Identifier: GPL-3.0-or-later
//
// Locating the installed original game, whose data files the program reads. The location comes from
// RCT3PP_GAME_PATH (see config/environment.hpp), else from game_path in the settings file
// (config/settings.hpp). When neither is set, the usual places are searched, and the first folder that
// holds a complete install wins: the folder the program runs from and its parent (unpacked into the game's
// folder, it starts with no setup), the Steam libraries, then the folders GOG installs to.
//
// The game was made for Windows, where file names ignore case, and its data refers to files with whatever
// case the author typed. `GameInstall::find` resolves such names on case-sensitive file systems.
#pragma once

#include "config/environment.hpp"
#include "config/settings.hpp"
#include "core/types.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::config::game
{

    enum class InstallError : u8
    {
        NotConfigured,  ///< Neither RCT3PP_GAME_PATH nor game_path is set, and no usual place holds an install
        NotFound,       ///< The path does not exist
        NotADirectory,  ///< The path is a file
        MissingContent, ///< A folder every install has is missing: probably the wrong folder
    };

    struct InstallProblem
    {
        InstallError error = InstallError::NotConfigured;
        std::filesystem::path path;
        std::string detail; ///< The missing entry, for MissingContent
        std::string source; ///< Where the path came from, for messages; RCT3PP_GAME_PATH when empty
    };

    /// One line for logs and error messages, naming the variable to set where that helps.
    [[nodiscard]] std::string describe(const InstallProblem& problem);

    struct InstallPlaces;

    class GameInstall
    {
    public:
        [[nodiscard]] const std::filesystem::path& root() const
        {
            return root_;
        }

        /// How the folder was found, for the log: "RCT3PP_GAME_PATH", "the program's folder", ...
        [[nodiscard]] const std::string& source() const
        {
            return source_;
        }

        /// Resolves a path relative to the install root, matching every component without regard to case.
        /// Accepts '/' and '\' as separators. Returns the path as it exists on disk, or nothing.
        [[nodiscard]] std::optional<std::filesystem::path> find(std::string_view relative) const;

    private:
        friend std::expected<GameInstall, InstallProblem> open_game_install(const std::filesystem::path& root);
        friend std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env, const Settings& settings, const InstallPlaces& places);

        explicit GameInstall(std::filesystem::path root)
            : root_(std::move(root))
        {
        }

        std::filesystem::path root_;
        std::string source_;
    };

    /// The folder names the game's editions install to: the Complete Edition, then the older Platinum and first
    /// releases.
    inline constexpr std::array<std::string_view, 3> kGameFolderNames = { "RollerCoaster Tycoon 3 Complete Edition", "RollerCoaster Tycoon 3 Platinum", "RollerCoaster Tycoon 3" };

    /// A folder that may hold the game itself, and how to name it in the log.
    struct GameFolder
    {
        std::filesystem::path path { };
        std::string_view source { }; ///< "GOG", "disc install", ...
    };

    /// Where to look for an install that neither RCT3PP_GAME_PATH nor game_path names, in order.
    struct InstallPlaces
    {
        std::filesystem::path program_folder { };           ///< The folder the program runs from; empty to skip
        std::vector<std::filesystem::path> steam_roots { }; ///< Steam installs; each lists its libraries
        std::vector<std::filesystem::path> epic_lists { };  ///< Epic's lists of installs: a folder of .item files, or one JSON file
        std::vector<GameFolder> game_folders { };           ///< GOG and disc install folders
    };

    /// This machine's usual places: the program's folder, the default Steam installs, Epic's lists of installs,
    /// and the GOG and disc install folders. Each platform has its own file: install_linux.cpp,
    /// install_windows.cpp.
    /// shortcut: no macOS file yet; add install_macos.cpp once there is a macOS build.
    [[nodiscard]] InstallPlaces usual_install_places(const Environment& env);

    /// Checks that `root` looks like an install of the game.
    [[nodiscard]] std::expected<GameInstall, InstallProblem> open_game_install(const std::filesystem::path& root);

    /// The install named by RCT3PP_GAME_PATH, else by game_path in `settings`, else the first complete one
    /// in `places`. A named folder that is not an install is an error, not a reason to search.
    [[nodiscard]] std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env, const Settings& settings, const InstallPlaces& places);

    /// The same, searching this machine's usual places.
    [[nodiscard]] std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env, const Settings& settings = { });

} // namespace rct3::config::game
