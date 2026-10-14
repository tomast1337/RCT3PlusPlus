// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/game_speed.hpp"

namespace rct3::simulation
{

    namespace
    {

        constexpr float kFastFactor = 2;
        constexpr float kFastestFactor = 5;

    } // namespace

    float time_factor(GameSpeed speed)
    {
        if (speed.paused)
            return 0;
        switch (speed.speed)
        {
            case Speed::Fast:
                return kFastFactor;
            case Speed::Fastest:
                return kFastestFactor;
            case Speed::Normal:
                break;
        }
        return 1;
    }

    GameSpeed playing(GameSpeed, Speed speed)
    {
        return { speed, false };
    }

    GameSpeed pause_toggled(GameSpeed current)
    {
        current.paused = !current.paused;
        return current;
    }

} // namespace rct3::simulation
