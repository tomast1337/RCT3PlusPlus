// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game/install.hpp"

#include "config/text_file.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <system_error>
#include <utility>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace rct3::config::game
{

    namespace fs = std::filesystem;

    namespace
    {

        // Folders present in every install of the game. Their absence means the path points somewhere else.
        constexpr std::array<std::string_view, 2> kRequiredFolders = { "gui", "Style" };

        // The folder names the game's editions install to: the Complete Edition, then the older Platinum and
        // first releases.
        constexpr std::array<std::string_view, 3> kGameFolderNames = { "RollerCoaster Tycoon 3 Complete Edition", "RollerCoaster Tycoon 3 Platinum", "RollerCoaster Tycoon 3" };

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

#ifdef _WIN32
        // A text value from the registry, if it is there and fits.
        std::optional<std::wstring> registry_text(HKEY key, const wchar_t* subkey, const wchar_t* value)
        {
            std::array<wchar_t, 1024> text { };
            DWORD size = sizeof(text);
            if (RegGetValueW(key, subkey, value, RRF_RT_REG_SZ, nullptr, text.data(), &size) != ERROR_SUCCESS)
                return std::nullopt;
            return std::wstring(text.data());
        }
#endif

    } // namespace

    std::string describe(const InstallProblem& problem)
    {
        const std::string variable = problem.source.empty() ? std::string(env_name(EnvVar::GamePath)) : problem.source;
        switch (problem.error)
        {
            case InstallError::NotConfigured:
                return std::format("{} and {} in the settings file are not set, and no install was found in the program's folder, the Steam libraries or the GOG folders; point one of them at the folder the original game is installed in", env_name(EnvVar::GamePath), setting_name(Setting::GamePath));
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

    InstallPlaces usual_install_places([[maybe_unused]] const Environment& env)
    {
        InstallPlaces places;
#ifdef _WIN32
        std::wstring program(32768, L'\0'); // The longest path Windows allows
        const DWORD length = GetModuleFileNameW(nullptr, program.data(), static_cast<DWORD>(program.size()));
        if (length > 0 && length < program.size())
            places.program_folder = fs::path(program.substr(0, length)).parent_path();

        if (const auto steam = registry_text(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"))
            places.steam_roots.emplace_back(*steam);
        places.steam_roots.emplace_back("C:\\Program Files (x86)\\Steam");

        // GOG Galaxy and GOG's installers list each game under its own key, with its name and folder.
        HKEY games = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ, &games) == ERROR_SUCCESS)
        {
            constexpr DWORD kMaxGames = 10000; ///< Chosen by eye: far more games than anyone installs
            for (DWORD index = 0; index < kMaxGames; ++index)
            {
                std::array<wchar_t, 256> id { };
                DWORD id_length = static_cast<DWORD>(id.size());
                const LSTATUS status = RegEnumKeyExW(games, index, id.data(), &id_length, nullptr, nullptr, nullptr, nullptr);
                if (status == ERROR_NO_MORE_ITEMS)
                    break;
                if (status != ERROR_SUCCESS)
                    continue;
                const auto name = registry_text(games, id.data(), L"gameName");
                if (!name || !name->contains(L"RollerCoaster Tycoon 3"))
                    continue;
                if (const auto path = registry_text(games, id.data(), L"path"))
                    places.game_folders.emplace_back(*path);
            }
            RegCloseKey(games);
        }
        for (std::string_view name : kGameFolderNames)
            places.game_folders.push_back(fs::path("C:\\GOG Games") / name);
#else
        std::error_code ec;
        const fs::path program = fs::read_symlink("/proc/self/exe", ec);
        if (!ec)
            places.program_folder = program.parent_path();

        if (const auto home = env.path(EnvVar::Home))
        {
            // The usual Steam, the one some distributions link to it, and the Flatpak one.
            for (std::string_view steam : { ".steam/steam", ".local/share/Steam", ".var/app/com.valvesoftware.Steam/.local/share/Steam" })
                places.steam_roots.push_back(*home / steam);
            // GOG's Windows installers run through Heroic, or through Wine in its default prefix.
            for (std::string_view name : kGameFolderNames)
            {
                places.game_folders.push_back(*home / "Games" / "Heroic" / name);
                places.game_folders.push_back(*home / ".wine" / "drive_c" / "GOG Games" / name);
            }
        }
#endif
        return places;
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
        for (const fs::path& folder : places.game_folders)
            candidates.emplace_back(folder, "GOG");

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
