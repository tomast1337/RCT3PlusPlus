// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/game_speed.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("time runs faster at each speed and stops while paused")
{
    CHECK(time_factor({ Speed::Normal, false }) == 1);
    CHECK(time_factor({ Speed::Fast, false }) > 1);
    CHECK(time_factor({ Speed::Fastest, false }) > time_factor({ Speed::Fast, false }));
    CHECK(time_factor({ Speed::Fastest, true }) == 0);
}

TEST_CASE("pausing keeps the speed, and picking a speed plays")
{
    const GameSpeed fast { Speed::Fast, false };
    const GameSpeed paused = pause_toggled(fast);
    CHECK(paused.paused);
    CHECK(paused.speed == Speed::Fast);
    CHECK(pause_toggled(paused) == fast);
    CHECK(playing(paused, Speed::Normal) == GameSpeed { Speed::Normal, false });
}
