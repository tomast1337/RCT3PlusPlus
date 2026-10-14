// SPDX-License-Identifier: GPL-3.0-or-later
//
// How fast the park's time runs. The player picks one of three speeds, and pausing stops time without
// forgetting the speed, so playing again goes back to it.
#pragma once

#include <cstdint>

namespace rct3::simulation
{

    enum class Speed : std::uint8_t
    {
        Normal,
        Fast,
        Fastest,
    };

    struct GameSpeed
    {
        Speed speed = Speed::Normal;
        bool paused = false;

        friend bool operator==(const GameSpeed&, const GameSpeed&) = default;
    };

    /// Park time passed for each second of real time: none while paused.
    [[nodiscard]] float time_factor(GameSpeed speed);

    /// Plays at `speed`, unpaused.
    [[nodiscard]] GameSpeed playing(GameSpeed current, Speed speed);
    /// Pauses, or plays again at the same speed.
    [[nodiscard]] GameSpeed pause_toggled(GameSpeed current);

} // namespace rct3::simulation
