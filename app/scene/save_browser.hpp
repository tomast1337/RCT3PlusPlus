// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files behind the front end's window that loads games: when one opens, the saves in its folder are
// listed and then read one a frame, each one's summary and picture handed to the window. Deleting and
// renaming happen here, on the disk, and the folder is listed again after them. Loading a park is not there
// yet, so a file picked to load is only logged.
#pragma once

#include "config/user_data.hpp"
#include "render/device.hpp"
#include "ui/screens/front_end.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace rct3::scene
{

    struct SaveBrowser
    {
        std::optional<config::UserData> data; ///< Where the folders are; without it the window stays empty
        std::size_t opened = 0;               ///< The front end's count of windows when this one was listed
        std::filesystem::path folder;
        std::vector<std::filesystem::path> files;
        std::size_t next = 0; ///< The next file to read
        std::vector<render::TextureHandle> pictures;
    };

    /// Follows the window: lists its folder when it opens, reads the next file and carries out its requests.
    void update_save_browser(render::Device& device, SaveBrowser& browser, ui::FrontEnd& front);

    void destroy_save_browser(render::Device& device, SaveBrowser& browser);

} // namespace rct3::scene
