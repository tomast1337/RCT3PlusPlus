// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sun_view.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    SunView sun_view(glm::vec3 to_sun, glm::vec3 centre, float radius, std::uint32_t texels)
    {
        const glm::vec3 towards = glm::normalize(to_sun);
        // Any up will do that is not along the sunlight.
        const glm::vec3 up = std::abs(towards.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), -towards, up);
        const float texel = 2 * radius / static_cast<float>(std::max(texels, 1u));
        glm::vec3 middle = glm::vec3(view * glm::vec4(centre, 1.0f));
        middle.x = std::floor(middle.x / texel) * texel;
        middle.y = std::floor(middle.y / texel) * texel;
        middle.z = std::floor(middle.z / texel) * texel; // Not needed for still edges; keeps the whole view in steps
        const glm::mat4 projection = glm::ortho(middle.x - radius, middle.x + radius, middle.y - radius, middle.y + radius, middle.z - kSunViewDepth, middle.z + kSunViewDepth);
        return { projection * view, texel };
    }

    float shadow_radius(float distance)
    {
        constexpr float kLeast = 40;
        constexpr float kMost = 300;
        const float wanted = std::clamp(distance * 1.5f, kLeast, kMost);
        const float steps = std::ceil(4 * std::log2(wanted / kLeast));
        return std::min(kLeast * std::exp2(steps / 4), kMost);
    }

    glm::vec3 steady_sun(glm::vec3 kept, glm::vec3 now, float step)
    {
        return glm::dot(kept, now) >= std::cos(step) ? kept : now;
    }

} // namespace rct3::park
