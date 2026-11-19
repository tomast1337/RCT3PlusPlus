// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/park_voices.hpp"

#include "core/types.hpp"

#include <algorithm>

namespace rct3::audio
{

    namespace
    {
        constexpr units::Pace kThrillSpeed { 14 };       ///< From which a car's speed thrills
        constexpr units::Pace kWildSpeed { 30 };         ///< At which it thrills the most
        constexpr units::Pace kThrillDrop { 4 };         ///< Of drop, from which it thrills
        constexpr units::Pace kWildDrop { 20 };          ///< At which it thrills the most
        constexpr units::Frequency kMostScreams { 1.5 }; ///< On the wildest stretch
    } // namespace

    PlayParams heard(glm::vec3 at, f32 volume)
    {
        return { .volume = std::max(volume, 0.0f), .at = at, .near = kNearVoice.value, .far = kFarVoice.value };
    }

    f32 chatter_level(usize people)
    {
        return people < 2 ? 0.0f : std::min(static_cast<f32>(people) / static_cast<f32>(kCrowd), 1.0f);
    }

    f32 screams_per_second(f32 speed, f32 drop)
    {
        const f32 fast = std::clamp((speed - kThrillSpeed.value) / (kWildSpeed.value - kThrillSpeed.value), 0.0f, 1.0f);
        const f32 falling = std::clamp((drop - kThrillDrop.value) / (kWildDrop.value - kThrillDrop.value), 0.0f, 1.0f);
        return static_cast<f32>(kMostScreams.value) * std::min(fast + falling, 1.0f);
    }

} // namespace rct3::audio
