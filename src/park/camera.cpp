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

    glm::mat4 ride_look(RideView& view, const glm::mat4& eye, std::optional<glm::vec3> ahead, bool sways, const ovl::RideCameraTuning& tuning, float elapsed, glm::vec2 dice)
    {
        constexpr float kGravity = 9.81f;
        const glm::vec3 at(eye[3]);
        glm::vec3 x = glm::normalize(glm::vec3(eye[0]));
        glm::vec3 y = glm::normalize(glm::vec3(eye[1]));
        glm::vec3 z = glm::normalize(glm::vec3(eye[2]));
        // The way a time-to-settle `seconds` long moves on toward where it is going in this frame.
        const auto settling = [&](float seconds) { return seconds > 0 ? std::min(1.0f, elapsed / seconds) : 1.0f; };

        // The car's pull from how its eyes moved over the last frames.
        glm::vec3 pull { };
        if (view.last && elapsed > 0)
        {
            const glm::vec3 velocity = (at - *view.last) / elapsed;
            if (view.velocity)
                pull = (velocity - *view.velocity) / elapsed;
            view.velocity = velocity;
        }
        view.last = at;
        glm::vec3 sway = sways ? -pull * (tuning.sway_per_g / kGravity) : glm::vec3 { };
        if (const float length = glm::length(sway); length > tuning.sway_most)
            sway *= tuning.sway_most / length;
        view.sway += (sway - view.sway) * settling(tuning.sway_settle);

        // Looking at the track ahead, unless it lies behind; looking into a turn leans the view into it.
        if (ahead && glm::length(*ahead - at) > 0)
            if (const glm::vec3 look = glm::normalize(*ahead - at); glm::dot(look, z) > 0)
            {
                const glm::vec3 up = y + glm::dot(look, x) * x * tuning.look_tilt;
                z = look;
                x = glm::normalize(glm::cross(up, z));
                y = glm::cross(z, x);
            }
        if (view.axes)
        {
            const float a = settling(tuning.turn_settle);
            z = glm::normalize(glm::mix((*view.axes)[2], z, a));
            x = glm::normalize(glm::mix((*view.axes)[0], x, a));
            y = glm::normalize(glm::cross(z, x));
            x = glm::cross(y, z);
        }
        view.axes = glm::mat3(x, y, z);

        if (const float speed = view.velocity ? glm::length(*view.velocity) : 0.0f; speed > tuning.shake_from)
        {
            const float amount = (speed - tuning.shake_from) * (speed - tuning.shake_from) * tuning.shake;
            const float across = std::clamp(dice.x * amount, -tuning.shake_across, tuning.shake_across);
            const float up = std::clamp(dice.y * amount, -tuning.shake_up, tuning.shake_up);
            z = glm::normalize(z + across * x + up * y);
            x = glm::normalize(glm::cross(y, z));
            y = glm::cross(z, x);
        }
        return { glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(at + view.sway, 1) };
    }

    glm::mat4 looked_round(const glm::mat4& seat, glm::vec2 turn)
    {
        // About the seat's own up, then its own sideways, so the head turns as the rider's would however the
        // car is banked.
        return seat * glm::rotate(glm::mat4(1.0f), turn.x, glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1.0f), -turn.y, glm::vec3(1, 0, 0));
    }

    glm::vec2 look_round_limited(glm::vec2 turn)
    {
        return { std::remainder(turn.x, glm::two_pi<float>()), std::clamp(turn.y, -kRideLookMostPitch, kRideLookMostPitch) };
    }

    glm::mat4 view_projection(const ParkCamera& camera, float aspect, const Seat& seat, float across)
    {
        if (seat)
        {
            const glm::vec3 from((*seat)[3]);
            return projection(aspect, seat, across) * glm::lookAt(from, from + glm::normalize(glm::vec3((*seat)[2])), glm::normalize(glm::vec3((*seat)[1])));
        }
        return projection(aspect) * glm::lookAt(eye(camera), camera.target, glm::vec3(0, 1, 0));
    }

    glm::mat4 projection(float aspect, const Seat& seat, float across)
    {
        aspect = aspect > 0 ? aspect : 1.0f;
        return glm::perspective(seat ? 2 * std::atan(std::tan(across / 2) / aspect) : kFieldOfView, aspect, seat ? kRideNear : kNear, kFar);
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

    ParkCamera on_ground(ParkCamera camera, const save::TerrainGrid& grid, bool lifted)
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
            camera.target.y = lifted ? std::max(camera.target.y, *h) : *h;
        return camera;
    }

    float isometric_yaw(int step)
    {
        return glm::quarter_pi<float>() + static_cast<float>(((step % 4) + 4) % 4) * glm::half_pi<float>();
    }

    int isometric_step(float yaw)
    {
        return static_cast<int>(std::lround((yaw - glm::quarter_pi<float>()) / glm::half_pi<float>()));
    }

    float yaw_towards(float from, float to, float most)
    {
        const float full = glm::two_pi<float>();
        const float left = std::remainder(to - from, full); // In -pi..pi: the shorter way
        return std::abs(left) <= most ? from + left : from + std::copysign(most, left);
    }

    ParkCamera turned_at_eye(ParkCamera camera, float yaw, float pitch)
    {
        const glm::vec3 at = eye(camera);
        camera = turned(camera, yaw, pitch, true);
        camera.target += at - eye(camera);
        return camera;
    }

    ParkCamera orbited(ParkCamera camera, glm::vec3 pivot, float yaw, float pitch)
    {
        const glm::vec3 from = eye(camera) - pivot;
        const float reach = glm::length(from);
        const ParkCamera turned_camera = turned(camera, yaw, pitch);
        if (reach <= 0)
            return turned_camera;
        // The eye goes round the pivot as far as the view turns, so the pivot keeps its place on the view.
        const float round = std::atan2(from.x, from.z) + yaw;
        const float up = std::asin(std::clamp(from.y / reach, -1.0f, 1.0f)) + (turned_camera.pitch - camera.pitch);
        const glm::vec3 moved = pivot + reach * glm::vec3 { std::sin(round) * std::cos(up), std::sin(up), std::cos(round) * std::cos(up) };
        camera = turned_camera;
        camera.target += moved - eye(camera);
        return camera;
    }

    ParkCamera flown(ParkCamera camera, glm::vec3 by)
    {
        const glm::vec3 forward = glm::normalize(camera.target - eye(camera));
        const glm::vec3 right = glm::normalize(glm::vec3 { forward.z, 0, -forward.x });
        camera.target += right * by.x + glm::vec3 { 0, by.y, 0 } + forward * by.z;
        return camera;
    }

} // namespace rct3::park
