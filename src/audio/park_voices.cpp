// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/park_voices.hpp"

#include <algorithm>

namespace rct3::audio
{

    namespace
    {
        constexpr float kThrillSpeed = 14;   ///< World units a second from which a car's speed thrills
        constexpr float kWildSpeed = 30;     ///< At which it thrills the most
        constexpr float kThrillDrop = 4;     ///< World units a second of drop from which it thrills
        constexpr float kWildDrop = 20;      ///< At which it thrills the most
        constexpr float kMostScreams = 1.5f; ///< A second, on the wildest stretch
    } // namespace

    PlayParams heard(glm::vec3 at, float volume)
    {
        return { .volume = std::max(volume, 0.0f), .at = at, .near = kNearVoice, .far = kFarVoice };
    }

    float chatter_level(std::size_t people)
    {
        return people < 2 ? 0.0f : std::min(static_cast<float>(people) / static_cast<float>(kCrowd), 1.0f);
    }

    float screams_per_second(float speed, float drop)
    {
        const float fast = std::clamp((speed - kThrillSpeed) / (kWildSpeed - kThrillSpeed), 0.0f, 1.0f);
        const float falling = std::clamp((drop - kThrillDrop) / (kWildDrop - kThrillDrop), 0.0f, 1.0f);
        return kMostScreams * std::min(fast + falling, 1.0f);
    }

} // namespace rct3::audio
