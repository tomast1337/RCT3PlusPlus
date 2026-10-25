// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's time of day. It runs only while the park keeps its day: from opening time to closing time, or
// through one of the fixed windows the Day and Night modes keep. On reaching the end of the window it starts
// again at the beginning; the hours between are skipped, not played faster.
//
// Times of day are fractions of a day from midnight, 0 up to 1, as the park file keeps them. The clock
// holds two: the time the park is at, and the time the sky and the light show. The transition option sets
// how the look gets from closing time to opening time when the window starts again:
//
//  - Instant: the look jumps with the clock.
//  - Blend: the look of closing time fades into the look of opening time over a few seconds.
//  - Cycle: the shown time runs on through the skipped hours, many times faster than the clock, until it
//    reaches the clock again, so the night passes by in a moment.
//
// Every rate and window here is ours to tune.
#pragma once

#include <cstdint>
#include <optional>

namespace rct3::simulation
{

    inline constexpr float kHoursPerDay = 24.0f;
    /// Real seconds a game hour lasts at normal speed.
    inline constexpr float kSecondsPerHour = 40.0f;
    /// Blend: real seconds the look of closing time takes to fade into the look of opening time.
    inline constexpr float kBlendSeconds = 4.0f;
    /// Cycle: game hours per real second the shown time runs through the skipped hours.
    inline constexpr float kCatchUpHoursPerSecond = 6.0f;

    /// The part of the day the park keeps, as fractions of a day. It may run past midnight, closing before
    /// it opens. A window that opens when it closes is the whole day: nothing is skipped.
    struct DayWindow
    {
        float open = 0;
        float close = 0;

        friend bool operator==(const DayWindow&, const DayWindow&) = default;
    };

    /// What the park file asks the day to do, in the order the file numbers the modes.
    enum class DayMode : std::uint8_t
    {
        ParkHours,   ///< From opening time to closing time
        AlwaysDay,   ///< Round the middle of the day
        AlwaysNight, ///< Round the middle of the night
    };

    /// How the look follows the clock when the window starts again, in the order of the game option.
    enum class DayTransition : std::uint8_t
    {
        Instant,
        Blend,
        Cycle,
    };

    /// The windows the Day and Night modes keep, well clear of dawn and dusk.
    inline constexpr DayWindow kAlwaysDayWindow { 10.0f / kHoursPerDay, 16.0f / kHoursPerDay };
    inline constexpr DayWindow kAlwaysNightWindow { 22.0f / kHoursPerDay, 3.0f / kHoursPerDay };

    /// The window from opening to closing time, in minutes since midnight. Minutes outside a day are taken
    /// round the clock, so closing at 24:00 is closing at midnight.
    [[nodiscard]] DayWindow opening_hours(std::int32_t opening_minutes, std::int32_t closing_minutes);

    /// The mode a park file stores. Nothing, or a number with no mode, is ParkHours.
    [[nodiscard]] DayMode day_mode(std::optional<std::int32_t> stored);

    /// The window `mode` keeps, given the park's opening hours.
    [[nodiscard]] DayWindow day_window(DayMode mode, DayWindow park_hours);

    /// The transition for the option's value; values out of range give the nearest.
    [[nodiscard]] DayTransition day_transition(int option);

    /// True when `time` lies in the window, its opening included and its closing not.
    [[nodiscard]] bool in_window(DayWindow window, float time);

    struct DayClock
    {
        DayWindow window;
        DayMode mode = DayMode::ParkHours; ///< Which window `window` is
        DayWindow park_hours;              ///< The park's opening hours, the window of ParkHours
        DayTransition transition = DayTransition::Instant;
        float time = 0.5f;  ///< Where the park is in its day
        float shown = 0.5f; ///< What the sky and the light show; behind `time` while Cycle catches up
        float faded = 0;    ///< Blend: the time whose look is fading out
        float fade = 0;     ///< Blend: how much of that look still shows, 1 down to 0
    };

    /// A clock at `time` (a stored value; anything outside 0..1 is ignored), moved into the window when it
    /// lies outside it, showing the time it is at.
    [[nodiscard]] DayClock start_day(std::optional<float> time, DayWindow window, DayTransition transition);

    /// Switches the clock to `mode`'s window. The Day and Night modes start their window from the beginning;
    /// the park's hours keep the time when it lies in them. The look gets there as the transition says.
    void change_mode(DayClock& clock, DayMode mode);

    /// Moves the clock on by `seconds` of real time at normal speed.
    void advance(DayClock& clock, float seconds);

} // namespace rct3::simulation
