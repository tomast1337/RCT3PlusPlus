// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game/install.hpp"

#include "config/json_text.hpp"
#include "config/text_file.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <system_error>
#include <utility>

namespace rct3::config::game
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

        // The Steam install itself, then every library its steamapps/libraryfolders.vdf lists. The file is
        // text another program wrote: only the values of "path" keys are read, every step is bounds-checked,
        // and an unterminated value ends the list.
        std::vector<fs::path> steam_libraries(const fs::path& steam)
        {
            std::vector<fs::path> libraries { steam };
            const auto text = read_text_file(steam / "steamapps" / "libraryfolders.vdf");
            if (!text || !*text)
                return libraries;
            constexpr std::string_view kKey = "\"path\"";
            std::string_view rest = **text;
            for (std::size_t at = rest.find(kKey); at != std::string_view::npos; at = rest.find(kKey))
            {
                rest.remove_prefix(at + kKey.size());
                const std::size_t open = rest.find_first_not_of(" \t\r\n");
                if (open == std::string_view::npos || rest[open] != '"')
                    continue;
                std::string value;
                std::size_t i = open + 1;
                for (; i < rest.size() && rest[i] != '"'; ++i)
                {
                    if (rest[i] == '\\' && i + 1 < rest.size())
                        ++i; // An escaped character: Windows paths double their backslashes
                    value += rest[i];
                }
                if (i >= rest.size())
                    break;
                rest.remove_prefix(i + 1);
                if (!value.empty())
                    libraries.emplace_back(value);
            }
            return libraries;
        }

        // The game's folders in Epic's lists of installs. Epic's launcher keeps one .item file per game, an
        // object with DisplayName and InstallLocation; Heroic (the launcher Linux players use for Epic) keeps
        // one installed.json, an object per game with title and install_path. Both are text other programs
        // wrote: anything not shaped so is skipped.
        std::vector<fs::path> epic_installs(const fs::path& list)
        {
            constexpr std::size_t kMaxFiles = 1000; ///< Chosen by eye: far more games than anyone installs
            std::vector<fs::path> files;
            std::error_code ec;
            if (fs::is_directory(list, ec))
            {
                for (fs::directory_iterator it(list, ec), end; !ec && it != end && files.size() < kMaxFiles; it.increment(ec))
                {
                    if (it->path().extension() == ".item")
                        files.push_back(it->path());
                }
            }
            else
            {
                files.push_back(list);
            }

            std::vector<fs::path> installs;
            const auto look_at = [&](const Json& game) {
                if (!game.is_object())
                    return;
                for (const auto [title_key, path_key] : { std::pair { "DisplayName", "InstallLocation" }, std::pair { "title", "install_path" } })
                {
                    const auto title = game.find(title_key);
                    const auto path = game.find(path_key);
                    if (title != game.end() && path != game.end() && title->is_string() && path->is_string() && title->get_ref<const std::string&>().contains("RollerCoaster Tycoon 3"))
                        installs.emplace_back(path->get<std::string>());
                }
            };
            for (const fs::path& file : files)
            {
                const auto text = read_text_file(file);
                if (!text || !*text)
                    continue;
                const auto json = parse_json_object(**text);
                if (!json)
                    continue;
                look_at(*json);
                for (const Json& member : *json)
                    look_at(member);
            }
            return installs;
        }

    } // namespace

    std::string describe(const InstallProblem& problem)
    {
        const std::string variable = problem.source.empty() ? std::string(env_name(EnvVar::GamePath)) : problem.source;
        switch (problem.error)
        {
            case InstallError::NotConfigured:
                return std::format("{} and {} in the settings file are not set, and no install was found in the program's folder, the Steam libraries, Epic's installs or the GOG and disc install folders; point one of them at the folder the original game is installed in", env_name(EnvVar::GamePath), setting_name(Setting::GamePath));
            case InstallError::NotFound:
                return std::format("game folder {} does not exist (from {})", problem.path.string(), variable);
            case InstallError::NotADirectory:
                return std::format("game folder {} is not a folder (from {})", problem.path.string(), variable);
            case InstallError::MissingContent:
                return std::format("game folder {} has no {} folder; is {} the install folder?", problem.path.string(), problem.detail, variable);
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
            return std::unexpected(InstallProblem { InstallError::NotFound, root, { }, { } });
        if (!fs::is_directory(root, ec))
            return std::unexpected(InstallProblem { InstallError::NotADirectory, root, { }, { } });
        for (std::string_view folder : kRequiredFolders)
        {
            const auto entry = find_entry(root, folder);
            if (!entry || !fs::is_directory(*entry, ec))
                return std::unexpected(InstallProblem { InstallError::MissingContent, root, std::string(folder), { } });
        }
        return GameInstall(root);
    }

    std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env, const Settings& settings, const InstallPlaces& places)
    {
        if (const auto root = env.path(EnvVar::GamePath))
        {
            auto install = open_game_install(*root);
            if (install)
                install->source_ = env_name(EnvVar::GamePath);
            return install;
        }
        if (const auto root = settings.path(Setting::GamePath))
        {
            const std::string source = std::format("{} in the settings file", setting_name(Setting::GamePath));
            auto install = open_game_install(*root);
            if (install)
                install->source_ = source;
            else
                install.error().source = source;
            return install;
        }

        std::vector<std::pair<fs::path, std::string>> candidates;
        if (!places.program_folder.empty())
        {
            // Unpacked into the game's folder, or into a folder of its own inside it.
            candidates.emplace_back(places.program_folder, "the program's folder");
            candidates.emplace_back(places.program_folder.parent_path(), "the folder above the program's");
        }
        for (const fs::path& steam : places.steam_roots)
        {
            for (const fs::path& library : steam_libraries(steam))
            {
                for (std::string_view name : kGameFolderNames)
                    candidates.emplace_back(library / "steamapps" / "common" / name, std::format("Steam library {}", library.string()));
            }
        }
        for (const fs::path& list : places.epic_lists)
        {
            for (const fs::path& folder : epic_installs(list))
                candidates.emplace_back(folder, "Epic Games");
        }
        for (const GameFolder& folder : places.game_folders)
            candidates.emplace_back(folder.path, std::string(folder.source));

        for (auto& [path, source] : candidates)
        {
            if (auto install = open_game_install(path))
            {
                install->source_ = std::move(source);
                return install;
            }
        }
        return std::unexpected(InstallProblem { InstallError::NotConfigured, { }, { }, { } });
    }

    std::expected<GameInstall, InstallProblem> find_game_install(const Environment& env, const Settings& settings)
    {
        return find_game_install(env, settings, usual_install_places(env));
    }

} // namespace rct3::config::game
