// SPDX-License-Identifier: GPL-3.0-or-later
//
// Firework displays: a show set to music. A display file is the save container (save/save_file.hpp) holding
// one display object, which keeps
//  - its name, the music it plays to, the months it runs in and the time of day it starts;
//  - its own launchers: scenery items set out on tiles relative to the display, each by the item's name;
//  - its shots: which firework effect, from which launcher and which of the launcher's tubes, when, and on which
//    lane of the show's timeline.
// A park keeps its scheduled displays the same way, but each shot names the launcher placed in the park it fires
// from, rather than one of the display's own. A show ride's timeline is saved as a display as well; it is the
// ride's, not one the park schedules.
//
// A shot is a firework, a laser or a water jet, and a display mostly of water jets is a fountain show. In a park, a
// laser or water jet shot carries its whole description (particles/show_effect.hpp), so the park does not need the
// file the shot names.
//
// A display starts on a day of a month it runs in, as the time of day reaches its start, and runs its shots at
// their times from then on.
#pragma once

#include "core/types.hpp"
#include "particles/effect.hpp"
#include "particles/show_effect.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::save
{
    struct SaveFile;
} // namespace rct3::save

namespace rct3::particles
{

    struct Launcher
    {
        std::string item { }; ///< The scenery item's name
        i32 col = 0;
        i32 row = 0;
        i32 corner = 0;                ///< Which quarter of the tile, for items smaller than a tile
        i32 direction = 0;             ///< Quarter turns
        std::optional<u64> placed { }; ///< In a park: the placed item it is, by object id; the rest is left 0
    };

    /// What a shot puts up, by the number a park's display keeps for it.
    enum class ShotKind : u8
    {
        Firework = 0,
        Laser = 1,
        WaterJet = 2,
    };

    struct Shot
    {
        ShotKind kind = ShotKind::Firework;
        std::string effect { };            ///< The effect's name, without folder or extension
        usize launcher = 0;                ///< In Display::launchers
        i32 tube = 0;                      ///< Which of the launcher's firing points
        f32 seconds = 0;                   ///< From the start of the show
        i32 lane = 0;                      ///< The timeline row it sits on in the editor
        std::optional<JetPattern> jet { }; ///< Of a water jet shot in a park
        std::optional<Laser> laser { };    ///< Of a laser shot in a park
    };

    struct Display
    {
        std::string name { };
        std::string music_file { };
        u32 months = 0;        ///< One bit a month the display runs in, the first month lowest
        i32 start_minutes = 0; ///< Time of day it starts, minutes after midnight
        std::vector<Launcher> launchers { };
        std::vector<Shot> shots { }; ///< Earliest first
    };

    enum class DisplayError : u8
    {
        Container,   ///< Not a readable save container
        NoDisplay,   ///< It holds no display object
        BadObject,   ///< An object could not be decoded
        BadLauncher, ///< A shot names a launcher the display does not have
    };

    [[nodiscard]] const char* to_string(DisplayError e) noexcept;

    /// The display in a firework display file.
    [[nodiscard]] std::expected<Display, DisplayError> read_display(std::span<const std::byte> data);

    /// The displays `file`, a park, schedules, leaving out show rides' own; their launchers are the placed items their
    /// shots name.
    [[nodiscard]] std::expected<std::vector<Display>, DisplayError> read_park_displays(const save::SaveFile& file);

    /// What most of `display`'s shots are; fireworks when it has none.
    [[nodiscard]] ShotKind main_kind(const Display& display);

    /// Seconds from a display's start to its last shot's end: a water jet or a laser runs on past its shot.
    [[nodiscard]] f32 display_seconds(const Display& display);

    /// Game minutes after its start time during which a display due that day still starts, so a clock run fast or
    /// skipped on does not pass it by.
    inline constexpr f32 kStartWindowMinutes = 30;

    /// Whether `display` is due to start in `month` (0-based, as the calendar counts them) at `minutes` after
    /// midnight: a month it runs in, and within the window after its start time.
    [[nodiscard]] bool display_due(const Display& display, i32 month, f32 minutes);

    /// How far a display being played has got.
    struct DisplayRun
    {
        f32 seconds = 0;     ///< Since it started
        usize next_shot = 0; ///< In Display::shots, the first not gone off yet
    };

    /// Moves `run` on by `seconds`; the shots due by then that had not gone off, earliest first.
    [[nodiscard]] std::span<const Shot> advance_display(DisplayRun& run, const Display& display, f32 seconds);

} // namespace rct3::particles
