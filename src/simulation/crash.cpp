// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/crash.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {
        constexpr float kGravity = 9.81f;
        constexpr float kStep = 0.05f;   ///< Seconds per step of a flight, short enough not to pass through a wall
        constexpr float kKept = 0.4f;    ///< Of its speed down, how much a bounce sends it back up with
        constexpr float kSkid = 0.6f;    ///< Of its speed across, how much it keeps through a bounce
        constexpr int kLandings = 2;     ///< It blows up the time it comes down this often
        constexpr float kFlightMost = 7; ///< Seconds it flies at most before it blows up in the air
        constexpr float kBurnsFor = 6;   ///< Seconds it burns where it lies before it is gone
    } // namespace

    WreckEvent fly(Wreck& w, float seconds, const WreckSurroundings& around)
    {
        WreckEvent event = WreckEvent::None;
        const int steps = static_cast<int>(std::ceil(seconds / kStep));
        for (int step = 0; step < steps && w.state != WreckState::Gone; ++step)
        {
            const float dt = std::min(seconds - static_cast<float>(step) * kStep, kStep);
            if (w.state == WreckState::Burning)
            {
                w.burnt += dt;
                if (w.burnt >= kBurnsFor)
                {
                    w.state = WreckState::Gone;
                    event = WreckEvent::Vanished;
                }
                continue;
            }
            w.velocity.y -= kGravity * dt;
            w.at += w.velocity * dt;
            if (const float turn = glm::length(w.spin); turn > 0)
                w.turned = glm::normalize(glm::angleAxis(turn * dt, w.spin / turn) * w.turned);
            w.flying += dt;
            const float ground = around.ground ? around.ground({ w.at.x, w.at.z }) : 0.0f;
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

} // namespace rct3::simulation
