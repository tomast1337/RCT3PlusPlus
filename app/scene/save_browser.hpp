// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files behind a window that loads or saves games, on the front screen or over the park: when one opens,
// the saves in its folder are listed, ours and then the original game's, and then read one a frame, each
// one's summary and picture handed to the window. Deleting and renaming happen here, on the disk, only ever
// in our folder, and the folder is listed again after them. The game's files are marked kept, so the
// window offers neither. Saving writes our own format through the owner's writer, into our folder only;
// without a writer the window has saving switched off.
#pragma once

#include "config/user_data.hpp"
#include "render/device.hpp"
#include "ui/screens/file_dialog.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace rct3::scene
{

    struct SaveBrowser
    {
        std::optional<config::UserData> data;            ///< Where our folders are; without it only the game's show
        std::optional<std::filesystem::path> game_saves; ///< The game's folder of the player's games, if found
        std::size_t opened = 0;                          ///< The owner's count of windows when this one was listed
        std::filesystem::path folder;                    ///< Ours, where deleting, renaming and saving happen
        std::vector<std::filesystem::path> files;        ///< As the window lists them
        std::vector<bool> kept;                          ///< The game's files, which are only read
        std::size_t next = 0;                            ///< The next file to read
        std::vector<render::TextureHandle> pictures;
    };

    /// The bytes of the save to write to the path given, made when the player saves.
    using SaveWriter = std::function<std::vector<std::byte>(const std::filesystem::path&)>;

    /// Follows `dialog` (none when no window is open): lists `folder`, ours and the game's, whenever `opened`
    /// changes, reads the next file and carries out the window's requests, saving with `write`. Returns the file
    /// picked to load.
    [[nodiscard]] std::optional<std::filesystem::path> update_save_browser(render::Device& device, SaveBrowser& browser, std::optional<ui::FileDialog&> dialog, std::size_t opened, config::UserFolder folder, const SaveWriter& write = nullptr);

    void destroy_save_browser(render::Device& device, SaveBrowser& browser);

} // namespace rct3::scene
