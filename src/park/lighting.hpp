// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fixed light the park is shaded with until it has real lighting: one sun and a floor of ambient light.
#pragma once

#include "math/glm.hpp"

#include <cmath>

namespace rct3::park
{

    inline const glm::vec3 kSun = glm::normalize(glm::vec3(-0.4f, 1.0f, -0.3f));
    inline constexpr float kAmbient = 0.45f; ///< Light on a face turned away from the sun

    /// Brightness of a surface facing `normal` (any length), either side lit alike.
    [[nodiscard]] inline float sunlight(const glm::vec3& normal)
    {
        const float length = glm::length(normal);
        if (length <= 0)
            return 1;
        return kAmbient + (1 - kAmbient) * std::abs(glm::dot(normal / length, kSun));
    }

} // namespace rct3::park
