// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/ride_walks.hpp"

#include "simulation/components.hpp"
#include "simulation/rides.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/systems.hpp"

#include <cmath>

namespace rct3::simulation
{

    void step_ride_walks(SimWorld& world, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;

        phase.emplace("Ride walks");
        // Riders on their feet walk on to their seats, stopping at the ticket spot first, or off to the paths.
        for (const entt::entity g : sorted(r.view<RideWalk>()))
        {
            auto& walk = r.get<RideWalk>(g);
            auto& pose = r.get<Pose>(g);
            const entt::entity ride = r.get<Errand>(g).target;
            if (!walk.off && walk.next == 1 && walk.pause.value > 0)
            {
                walk.pause.value -= dt;
                if (const auto* walks = r.valid(ride) ? r.try_get<RideWalks>(ride) : nullptr)
                    pose.heading.value = walks->ticket_heading.value;
                continue;
            }
            float stride = r.get<Stride>(g).speed.value * dt;
            while (stride > 0 && walk.next < walk.points.size())
            {
                const glm::vec3 to = walk.points[walk.next] - pose.position;
                const float left = glm::length(to);
                if (glm::length(glm::vec2(to.x, to.z)) > 0)
                    pose.heading.value = std::atan2(to.x, to.z);
                if (left > stride)
                {
                    pose.position += to * (stride / left);
                    break;
                }
                pose.position = walk.points[walk.next++];
                stride -= left;
                if (!walk.off && walk.next == 1)
                    break; // At the ticket spot
            }
            if (walk.next < walk.points.size())
                continue;
            const std::optional<GestureKind> gesture = walk.gesture;
            const bool off = walk.off;
            r.remove<RideWalk>(g);
            if (off && r.valid(ride) && r.all_of<CycleRide>(ride))
                let_out(r, paths, g, ride, gesture);
        }
    }

} // namespace rct3::simulation
