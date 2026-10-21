// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/bone_poses.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{
    namespace
    {
        /// The keys either side of `time`, and how far from the first to the second.
        std::tuple<std::size_t, std::size_t, float> around(const BoneKeys& keys, float time)
        {
            const auto after = std::ranges::upper_bound(keys.times, time) - keys.times.begin();
            if (after == 0)
                return { 0, 0, 0.0f };
            const auto to = static_cast<std::size_t>(after);
            if (to >= keys.times.size())
                return { keys.times.size() - 1, keys.times.size() - 1, 0.0f };
            const float span = keys.times[to] - keys.times[to - 1];
            return { to - 1, to, span > 0 ? (time - keys.times[to - 1]) / span : 0.0f };
        }

        glm::quat key_quat(const glm::vec3& axis_angle)
        {
            const float angle = glm::length(axis_angle);
            return angle > 1e-6f ? glm::angleAxis(angle, axis_angle / angle) : glm::quat(1, 0, 0, 0);
        }
    } // namespace

    glm::mat4 key_turn(const glm::vec3& axis_angle)
    {
        return glm::mat4_cast(key_quat(axis_angle));
    }

    std::vector<glm::mat4> bone_moves(const BoneRig& rig, std::size_t motion, float time)
    {
        const std::size_t count = rig.parents.size();
        std::vector<glm::mat4> local = rig.rest_local;
        if (motion < rig.motions.size())
        {
            const BoneMotion& m = rig.motions[motion];
            for (std::size_t b = 0; b < count; ++b)
            {
                if (b < m.rotations.size() && !m.rotations[b].times.empty())
                {
                    // shortcut: turns between keys blend along the shortest way, upgrade if a ride shows keys
                    // more than half a turn apart.
                    const auto [from, to, along] = around(m.rotations[b], time);
                    const glm::mat4 turn = glm::mat4_cast(glm::slerp(key_quat(m.rotations[b].values[from]), key_quat(m.rotations[b].values[to]), along));
                    const glm::vec4 shift = local[b][3];
                    local[b] = turn;
                    local[b][3] = shift;
                }
                if (b < m.positions.size() && !m.positions[b].times.empty())
                {
                    const auto [from, to, along] = around(m.positions[b], time);
                    local[b][3] = glm::vec4(glm::mix(m.positions[b].values[from], m.positions[b].values[to], along), 1.0f);
                }
            }
        }
        // Each bone where its parent carries it; a parent may come after its children.
        std::vector<glm::mat4> world(count);
        std::vector<char> done(count, 0);
        const auto solve = [&](auto&& self, std::size_t b, std::size_t depth) -> const glm::mat4& {
            if (!done[b])
            {
                const std::int32_t p = rig.parents[b];
                const bool has_parent = p >= 0 && static_cast<std::size_t>(p) < count && depth < count;
                world[b] = has_parent ? self(self, static_cast<std::size_t>(p), depth + 1) * local[b] : local[b];
                done[b] = 1;
            }
            return world[b];
        };
        std::vector<glm::mat4> out(count);
        for (std::size_t b = 0; b < count; ++b)
            out[b] = solve(solve, b, 0) * glm::inverse(rig.rest_world[b]);
        return out;
    }

    std::pair<std::size_t, float> flat_ride_motion(std::span<const float> seconds, float cycle, float into, bool riding, float idle)
    {
        const auto length = [&](std::size_t k) { return k < seconds.size() ? seconds[k] : 0.0f; };
        if (!riding)
            return { kRideStanding, length(kRideStanding) > 0 ? std::fmod(idle, length(kRideStanding)) : 0.0f };
        const float start = length(kRideStarting);
        const float run = length(kRideRunning);
        const float stop = length(kRideStopping);
        if (into < start)
            return { kRideStarting, into };
        const float running = std::max(cycle - start - stop, 0.0f);
        if (into < start + running)
            return { kRideRunning, run > 0 ? std::fmod(into - start, run) : 0.0f };
        return { kRideStopping, std::min(into - start - running, stop) };
    }

    std::size_t next_gate_motion(std::size_t playing, bool open)
    {
        if (open)
            return playing == kGateShut || playing == kGateShutting ? kGateOpening : kGateOpen;
        return playing == kGateOpen || playing == kGateOpening ? kGateShutting : kGateShut;
    }

} // namespace rct3::park
