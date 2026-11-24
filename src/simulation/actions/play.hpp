// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions on how the park plays: its speed, pausing it, and its day and night.
#pragma once

#include "simulation/actions/world.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/game_speed.hpp"

namespace rct3::simulation::actions
{

    /// Runs the park at a speed, or pauses it.
    struct SetSpeed
    {
        GameSpeed speed { };
    };

    /// Pauses the park, or plays it again at the speed it had.
    struct TogglePause
    {
    };

    /// Switches the day and night to a mode (change_mode).
    struct SetDayMode
    {
        DayMode mode { };
    };

    [[nodiscard]] Outcome check(const World& world, const SetSpeed& action);
    void apply(World& world, const SetSpeed& action);
    [[nodiscard]] Outcome check(const World& world, const TogglePause& action);
    void apply(World& world, const TogglePause& action);
    [[nodiscard]] Outcome check(const World& world, const SetDayMode& action);
    void apply(World& world, const SetDayMode& action);

} // namespace rct3::simulation::actions
