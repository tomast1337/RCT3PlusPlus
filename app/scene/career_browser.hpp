// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files behind the front end's window that picks a career. When it opens, the original game's career
// files found in the careers folder (or a folder in it) are brought over first: each becomes one of ours,
// its games copied along, and the old file and its games are moved to "imported" so they are brought over
// only once. Then the game's careers are listed, fitted to its scenarios, and shown, the picture of each
// career's last game read one a frame. New, rename and delete happen here, on the disk. The career map is
// not there yet, so loading a career is only logged.
#pragma once

#include "career/career_file.hpp"
#include "config/user_data.hpp"
#include "render/device.hpp"
#include "ui/screens/front_end.hpp"
#include "ui/string_table.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace rct3::scene
{

    struct CareerBrowser
    {
        std::optional<config::UserData> data; ///< Where the careers are; without it none are kept
        std::size_t opened = 0;               ///< The front end's count of windows when this one was listed
        std::filesystem::path folder;
        std::vector<std::string> ids; ///< The game's scenarios
        std::vector<career::CareerEntry> careers;
        std::size_t next_picture = 0; ///< The next career whose picture to read
        std::vector<render::TextureHandle> pictures;
    };

    void update_career_browser(render::Device& device, CareerBrowser& browser, ui::FrontEnd& front, const ui::StringTable& strings);

    void destroy_career_browser(render::Device& device, CareerBrowser& browser);

} // namespace rct3::scene
