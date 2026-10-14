// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/day_clock.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    constexpr float hours(float h)
    {
        return h / kHoursPerDay;
    }

    /// Real seconds that move the clock on by `h` game hours.
    constexpr float seconds_for(float h)
    {
        return h * kSecondsPerHour;
    }
} // namespace

TEST_CASE("opening hours become a window of the day")
{
    const DayWindow w = opening_hours(8 * 60, 18 * 60 + 30);
    CHECK(w.open == doctest::Approx(hours(8)));
    CHECK(w.close == doctest::Approx(hours(18.5f)));
    // Closing at 24:00 is closing at midnight.
    CHECK(opening_hours(8 * 60, 24 * 60).close == doctest::Approx(0));
    CHECK(opening_hours(-60, 0).open == doctest::Approx(hours(23)));

    SUBCASE("a window may run past midnight")
    {
        const DayWindow late = opening_hours(17 * 60, 2 * 60);
        CHECK(in_window(late, hours(23)));
        CHECK(in_window(late, hours(1)));
        CHECK_FALSE(in_window(late, hours(2)));
        CHECK_FALSE(in_window(late, hours(12)));
        CHECK(in_window(late, hours(17)));
    }
    SUBCASE("a window that opens when it closes is the whole day")
    {
        CHECK(in_window(opening_hours(600, 600), hours(3)));
    }
}

TEST_CASE("the stored mode picks the window")
{
    const DayWindow park { hours(8), hours(20) };
    CHECK(day_mode(std::nullopt) == DayMode::ParkHours);
    CHECK(day_mode(0) == DayMode::ParkHours);
    CHECK(day_mode(1) == DayMode::AlwaysDay);
    CHECK(day_mode(2) == DayMode::AlwaysNight);
    CHECK(day_mode(7) == DayMode::ParkHours);
    CHECK(day_window(DayMode::ParkHours, park) == park);
    CHECK(day_window(DayMode::AlwaysDay, park) == kAlwaysDayWindow);
    CHECK(day_window(DayMode::AlwaysNight, park) == kAlwaysNightWindow);
    // The day window lies round noon, the night window round midnight.
    CHECK(in_window(kAlwaysDayWindow, hours(12)));
    CHECK_FALSE(in_window(kAlwaysDayWindow, hours(0)));
    CHECK(in_window(kAlwaysNightWindow, hours(0)));
    CHECK_FALSE(in_window(kAlwaysNightWindow, hours(12)));

    CHECK(day_transition(0) == DayTransition::Instant);
    CHECK(day_transition(1) == DayTransition::Blend);
    CHECK(day_transition(2) == DayTransition::Cycle);
    CHECK(day_transition(9) == DayTransition::Cycle);
    CHECK(day_transition(-1) == DayTransition::Instant);
}

TEST_CASE("the clock starts inside its window")
{
    const DayWindow w { hours(8), hours(20) };
    CHECK(start_day(hours(13), w, DayTransition::Instant).time == doctest::Approx(hours(13)));
    CHECK(start_day(hours(22), w, DayTransition::Instant).time == doctest::Approx(hours(8)));
    CHECK(start_day(std::nullopt, w, DayTransition::Instant).time == doctest::Approx(hours(8)));
    CHECK(start_day(-1.0f, w, DayTransition::Instant).time == doctest::Approx(hours(8)));
    CHECK(start_day(1.5f, w, DayTransition::Instant).time == doctest::Approx(hours(8)));
    const DayClock c = start_day(hours(13), w, DayTransition::Cycle);
    CHECK(c.shown == c.time);
    CHECK(c.fade == 0);
}

TEST_CASE("the clock runs at its rate and skips the hours outside the window")
{
    DayClock c = start_day(hours(10), { hours(8), hours(20) }, DayTransition::Instant);
    advance(c, seconds_for(3));
    CHECK(c.time == doctest::Approx(hours(13)));
    CHECK(c.shown == c.time);
    advance(c, 0);
    advance(c, -5);
    CHECK(c.time == doctest::Approx(hours(13)));

    // Seven hours on from 13:00 reaches closing time, so it is 08:00 again; one more is 09:00.
    advance(c, seconds_for(8));
    CHECK(c.time == doctest::Approx(hours(9)));
    CHECK(c.shown == c.time);

    SUBCASE("a whole-day window never skips")
    {
        DayClock all = start_day(hours(22), { }, DayTransition::Instant);
        advance(all, seconds_for(4));
        CHECK(all.time == doctest::Approx(hours(2)));
    }
    SUBCASE("a window past midnight runs through it")
    {
        DayClock late = start_day(hours(23), { hours(17), hours(2) }, DayTransition::Instant);
        advance(late, seconds_for(2));
        CHECK(late.time == doctest::Approx(hours(1)));
        advance(late, seconds_for(2));
        CHECK(late.time == doctest::Approx(hours(18)));
    }
    SUBCASE("a long step stays in the window")
    {
        DayClock far = start_day(hours(10), { hours(8), hours(20) }, DayTransition::Instant);
        advance(far, seconds_for(100));
        CHECK(in_window(far.window, far.time));
    }
}

TEST_CASE("blend fades the closing look into the opening look")
{
    DayClock c = start_day(hours(19.5f), { hours(8), hours(20) }, DayTransition::Blend);
    advance(c, seconds_for(1));
    CHECK(c.time == doctest::Approx(hours(8.5f)));
    CHECK(c.shown == c.time);
    CHECK(c.faded == doctest::Approx(hours(19.5f)));
    CHECK(c.fade == 1);
    advance(c, kBlendSeconds / 2);
    CHECK(c.fade == doctest::Approx(0.5f));
    advance(c, kBlendSeconds);
    CHECK(c.fade == 0);
}

TEST_CASE("cycle runs the shown time on through the skipped hours")
{
    DayClock c = start_day(hours(19.999f), { hours(8), hours(20) }, DayTransition::Cycle);
    advance(c, 0.1f);
    CHECK(c.time == doctest::Approx(hours(8)).epsilon(1e-3));
    // The look runs forwards from closing time, not back, and much faster than the clock.
    CHECK(c.shown > hours(20.2f));
    CHECK(c.fade == 0);

    // It catches up within the skipped hours' worth of seconds at its rate.
    const float skipped = 12.0f;
    float waited = 0;
    while (c.shown != c.time && waited < skipped / kCatchUpHoursPerSecond + 1)
    {
        advance(c, 0.1f);
        waited += 0.1f;
    }
    CHECK(c.shown == c.time);
    CHECK(waited < skipped / kCatchUpHoursPerSecond + 1);
    // Once caught up it follows the clock.
    advance(c, seconds_for(1));
    CHECK(c.shown == c.time);
}
