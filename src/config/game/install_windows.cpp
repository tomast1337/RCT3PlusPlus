// SPDX-License-Identifier: GPL-3.0-or-later
// Where installs live on Windows: Steam's folder from the registry, Epic's list of installs, GOG's installs
// and the disc versions' folders.
#include "config/game/install.hpp"

#include <array>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

namespace rct3::config::game
{

    namespace fs = std::filesystem;

    namespace
    {

        // A text value from the registry, if it is there and fits.
        std::optional<std::wstring> registry_text(HKEY key, const wchar_t* subkey, const wchar_t* value)
        {
            std::array<wchar_t, 1024> text { };
            DWORD size = sizeof(text);
            if (RegGetValueW(key, subkey, value, RRF_RT_REG_SZ, nullptr, text.data(), &size) != ERROR_SUCCESS)
                return std::nullopt;
            return std::wstring(text.data());
        }

    } // namespace

    InstallPlaces usual_install_places(const Environment&)
    {
        InstallPlaces places;
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
                    places.game_folders.push_back({ .path = *path, .source = "GOG" });
            }
            RegCloseKey(games);
        }
        // Epic's launcher keeps its list of installs here, whichever drive the games are on.
        places.epic_lists.emplace_back("C:\\ProgramData\\Epic\\EpicGamesLauncher\\Data\\Manifests");
        // GOG's default folder, then where the disc versions install (Atari published them).
        for (std::string_view name : kGameFolderNames)
            places.game_folders.push_back({ .path = fs::path("C:\\GOG Games") / name, .source = "GOG" });
        for (std::string_view program_files : { "C:\\Program Files (x86)", "C:\\Program Files" })
        {
            for (std::string_view name : kGameFolderNames)
                places.game_folders.push_back({ .path = fs::path(program_files) / "Atari" / name, .source = "disc install" });
        }
        return places;
    }

} // namespace rct3::config::game
