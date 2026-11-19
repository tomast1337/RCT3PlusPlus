// SPDX-License-Identifier: GPL-3.0-or-later
//
// How loud the park's people are, heard from the camera: our own rules, not the original game's.
//
// A sound in the park is placed where it happens and heard from the eye: full loud up to kNearVoice, fading
// evenly to nothing at kFarVoice (the engine pans and fades it). The crowd's chatter grows with the people around the point
// the camera looks at, from silence with a lone guest to full with a crowd, however far the eye is. Riders scream more the faster
// their car goes and the faster it drops.
#pragma once

#include "audio/audio.hpp"
#include "core/types.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <glm/vec3.hpp>

namespace rct3::audio
{

    inline constexpr units::Distance kNearVoice { 12 };
    inline constexpr units::Distance kFarVoice { 400 }; ///< As far as the camera goes
    inline constexpr usize kCrowd = 30;                 ///< People about the camera's point for the chatter at full

    /// A sound at `at`, at `volume` when near.
    [[nodiscard]] PlayParams heard(glm::vec3 at, f32 volume);

    /// The chatter's loudness, 0..1, with `people` about the camera's point.
    [[nodiscard]] f32 chatter_level(usize people);

    /// Screams a second from one car's riders, going at `speed` and dropping at `drop` (both a second, world units).
    [[nodiscard]] f32 screams_per_second(f32 speed, f32 drop);

} // namespace rct3::audio
