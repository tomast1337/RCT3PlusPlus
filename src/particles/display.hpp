// SPDX-License-Identifier: GPL-3.0-or-later
//
// Firework displays: a show set to music. A display file is the save container (save/save_file.hpp) holding
// one display object, which keeps
//  - its name, the music it plays to, the months it runs in and the time of day it starts;
//  - its own launchers: scenery items set out on tiles relative to the display, each by the item's name;
//  - its shots: which firework effect, from which launcher and which of the launcher's tubes, when, and on which
//    lane of the show's timeline.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace rct3::particles
{

    struct Launcher
    {
        std::string item { }; ///< The scenery item's name
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::int32_t corner = 0;    ///< Which quarter of the tile, for items smaller than a tile
        std::int32_t direction = 0; ///< Quarter turns
    };

    struct Shot
    {
        std::string effect { };   ///< The firework effect's name, without folder or extension
        std::size_t launcher = 0; ///< In Display::launchers
        std::int32_t tube = 0;    ///< Which of the launcher's firing points
        float seconds = 0;        ///< From the start of the show
        std::int32_t lane = 0;    ///< The timeline row it sits on in the editor
    };

    struct Display
    {
        std::string name { };
        std::string music { };          ///< A music file's name
        std::uint32_t months = 0;       ///< One bit a month the display runs in, the first month lowest
        std::int32_t start_minutes = 0; ///< Time of day it starts, minutes after midnight
        std::vector<Launcher> launchers { };
        std::vector<Shot> shots { }; ///< Earliest first
    };

    enum class DisplayError : std::uint8_t
    {
        Container,   ///< Not a readable save container
        NoDisplay,   ///< It holds no display object
        BadObject,   ///< An object could not be decoded
        BadLauncher, ///< A shot names a launcher the display does not have
    };

    [[nodiscard]] const char* to_string(DisplayError e) noexcept;

    /// The display in a firework display file.
    [[nodiscard]] std::expected<Display, DisplayError> read_display(std::span<const std::byte> data);

} // namespace rct3::particles
