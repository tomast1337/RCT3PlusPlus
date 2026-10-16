// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original game keeps the player's games in a folder of its own, "RCT3" in the user's Documents, with a
// folder in it for each kind: sandbox parks, custom scenarios and the scenarios to start one from. The load
// window shows those games beside ours. They are only ever read: nothing here writes, renames or deletes
// a file of the game's.
//
// The folder is RCT3PP_GAME_SAVES when set. Otherwise it is looked for where the game would have made it:
// for a game installed with Steam and played through Proton, in a Proton prefix of the same Steam library
// (steamapps/compatdata/<id>/pfx/drive_c/users/steamuser/Documents/RCT3); on Windows, in the Documents
// folder of the profile that holds APPDATA.
#pragma once

#include "config/environment.hpp"
#include "config/user_data.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace rct3::config
{

    /// The game's folder of the player's games, if it is there. `install` is the game's root folder.
    [[nodiscard]] std::optional<std::filesystem::path> find_game_saves(const Environment& env, const std::filesystem::path& install);

    /// The folder in the game's that holds what our `folder` holds; nothing for folders it has no match for.
    [[nodiscard]] std::optional<std::string_view> game_saves_folder(UserFolder folder);

} // namespace rct3::config
