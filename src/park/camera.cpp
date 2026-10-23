// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/camera.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    namespace
    {
        constexpr float kOverviewYaw = 0.785f; ///< Facing north-east
        constexpr float kOverviewShare = 0.5f; ///< Of the map's longer side, as the distance
    } // namespace

    glm::vec3 eye(const ParkCamera& camera, const Seat& seat)
    {
        if (seat)
            return glm::vec3((*seat)[3]);
        const glm::vec3 forward { std::sin(camera.yaw) * std::cos(camera.pitch), -std::sin(camera.pitch), std::cos(camera.yaw) * std::cos(camera.pitch) };
        return camera.target - forward * camera.distance;
    }

    ParkCamera looking(glm::vec3 from, glm::vec3 at)
    {
        const glm::vec3 d = at - from;
        const float length = glm::length(d);
        ParkCamera c;
        c.target = at;
        if (length <= 0)
            return c;
        c.yaw = std::atan2(d.x, d.z);
        c.pitch = std::clamp(std::asin(-d.y / length), kMinPitch, kMaxPitch);
        c.distance = std::clamp(length, kMinDistance, kMaxDistance);
        return c;
    }

    Ray view_ray(const ParkCamera& camera, float aspect, glm::vec2 at)
    {
        const glm::mat4 back = glm::inverse(view_projection(camera, aspect));
        const glm::vec4 near = back * glm::vec4(at, 0.0f, 1.0f);
        const glm::vec4 far = back * glm::vec4(at, 1.0f, 1.0f);
        const glm::vec3 from = glm::vec3(near) / near.w;
        return { from, glm::normalize(glm::vec3(far) / far.w - from) };
    }

    glm::mat4 view_projection(const ParkCamera& camera, float aspect, const Seat& seat)
    {
        if (seat)
        {
            const glm::vec3 from((*seat)[3]);
            return projection(aspect, seat) * glm::lookAt(from, from + glm::normalize(glm::vec3((*seat)[2])), glm::normalize(glm::vec3((*seat)[1])));
        }
        return projection(aspect) * glm::lookAt(eye(camera), camera.target, glm::vec3(0, 1, 0));
    }

    glm::mat4 projection(float aspect, const Seat& seat)
    {
        return glm::perspective(seat ? kRideFieldOfView : kFieldOfView, aspect > 0 ? aspect : 1.0f, seat ? kRideNear : kNear, kFar);
    }

    ParkCamera overview(const save::TerrainGrid& grid)
    {
        ParkCamera c;
        c.yaw = kOverviewYaw;
        if (grid.empty())
            return c;
        const float width = static_cast<float>(grid.width) * grid.tile_size_x;
        const float depth = static_cast<float>(grid.height) * grid.tile_size_z;
        c.target = { grid.origin_x + width / 2, 0, grid.origin_z + depth / 2 };
        c.distance = std::clamp(std::max(width, depth) * kOverviewShare, kMinDistance, kMaxDistance);
        return on_ground(c, grid);
    }

    ParkCamera framed(std::span<const glm::vec3> points, float yaw, float pitch, float closest)
    {
        ParkCamera c;
        c.yaw = yaw;
        c.pitch = pitch;
        if (points.empty())
            return c;
        glm::vec3 low = points.front();
        glm::vec3 high = points.front();
        for (const glm::vec3& p : points)
        {
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        c.target = (low + high) / 2.0f;
        // Far enough that a ball round the box fits inside the view's narrower angle.
        const float radius = glm::length(high - low) / 2;
        c.distance = std::clamp(radius / std::sin(kFieldOfView / 2), closest, kMaxDistance);
        return c;
    }

    ParkCamera panned(ParkCamera camera, float right, float forward)
    {
        const glm::vec3 ahead { std::sin(camera.yaw), 0, std::cos(camera.yaw) };
        const glm::vec3 side { ahead.z, 0, -ahead.x };
        camera.target += ahead * forward + side * right;
        return camera;
    }

    glm::vec2 edge_push(glm::vec2 at, glm::vec2 size, float band)
    {
        if (band <= 0)
            return { 0, 0 };
        const auto push = [band](float from_low, float from_high) {
            const float low = std::clamp(1 - from_low / band, 0.0f, 1.0f);
            const float high = std::clamp(1 - from_high / band, 0.0f, 1.0f);
            return high - low;
        };
        // Down the screen is towards the viewer: the top edge pushes forward.
        return { push(at.x, size.x - 1 - at.x), push(size.y - 1 - at.y, at.y) };
    }

    ParkCamera turned(ParkCamera camera, float yaw, float pitch, bool free)
    {
        camera.yaw += yaw;
        camera.pitch = free ? std::clamp(camera.pitch + pitch, -kMaxPitch, kMaxPitch) : std::clamp(camera.pitch + pitch, kMinPitch, kMaxPitch);
        return camera;
    }

    ParkCamera zoomed(ParkCamera camera, float factor, bool free)
    {
        camera.distance = free ? std::clamp(camera.distance * factor, kNear, kFar / 2) : std::clamp(camera.distance * factor, kMinDistance, kMaxDistance);
        return camera;
    }

    float zoom_step(float pending, float elapsed, float seconds)
    {
        if (seconds <= 0 || std::abs(pending) < 1e-4f)
            return pending; // The tail, taken at once so it does not linger
        return pending * (1 - std::exp(std::log(0.05f) * elapsed / seconds));
    }

    ParkCamera on_ground(ParkCamera camera, const save::TerrainGrid& grid)
    {
        if (grid.empty())
            return camera;
        const float x0 = grid.origin_x;
        const float z0 = grid.origin_z;
        // Just inside the far edges, where the surface is still defined.
        const float x1 = grid.world_x(grid.width) - kNear;
        const float z1 = grid.world_z(grid.height) - kNear;
        camera.target.x = std::clamp(camera.target.x, x0, x1);
        camera.target.z = std::clamp(camera.target.z, z0, z1);
        if (const auto h = grid.height_at(camera.target.x, camera.target.z))
            camera.target.y = *h;
        return camera;
    }

} // namespace rct3::park
