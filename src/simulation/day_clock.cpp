// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/day_clock.hpp"

#include "core/types.hpp"
#include "save/model/park.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::simulation
{

    namespace
    {

        /// The same time of day, brought into 0..1.
        f32 wrapped(f32 t)
        {
            return t - std::floor(t);
        }

        /// How long the window lasts, as a fraction of a day; 0 for the whole day.
        f32 length(DayWindow window)
        {
            return wrapped(window.close - window.open);
        }

    } // namespace

    DayWindow opening_hours(i32 opening_minutes, i32 closing_minutes)
    {
        const auto fraction = [](i32 minutes) { return wrapped(static_cast<f32>(minutes) / static_cast<f32>(save::model::kMinutesPerDay)); };
        return { fraction(opening_minutes), fraction(closing_minutes) };
    }

    DayMode day_mode(std::optional<i32> stored)
    {
        if (stored == static_cast<i32>(DayMode::AlwaysDay))
            return DayMode::AlwaysDay;
        if (stored == static_cast<i32>(DayMode::AlwaysNight))
            return DayMode::AlwaysNight;
        return DayMode::ParkHours;
    }

    DayWindow day_window(DayMode mode, DayWindow park_hours)
    {
        switch (mode)
        {
            case DayMode::AlwaysDay:
                return kAlwaysDayWindow;
            case DayMode::AlwaysNight:
                return kAlwaysNightWindow;
            case DayMode::ParkHours:
                break;
        }
        return park_hours;
    }

    DayTransition day_transition(i32 option)
    {
        return static_cast<DayTransition>(std::clamp(option, static_cast<i32>(DayTransition::Instant), static_cast<i32>(DayTransition::Cycle)));
    }

    bool in_window(DayWindow window, f32 time)
    {
        const f32 span = length(window);
        return span <= 0 || wrapped(time - window.open) < span;
    }

    DayClock start_day(std::optional<f32> time, DayWindow window, DayTransition transition)
    {
        DayClock clock;
        clock.window = window;
        clock.transition = transition;
        const bool usable = time && std::isfinite(*time) && *time >= 0 && *time < 1;
        clock.time = usable ? *time : window.open;
        if (!in_window(window, clock.time))
            clock.time = window.open;
        clock.shown = clock.time;
        return clock;
    }

    void change_mode(DayClock& clock, DayMode mode)
    {
        clock.mode = mode;
        clock.window = day_window(mode, clock.park_hours);
        if (mode != DayMode::ParkHours || !in_window(clock.window, clock.time))
            clock.time = clock.window.open;
        // Cycle's shown time catches up by itself; Blend fades from the old look, as when the window starts again.
        if (clock.transition == DayTransition::Instant)
            clock.shown = clock.time;
        else if (clock.transition == DayTransition::Blend)
        {
            clock.faded = clock.shown;
            clock.fade = 1;
            clock.shown = clock.time;
        }
    }

    void advance(DayClock& clock, f32 seconds)
    {
        if (!(seconds > 0))
            return;
        const f32 step = seconds / kSecondsPerHour / kHoursPerDay;
        const f32 span = length(clock.window);
        bool restarted = false;
        if (span <= 0)
            clock.time = wrapped(clock.time + step);
        else
        {
            f32 into = wrapped(clock.time - clock.window.open) + step;
            if (into >= span)
            {
                restarted = true;
                into = std::fmod(into - span, span);
            }
            clock.time = wrapped(clock.window.open + into);
        }

        switch (clock.transition)
        {
            case DayTransition::Instant:
                clock.shown = clock.time;
                break;
            case DayTransition::Blend:
                clock.fade = std::max(0.0f, clock.fade - seconds / kBlendSeconds);
                if (restarted)
                {
                    clock.faded = clock.shown;
                    clock.fade = 1;
                }
                clock.shown = clock.time;
                break;
            case DayTransition::Cycle:
            {
                // Always forwards, so the catch-up runs on through the night to the morning.
                const f32 reach = step + seconds * kCatchUpHoursPerSecond / kHoursPerDay;
                const f32 behind = wrapped(clock.time - clock.shown);
                clock.shown = behind <= reach ? clock.time : wrapped(clock.shown + reach);
                break;
            }
        }
    }

} // namespace rct3::simulation
