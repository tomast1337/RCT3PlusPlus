// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cars of a crashed train, thrown off the track: our own model of how a wreck flies, not the original
// game's. Each car flies on from where it left the track, falling and tumbling as it goes. It bounces off the
// ground the first time it lands and blows up the next, or as soon as it hits something standing in the park,
// or once it has been flying a while. Blown up, it burns where it lies for a while and is then gone.
//
// Kept free of the registry: the ground and what stands on it come in as functions, so it can be tested on
// made-up land. Positions are the park's world x, y (up) and z; units are metres and seconds.
#pragma once

#include "math/glm.hpp"

#include <cstdint>
#include <functional>

namespace rct3::simulation
{

    enum class WreckState : std::uint8_t
    {
        Flying,
        Burning, ///< Blown up, lying where it came down
        Gone,
    };

    /// What became of a wreck over a step, the last thing that did when several did.
    enum class WreckEvent : std::uint8_t
    {
        None,
        Bounced,
        BlewUp,
        Vanished,
    };

    struct Wreck
    {
        glm::vec3 at { };
        glm::vec3 velocity { };
        glm::vec3 spin { };              ///< Its tumble: the axis, as long as the radians a second
        glm::quat turned { 1, 0, 0, 0 }; ///< From how it sat on the track
        int landings = 0;                ///< Times it has come down on the ground
        float flying = 0;                ///< Seconds since it was thrown
        float burnt = 0;                 ///< Seconds since it blew up
        WreckState state = WreckState::Flying;
    };

    /// What a wreck flies through.
    struct WreckSurroundings
    {
        std::function<float(glm::vec2)> ground { }; ///< The ground's height under a point x, z; flat at 0 when not given
        std::function<bool(glm::vec3)> struck { };  ///< Whether something standing in the park is at a point
    };

    /// Moves `wreck` on by `seconds` through `around`.
    WreckEvent fly(Wreck& wreck, float seconds, const WreckSurroundings& around);

} // namespace rct3::simulation
