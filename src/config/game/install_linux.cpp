// SPDX-License-Identifier: GPL-3.0-or-later
// Where installs live on Linux: Steam in the home folder (also as a Flatpak), Epic and GOG through Heroic, and GOG's
// installers and the disc versions run through Wine.
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
            // Epic's games through Heroic, as installed or as a Flatpak: its list of installs.
            places.epic_lists.push_back(*home / ".config/heroic/legendaryConfig/legendary/installed.json");
            places.epic_lists.push_back(*home / ".var/app/com.heroicgameslauncher.hgl/config/heroic/legendaryConfig/legendary/installed.json");
            // GOG's Windows installers run through Heroic, or through Wine in its default prefix; a disc
            // version installed through Wine lands in its Program Files.
            const fs::path wine = *home / ".wine" / "drive_c";
            for (std::string_view name : kGameFolderNames)
            {
                places.game_folders.push_back({ .path = *home / "Games" / "Heroic" / name, .source = "GOG" });
                places.game_folders.push_back({ .path = wine / "GOG Games" / name, .source = "GOG" });
                places.game_folders.push_back({ .path = wine / "Program Files (x86)" / "Atari" / name, .source = "disc install" });
                places.game_folders.push_back({ .path = wine / "Program Files" / "Atari" / name, .source = "disc install" });
            }
        }
        return places;
    }

} // namespace rct3::config::game
