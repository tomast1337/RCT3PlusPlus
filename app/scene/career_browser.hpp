// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files behind the front end's window that picks a career. When it opens, the original game's career
// files found in the careers folder (or a folder in it) are brought over first: each becomes one of ours,
// its games copied along, and the old file and its games are moved to "imported" so they are brought over
// only once. Then the game's careers are listed, fitted to its scenarios, and shown, the picture of each
// career's last game read one a frame. New, rename and delete happen here, on the disk.
//
// Loading a career shows its map. A scenario picked there becomes the career's current one, saved at once,
// and the slideshow behind turns to that scenario's pictures until the map closes. Starting a scenario plays
// its park from the game's folder; resuming plays its last save, and loading lists its games. Its games are
// kept in the career's folder, one folder per scenario, and the park counts for the career. The map stays
// under the park, and shows the career as the park left it once the player is back.
#pragma once

#include "career/career_file.hpp"
#include "config/game_install.hpp"
#include "config/user_data.hpp"
#include "render/device.hpp"
#include "scene/save_browser.hpp"
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
        std::optional<career::CareerEntry> playing; ///< The career whose map is shown
        std::size_t map_opened = 0;                 ///< The front end's count of maps when it was shown
        /// The folder of pictures the slideshow should show, relative to the game; nothing for its usual ones.
        std::optional<std::string> slideshow;
        std::optional<std::filesystem::path> park; ///< A scenario's park or a career's game to play, until taken
        ParkGame game;                             ///< What that is played as; also what the games window loads
        bool played = false;                       ///< A park was started from the map, which is out of date when back
    };

    void update_career_browser(render::Device& device, CareerBrowser& browser, ui::FrontEnd& front, const ui::StringTable& strings, const config::GameInstall& install);

    /// Starts scenario `index` of the first career of game `pack`, made when there is none, as its map's New
    /// does, the map shown under the park. False when the scenario is not there or not open.
    bool start_career_scenario(CareerBrowser& browser, ui::FrontEnd& front, int pack, std::size_t index, const ui::StringTable& strings, const config::GameInstall& install);

    void destroy_career_browser(render::Device& device, CareerBrowser& browser);

} // namespace rct3::scene
