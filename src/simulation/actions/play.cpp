// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/play.hpp"

namespace rct3::simulation::actions
{

    Outcome check(const World&, const SetSpeed&)
    {
        return Checked { };
    }

    void apply(World& world, const SetSpeed& action)
    {
        world.park.speed = action.speed;
    }

    Outcome check(const World&, const TogglePause&)
    {
        return Checked { };
    }

    void apply(World& world, const TogglePause&)
    {
        world.park.speed = pause_toggled(world.park.speed);
    }

    Outcome check(const World&, const SetDayMode&)
    {
        return Checked { };
    }

    void apply(World& world, const SetDayMode& action)
    {
        change_mode(world.day_clock, action.mode);
    }

} // namespace rct3::simulation::actions
