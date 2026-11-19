// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sun_view.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    SunView sun_view(glm::vec3 to_sun, glm::vec3 centre, f32 radius, u32 texels)
    {
        const glm::vec3 towards = glm::normalize(to_sun);
        // Any up will do that is not along the sunlight.
        const glm::vec3 up = std::abs(towards.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), -towards, up);
        // The square reaches half a step further each way, so it holds the one round `centre` wherever that
        // falls in its step: a fifteenth more across, the texels as much larger.
        const auto across = static_cast<f32>(std::max(texels, 1u));
        const f32 texel = 2 * radius / (across - across / kSunViewSteps);
        const f32 step = texel * std::max(1.0f, std::floor(across / kSunViewSteps));
        const f32 half = texel * across / 2;
        glm::vec3 middle = glm::vec3(view * glm::vec4(centre, 1.0f));
        middle.x = std::round(middle.x / step) * step;
        middle.y = std::round(middle.y / step) * step;
        middle.z = std::round(middle.z / step) * step; // Not needed for still edges; keeps the whole view in steps
        const glm::mat4 projection = glm::ortho(middle.x - half, middle.x + half, middle.y - half, middle.y + half, middle.z - kSunViewDepth, middle.z + kSunViewDepth);
        return { projection * view, { texel } };
    }

    f32 shadow_radius(f32 distance)
    {
        constexpr f32 kLeast = 40;
        constexpr f32 kMost = 300;
        const f32 wanted = std::clamp(distance * 1.5f, kLeast, kMost);
        const f32 steps = std::ceil(4 * std::log2(wanted / kLeast));
        return std::min(kLeast * std::exp2(steps / 4), kMost);
    }

    glm::vec3 steady_sun(glm::vec3 kept, glm::vec3 now, f32 step)
    {
        return glm::dot(kept, now) >= std::cos(step) ? kept : now;
    }

    f32 sun_step_share(glm::vec3 kept, glm::vec3 now, f32 step)
    {
        if (step <= 0)
            return 1;
        // Through the cross product: accurate at the half degree steps, where an arc cosine of nearly 1 is not.
        return std::clamp(std::atan2(glm::length(glm::cross(kept, now)), glm::dot(kept, now)) / step, 0.0f, 1.0f);
    }

} // namespace rct3::park
