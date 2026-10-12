// SPDX-License-Identifier: GPL-3.0-or-later
//
// Locating the installed original game, whose data files the program reads. The location comes from
// RCT3PP_GAME_PATH (see config/environment.hpp).
//
// The game was made for Windows, where file names ignore case, and its data refers to files with whatever
// case the author typed. `GameInstall::find` resolves such names on case-sensitive file systems.
#pragma once

#include "config/environment.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::config
{

    enum class InstallError : std::uint8_t
    {
        NotConfigured,  ///< RCT3PP_GAME_PATH is not set
        NotFound,       ///< The path does not exist
        NotADirectory,  ///< The path is a file
        MissingContent, ///< A folder every install has is missing: probably the wrong folder
    };

    struct InstallProblem
    {
        InstallError error = InstallError::NotConfigured;
        std::filesystem::path path;
        std::string detail; ///< The missing entry, for MissingContent
    };

    /// One line for logs and error messages, naming the variable to set where that helps.
    [[nodiscard]] std::string describe(const InstallProblem& problem);

    class GameInstall
    {
    public:
        [[nodiscard]] const std::filesystem::path& root() const
        {
            return root_;
        }

        /// Resolves a path relative to the install root, matching every component without regard to case.
        /// Accepts '/' and '\' as separators. Returns the path as it exists on disk, or nothing.
        [[nodiscard]] std::optional<std::filesystem::path> find(std::string_view relative) const;

    private:
        friend std::expected<GameInstall, InstallProblem> open_game_install(const std::filesystem::path& root);

        explicit GameInstall(std::filesystem::path root)
            : root_(std::move(root))
        {
        }

        std::filesystem::path root_;
    };

    /// Checks that `root` looks like an install of the game.
    [[nodiscard]] std::expected<GameInstall, InstallProblem> open_game_install(const std::filesystem::path& root);

    /// The install named by RCT3PP_GAME_PATH.
    [[nodiscard]] std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env);

} // namespace rct3::config
