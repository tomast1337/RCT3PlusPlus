// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/spinning_part.hpp"

#include "core/types.hpp"
#include "math/portable.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::simulation
{

    namespace
    {
        constexpr f32 kHeldFaster = 10;                         ///< Chosen by eye: a part held still dies down this many times faster than its own damping
        constexpr units::TurnRate kStill { 0.2f };              ///< Chosen by eye: a part slowing to a stop stops once it spins slower than this
        constexpr units::TurnRate kSquarePace { 0.8f };         ///< Chosen by eye: a part squaring up turns on to its stop at this, slow enough to look deliberate
        constexpr units::TurnAcceleration kSquareEase { 1.5f }; ///< Chosen by eye: and is eased to that pace by this
        constexpr f32 kSettleFaster = 4;                        ///< Chosen by eye: a part settling at a station dies down this many times faster than its own damping
    } // namespace

    void spin_on(Spin& spin, const SpinningPart& part, const SpinCar& car, f32 seconds)
    {
        constexpr f32 kTurn = 2 * std::numbers::pi_v<f32>;
        const f32 dt = seconds;
        f32& angle = spin.angle.value;
        f32& rate = spin.rate.value;
        if (car.ground != SpinGround::Station)
            spin.squaring = Squaring::Free;
        if (spin.squaring == Squaring::Squared)
            return;

        // Its car turns under it: its angle on the car moves by its own spin less the car's turning.
        const f32 was = angle;
        const f32 moved = angle + (rate - car.turning.value) * dt;
        angle = std::remainder(moved, kTurn);

        if (car.ground == SpinGround::Station && part.rest == SpinRest::Settles)
        {
            rate *= math::exp(-kSettleFaster * part.damping.value * dt);
            if (std::abs(rate) < kStill.value)
            {
                rate = 0;
                spin.squaring = Squaring::Squared;
            }
            return;
        }
        if (car.ground == SpinGround::Station)
        {
            // Squaring up: on the way it already spins, or the shorter way back to facing ahead when all but still,
            // eased to a slow pace; it stops on the first stop it comes to at that pace.
            const f32 step = kTurn / static_cast<f32>(std::max(part.stops, 1u));
            const f32 back = std::abs(rate) < kStill.value ? -std::copysign(1.0f, angle) : std::copysign(1.0f, rate);
            const f32 target = back * kSquarePace.value;
            const f32 ease = kSquareEase.value * dt;
            rate = std::clamp(target, rate - ease, rate + ease);
            spin.squaring = Squaring::Turning;
            const f32 before = std::floor(was / step);
            const f32 after = std::floor(moved / step);
            if ((before != after || moved == after * step) && std::abs(rate) <= kSquarePace.value + 1e-4f)
            {
                angle = std::remainder(std::max(before, after) * step, kTurn);
                rate = 0;
                spin.squaring = Squaring::Squared;
            }
            return;
        }
        if (car.ground == SpinGround::Chain && part.held_on_chains)
        {
            rate *= math::exp(-kHeldFaster * part.damping.value * dt);
            if (std::abs(rate) < kStill.value)
                rate = 0;
            return;
        }

        // Free: its spin dies down, and its weight is turned by what it feels at the end of its lever, across the
        // car's up: gravity, less the share of the car's acceleration it feels. Seen in the car's own space, where
        // up is +Y.
        rate *= math::exp(-part.damping.value * dt);
        const glm::mat3 to_car = glm::transpose(car.axes);
        const glm::vec3 felt = to_car * (glm::vec3(0, -units::kGravity.value * part.gravity.value, 0) - part.pull.value * car.acceleration);
        const glm::vec3 lever = glm::mat3(math::rotation(angle, glm::vec3(0, 1, 0))) * glm::vec3(part.lever.x, 0, part.lever.z);
        const f32 turned = lever.z * felt.x - lever.x * felt.z;
        // Its bearing's friction under its weight holds it back, stopping it dead rather than turning it back; and its
        // spring draws it back to face ahead.
        const f32 weight = std::max(units::kGravity.value * car.axes[1].y, 0.0f);
        const f32 held = part.friction * weight * glm::length(lever) * dt;
        const f32 before = rate;
        rate += (turned - part.spring * angle) * dt;
        if (before != 0)
            rate = (rate - std::copysign(held, before)) * before > 0 ? rate - std::copysign(held, before) : 0.0f;
        if (part.furthest)
            angle = std::clamp(angle, -part.furthest->value, part.furthest->value);
        if (part.fastest)
            rate = std::clamp(rate, -part.fastest->value, part.fastest->value);
    }

} // namespace rct3::simulation
