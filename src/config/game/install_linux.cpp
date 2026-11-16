// SPDX-License-Identifier: GPL-3.0-or-later
// Where installs live on Linux: Steam in the home folder (also as a Flatpak), and GOG's Windows installers run through Heroic or Wine.
#include "config/game/install.hpp"

#include <system_error>

namespace rct3::config::game
{

    namespace fs = std::filesystem;

    InstallPlaces usual_install_places(const Environment& env)
    {
        InstallPlaces places;
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
        return places;
    }

} // namespace rct3::config::game
