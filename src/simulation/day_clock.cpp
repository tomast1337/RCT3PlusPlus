// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/day_clock.hpp"

#include "save/model/park.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {

        /// The same time of day, brought into 0..1.
        float wrapped(float t)
        {
            return t - std::floor(t);
        }

        /// How long the window lasts, as a fraction of a day; 0 for the whole day.
        float length(DayWindow window)
        {
            return wrapped(window.close - window.open);
        }

    } // namespace

    DayWindow opening_hours(std::int32_t opening_minutes, std::int32_t closing_minutes)
    {
        const auto fraction = [](std::int32_t minutes) { return wrapped(static_cast<float>(minutes) / static_cast<float>(save::kMinutesPerDay)); };
        return { fraction(opening_minutes), fraction(closing_minutes) };
    }

    DayMode day_mode(std::optional<std::int32_t> stored)
    {
        if (stored == static_cast<std::int32_t>(DayMode::AlwaysDay))
            return DayMode::AlwaysDay;
        if (stored == static_cast<std::int32_t>(DayMode::AlwaysNight))
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

    DayTransition day_transition(int option)
    {
        return static_cast<DayTransition>(std::clamp(option, static_cast<int>(DayTransition::Instant), static_cast<int>(DayTransition::Cycle)));
    }

    bool in_window(DayWindow window, float time)
    {
        const float span = length(window);
        return span <= 0 || wrapped(time - window.open) < span;
    }

    DayClock start_day(std::optional<float> time, DayWindow window, DayTransition transition)
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

    void advance(DayClock& clock, float seconds)
    {
        if (!(seconds > 0))
            return;
        const float step = seconds / kSecondsPerHour / kHoursPerDay;
        const float span = length(clock.window);
        bool restarted = false;
        if (span <= 0)
            clock.time = wrapped(clock.time + step);
        else
        {
            float into = wrapped(clock.time - clock.window.open) + step;
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
                const float reach = step + seconds * kCatchUpHoursPerSecond / kHoursPerDay;
                const float behind = wrapped(clock.time - clock.shown);
                clock.shown = behind <= reach ? clock.time : wrapped(clock.shown + reach);
                break;
            }
        }
    }

} // namespace rct3::simulation
