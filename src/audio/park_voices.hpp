// SPDX-License-Identifier: GPL-3.0-or-later
//
// How loud the park's people are, heard from the camera: our own rules, not the original game's.
//
// A sound in the park is full loud up to kNearVoice from the eye and fades evenly to nothing at kFarVoice;
// it pans toward the side of the view it is on. The crowd's chatter grows with the people around the point
// the camera looks at, from silence with a lone guest to full with a crowd, however far the eye is. Riders scream more the faster
// their car goes and the faster it drops.
#pragma once

#include "audio/audio.hpp"

#include <cstddef>
#include <glm/vec3.hpp>

namespace rct3::audio
{

    inline constexpr float kNearVoice = 12;   ///< World units
    inline constexpr float kFarVoice = 400;   ///< World units, as far as the camera goes
    inline constexpr std::size_t kCrowd = 30; ///< People about the camera's point for the chatter at full

    /// How a sound at `at` is heard from `eye`, `right` being the view's right (length 1), at `volume` when near.
    [[nodiscard]] PlayParams heard(glm::vec3 eye, glm::vec3 right, glm::vec3 at, float volume);

    /// The chatter's loudness, 0..1, with `people` about the camera's point.
    [[nodiscard]] float chatter_level(std::size_t people);

    /// Screams a second from one car's riders, going at `speed` and dropping at `drop` (both a second, world units).
    [[nodiscard]] float screams_per_second(float speed, float drop);

} // namespace rct3::audio
