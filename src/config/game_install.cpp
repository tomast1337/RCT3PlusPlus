// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game_install.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <system_error>

namespace rct3::config
{

    namespace fs = std::filesystem;

    namespace
    {

        // Folders present in every install of the game. Their absence means the path points somewhere else.
        constexpr std::array<std::string_view, 2> kRequiredFolders = { "gui", "Style" };

        // The entry of `dir` named `name`: the exact name if it exists, otherwise the first that matches
        // without regard to case. Directory order is unspecified, so two entries differing only in case
        // (which an install never has) resolve to either.
        std::optional<fs::path> find_entry(const fs::path& dir, std::string_view name)
        {
            std::error_code ec;
            fs::path exact = dir / name;
            if (fs::exists(exact, ec))
                return exact;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                if (strings::equals_ignoring_case(it->path().filename().string(), name))
                    return it->path();
            }
            return std::nullopt;
        }

    } // namespace

    std::string describe(const InstallProblem& problem)
    {
        const std::string variable(env_name(EnvVar::GamePath));
        switch (problem.error)
        {
            case InstallError::NotConfigured:
                return variable + " is not set; point it at the folder the original game is installed in";
            case InstallError::NotFound:
                return "game folder " + problem.path.string() + " does not exist (from " + variable + ")";
            case InstallError::NotADirectory:
                return "game folder " + problem.path.string() + " is not a folder (from " + variable + ")";
            case InstallError::MissingContent:
                return "game folder " + problem.path.string() + " has no " + problem.detail + " folder; is " + variable + " the install folder?";
        }
        return "unknown game folder problem";
    }

    std::optional<fs::path> GameInstall::find(std::string_view relative) const
    {
        fs::path current = root_;
        while (!relative.empty())
        {
            const std::size_t cut = relative.find_first_of("/\\");
            const std::string_view part = relative.substr(0, cut);
            relative = cut == std::string_view::npos ? std::string_view { } : relative.substr(cut + 1);
            if (part.empty() || part == ".")
                continue;
            if (part == "..")
                return std::nullopt; // Stay inside the install
            const auto next = find_entry(current, part);
            if (!next)
                return std::nullopt;
            current = *next;
        }
        return current;
    }

    std::expected<GameInstall, InstallProblem> open_game_install(const fs::path& root)
    {
        std::error_code ec;
        if (!fs::exists(root, ec))
            return std::unexpected(InstallProblem { InstallError::NotFound, root, { } });
        if (!fs::is_directory(root, ec))
            return std::unexpected(InstallProblem { InstallError::NotADirectory, root, { } });
        for (std::string_view folder : kRequiredFolders)
        {
            const auto entry = find_entry(root, folder);
            if (!entry || !fs::is_directory(*entry, ec))
                return std::unexpected(InstallProblem { InstallError::MissingContent, root, std::string(folder) });
        }
        return GameInstall(root);
    }

    std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env)
    {
        const auto root = env.path(EnvVar::GamePath);
        if (!root)
            return std::unexpected(InstallProblem { InstallError::NotConfigured, { }, { } });
        return open_game_install(*root);
    }

} // namespace rct3::config
