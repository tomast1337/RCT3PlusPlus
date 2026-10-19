// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/user_data.hpp"

#include <array>
#include <system_error>
#include <utility>

namespace rct3::config
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::string_view kFolderName = "rct3pp";

        constexpr std::array<UserFolderInfo, kUserFolderCount> kFolders = { {
            { UserFolder::Config, "config", "Settings, game options and keys" },
            { UserFolder::Screenshots, "screenshots", "Screenshots" },
            { UserFolder::Music, "music", "The user's own music for the park" },
            { UserFolder::Parks, "parks", "Saved sandbox parks" },
            { UserFolder::Scenarios, "scenarios", "Saved custom scenarios" },
            { UserFolder::NewScenarios, "new_scenarios", "Scenarios to start a custom scenario from" },
            { UserFolder::Careers, "careers", "Careers and their games" },
            { UserFolder::Photos, "photos", "Photos the park's cameras take of guests" },
            { UserFolder::Coasters, "coasters", "Saved ride designs" },
            { UserFolder::Structures, "structures", "Saved scenery structures" },
            { UserFolder::Guests, "guests", "Guests the player made" },
            { UserFolder::Pools, "pools", "Saved pool designs" },
            { UserFolder::Fireworks, "fireworks", "Firework displays" },
            { UserFolder::FireworkEffects, "firework_effects", "The player's own fireworks" },
            { UserFolder::LaserWriting, "laser_writing", "Laser show texts" },
            { UserFolder::LaserEffects, "laser_effects", "The player's own laser effects" },
            { UserFolder::WaterJetEffects, "water_jet_effects", "The player's own water jet effects" },
            { UserFolder::Logs, "logs", "A log of each run" },
        } };

        constexpr std::array<UserFileInfo, kUserFileCount> kFiles = { {
            { UserFile::Settings, UserFolder::Config, "settings.json" },
            { UserFile::GameOptions, UserFolder::Config, "options.json" },
            { UserFile::Keys, UserFolder::Config, "keys.json" },
        } };

        // The tables are indexed by their ids, so their order must follow the enums.
        static_assert([] {
            for (std::size_t i = 0; i < kFolders.size(); ++i)
            {
                if (static_cast<std::size_t>(kFolders[i].folder) != i)
                    return false;
            }
            for (std::size_t i = 0; i < kFiles.size(); ++i)
            {
                if (static_cast<std::size_t>(kFiles[i].file) != i)
                    return false;
            }
            return true;
        }());

    } // namespace

    std::span<const UserFolderInfo> user_folders_table()
    {
        return kFolders;
    }

    std::span<const UserFileInfo> user_files_table()
    {
        return kFiles;
    }

    UserData::UserData(fs::path root)
        : root_(std::move(root))
    {
    }

    std::optional<UserData> UserData::locate(const Environment& env)
    {
        if (const auto folder = env.path(EnvVar::DataFolder))
            return UserData(*folder);
#ifdef _WIN32
        if (const auto app_data = env.path(EnvVar::AppData))
            return UserData(*app_data / kFolderName);
#endif
        // The XDG base directory rules ignore a relative XDG_DATA_HOME.
        if (const auto data = env.path(EnvVar::DataHome); data && data->is_absolute())
            return UserData(*data / kFolderName);
        if (const auto home = env.path(EnvVar::Home))
            return UserData(*home / ".local" / "share" / kFolderName);
        return std::nullopt;
    }

    fs::path UserData::folder(UserFolder folder) const
    {
        return root_ / kFolders.at(static_cast<std::size_t>(folder)).name;
    }

    fs::path UserData::file(UserFile file) const
    {
        const UserFileInfo& info = kFiles.at(static_cast<std::size_t>(file));
        return folder(info.folder) / info.name;
    }

    std::expected<fs::path, std::string> UserData::make_folder(UserFolder folder) const
    {
        fs::path path = this->folder(folder);
        std::error_code ec;
        fs::create_directories(path, ec);
        if (ec)
            return std::unexpected("cannot create " + path.string() + ": " + ec.message());
        return path;
    }

} // namespace rct3::config
