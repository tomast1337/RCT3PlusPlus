// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game_saves.hpp"

#include <system_error>

namespace rct3::config
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::string_view kFolderName = "RCT3";

        bool is_folder(const fs::path& path)
        {
            std::error_code error;
            return fs::is_directory(path, error);
        }

        /// Through Proton: the install is <library>/steamapps/common/<game>; each game played through Proton
        /// has a prefix under <library>/steamapps/compatdata, a Windows user's folders inside it.
        std::optional<fs::path> in_proton_prefix(const fs::path& install)
        {
            const fs::path game = install.lexically_normal();
            const fs::path common = (game.has_filename() ? game : game.parent_path()).parent_path();
            if (common.filename() != "common")
                return std::nullopt;
            const fs::path compat = common.parent_path() / "compatdata";
            std::error_code error;
            for (const auto& prefix : fs::directory_iterator(compat, error))
            {
                const fs::path saves = prefix.path() / "pfx" / "drive_c" / "users" / "steamuser" / "Documents" / kFolderName;
                if (is_folder(saves))
                    return saves;
            }
            return std::nullopt;
        }

    } // namespace

    std::optional<fs::path> find_game_saves(const Environment& env, const fs::path& install)
    {
        if (const auto folder = env.path(EnvVar::GameSaves))
            return *folder;
        if (auto proton = in_proton_prefix(install))
            return proton;
#ifdef _WIN32
        // APPDATA is <profile>\AppData\Roaming; Documents is beside AppData.
        if (const auto app_data = env.path(EnvVar::AppData))
        {
            const fs::path saves = app_data->parent_path().parent_path() / "Documents" / kFolderName;
            if (is_folder(saves))
                return saves;
        }
#endif
        return std::nullopt;
    }

    std::optional<std::string_view> game_saves_folder(UserFolder folder)
    {
        switch (folder)
        {
            case UserFolder::Parks:
                return "Parks";
            case UserFolder::Scenarios:
                return "Scenarios";
            case UserFolder::NewScenarios:
                return "Start New Scenarios";
            default:
                return std::nullopt;
        }
    }

} // namespace rct3::config
