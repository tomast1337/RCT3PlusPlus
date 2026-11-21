// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/swinging_part.hpp"

#include "core/types.hpp"
#include "math/portable.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::simulation
{

    void swing_on(Swing& swing, const SwingingPart& part, const glm::mat3& car, glm::vec3 acceleration, f32 seconds)
    {
        constexpr glm::vec3 kUp { 0, 1, 0 };
        constexpr f32 kSteep = 1e-3f; // A line this near straight up leaves no level to hang at; it hangs on its car
        const f32 dt = seconds;
        const glm::vec3 gravity = -kUp * units::kGravity.value;
        const glm::vec3 line = glm::normalize(car * part.line);
        f32& angle = swing.angle.value;
        f32& rate = swing.rate.value;

        // At rest it hangs as near straight down as its line lets it, unless sprung to its car.
        glm::vec3 rest = glm::normalize(car * part.up);
        if (const glm::vec3 level = kUp - line * glm::dot(kUp, line); part.spring <= 0 && glm::length(level) > kSteep)
            rest = glm::normalize(level);

        // Moved on as it was turning, its swing dying down and drawn back to rest, and held within its furthest.
        angle = std::remainder(angle + rate * dt, 2 * std::numbers::pi_v<f32>);
        rate *= math::exp(-part.damping.value * dt);
        angle *= math::exp(-part.centring.value * dt);
        if (part.furthest && std::abs(angle) >= part.furthest->value)
        {
            angle = std::copysign(part.furthest->value, angle);
            if (rate * angle > 0)
                rate = 0;
        }

        // Then turned by what it feels at the end of its arm: gravity, less the share of the car's acceleration it
        // feels, along the way it swings, and its spring; slowed while it turns by its bearing's friction under its
        // weight, which stops it dead rather than turning it back.
        const glm::vec3 hang = glm::mat3(math::rotation(angle, line)) * -rest;
        const glm::vec3 felt = gravity - part.pull.value * acceleration;
        const f32 held = part.friction * std::max(glm::dot(gravity, hang), 0.0f) * part.arm.value * dt;
        const f32 was = rate;
        rate += (glm::dot(felt, glm::cross(line, hang)) * part.arm.value - std::copysign(angle * angle * part.spring, angle)) * dt;
        if (was != 0)
            rate = (rate - std::copysign(held, was)) * was > 0 ? rate - std::copysign(held, was) : 0.0f;
        if (part.fastest)
            rate = std::clamp(rate, -part.fastest->value, part.fastest->value);
    }

} // namespace rct3::simulation
