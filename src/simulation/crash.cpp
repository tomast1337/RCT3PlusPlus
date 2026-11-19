// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/crash.hpp"

#include "core/types.hpp"
#include "units/units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rct3::simulation
{

    namespace
    {
        using units::kGravity;
        constexpr units::Seconds kStep { 0.05f };   ///< A step of a flight, short enough not to pass through a wall
        constexpr f32 kKept = 0.4f;                 ///< Of its speed down, how much a bounce sends it back up with
        constexpr f32 kSkid = 0.6f;                 ///< Of its speed across, how much it keeps through a bounce
        constexpr i32 kLandings = 2;                ///< It blows up the time it comes down this often
        constexpr units::Seconds kFlightMost { 7 }; ///< It flies at most before it blows up in the air
        constexpr units::Seconds kBurnsFor { 6 };   ///< It burns where it lies before it is gone
    } // namespace

    WreckEvent fly(Wreck& w, f32 seconds, const WreckSurroundings& around)
    {
        WreckEvent event = WreckEvent::None;
        const i32 steps = static_cast<i32>(std::ceil(seconds / kStep.value));
        for (i32 step = 0; step < steps && w.state != WreckState::Gone; ++step)
        {
            const f32 dt = std::min(seconds - static_cast<f32>(step) * kStep.value, kStep.value);
            if (w.state == WreckState::Burning)
            {
                w.burnt.value += dt;
                if (w.burnt >= kBurnsFor)
                {
                    w.state = WreckState::Gone;
                    event = WreckEvent::Vanished;
                }
                continue;
            }
            w.velocity.y -= kGravity.value * dt;
            w.at += w.velocity * dt;
            if (const f32 turn = glm::length(w.spin); turn > 0)
                w.turned = glm::normalize(glm::angleAxis(turn * dt, w.spin / turn) * w.turned);
            w.flying.value += dt;
            const f32 ground = around.ground ? around.ground({ w.at.x, w.at.z }) : 0.0f;
            bool blown = w.flying >= kFlightMost || (around.struck && around.struck(w.at));
            if (w.at.y <= ground && w.velocity.y < 0)
            {
                w.at.y = ground;
                blown = blown || ++w.landings >= kLandings;
                w.velocity = { w.velocity.x * kSkid, -w.velocity.y * kKept, w.velocity.z * kSkid };
                w.spin *= kSkid;
                event = WreckEvent::Bounced;
            }
            if (blown)
            {
                w.state = WreckState::Burning;
                w.velocity = { };
                w.spin = { };
                w.at.y = std::max(w.at.y, ground);
                event = WreckEvent::BlewUp;
            }
        }
        return event;
    }

    glm::vec2 slope(const std::function<f32(glm::vec2)>& ground, glm::vec2 at)
    {
        constexpr f32 kReach = 0.1f; ///< Chosen by eye: how far either side of the point the ground is measured
        if (!ground)
            return { };
        // Of the rise towards each side, the gentler, so a cliff edge beside the point is not taken for a slope.
        const f32 here = ground(at);
        const auto rise = [&](glm::vec2 way) {
            const f32 ahead = ground(at + way * kReach) - here;
            const f32 behind = here - ground(at - way * kReach);
            return (std::abs(ahead) < std::abs(behind) ? ahead : behind) / kReach;
        };
        return { rise({ 1, 0 }), rise({ 0, 1 }) };
    }

} // namespace rct3::simulation
