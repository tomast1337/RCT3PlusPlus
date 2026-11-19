// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/camera.hpp"

#include "core/types.hpp"
#include "units/units.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rct3::park
{

    namespace
    {
        constexpr f32 kOverviewYaw = 0.785f; ///< Facing north-east
        constexpr f32 kOverviewShare = 0.5f; ///< Of the map's longer side, as the distance
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
        const f32 length = glm::length(d);
        ParkCamera c;
        c.target = at;
        if (length <= 0)
            return c;
        c.yaw = std::atan2(d.x, d.z);
        c.pitch = std::clamp(std::asin(-d.y / length), kMinPitch, kMaxPitch);
        c.distance = std::clamp(length, kMinDistance, kMaxDistance);
        return c;
    }

    Ray view_ray(const ParkCamera& camera, f32 aspect, glm::vec2 at)
    {
        const glm::mat4 back = glm::inverse(view_projection(camera, aspect));
        const glm::vec4 near = back * glm::vec4(at, 0.0f, 1.0f);
        const glm::vec4 far = back * glm::vec4(at, 1.0f, 1.0f);
        const glm::vec3 from = glm::vec3(near) / near.w;
        return { from, glm::normalize(glm::vec3(far) / far.w - from) };
    }

    std::optional<f32> upright_height(const Ray& ray, glm::vec3 at, f32 steepest)
    {
        const glm::vec3 across = glm::normalize(glm::vec3 { ray.direction.x, 0.0f, ray.direction.z });
        const f32 along = glm::dot(ray.direction, across);
        if (!(along > 1e-4f))
            return std::nullopt;
        return ray.origin.y + ray.direction.y * glm::dot(at - ray.origin, across) / std::max(along, steepest);
    }

    glm::mat4 ride_look(RideView& view, const glm::mat4& eye, std::optional<glm::vec3> ahead, bool sways, const ovl::RideCameraTuning& tuning, f32 elapsed, glm::vec2 dice)
    {
        using units::kGravity;
        const glm::vec3 at(eye[3]);
        glm::vec3 x = glm::normalize(glm::vec3(eye[0]));
        glm::vec3 y = glm::normalize(glm::vec3(eye[1]));
        glm::vec3 z = glm::normalize(glm::vec3(eye[2]));
        // The way a time-to-settle `seconds` long moves on toward where it is going in this frame.
        const auto settling = [&](f32 seconds) { return seconds > 0 ? std::min(1.0f, elapsed / seconds) : 1.0f; };

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
        glm::vec3 sway = sways ? -pull * (tuning.sway_per_g / kGravity.value) : glm::vec3 { };
        if (const f32 length = glm::length(sway); length > tuning.sway_most)
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
            const f32 a = settling(tuning.turn_settle);
            z = glm::normalize(glm::mix((*view.axes)[2], z, a));
            x = glm::normalize(glm::mix((*view.axes)[0], x, a));
            y = glm::normalize(glm::cross(z, x));
            x = glm::cross(y, z);
        }
        view.axes = glm::mat3(x, y, z);

        if (const f32 speed = view.velocity ? glm::length(*view.velocity) : 0.0f; speed > tuning.shake_from)
        {
            const f32 amount = (speed - tuning.shake_from) * (speed - tuning.shake_from) * tuning.shake;
            const f32 across = std::clamp(dice.x * amount, -tuning.shake_across, tuning.shake_across);
            const f32 up = std::clamp(dice.y * amount, -tuning.shake_up, tuning.shake_up);
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
        return { std::remainder(turn.x, glm::two_pi<f32>()), std::clamp(turn.y, -kRideLookMostPitch, kRideLookMostPitch) };
    }

    glm::mat4 view_projection(const ParkCamera& camera, f32 aspect, const Seat& seat, f32 across)
    {
        if (seat)
        {
            const glm::vec3 from((*seat)[3]);
            return projection(aspect, seat, across) * glm::lookAt(from, from + glm::normalize(glm::vec3((*seat)[2])), glm::normalize(glm::vec3((*seat)[1])));
        }
        return projection(aspect) * glm::lookAt(eye(camera), camera.target, glm::vec3(0, 1, 0));
    }

    bool box_in_view(const glm::mat4& transform, const glm::vec3& low, const glm::vec3& high)
    {
        std::array<i32, 5> beyond { };
        for (i32 k = 0; k < 8; ++k)
        {
            const glm::vec4 p = transform * glm::vec4((k & 1) != 0 ? high.x : low.x, (k & 2) != 0 ? high.y : low.y, (k & 4) != 0 ? high.z : low.z, 1.0f);
            beyond[0] += p.x < -p.w ? 1 : 0;
            beyond[1] += p.x > p.w ? 1 : 0;
            beyond[2] += p.y < -p.w ? 1 : 0;
            beyond[3] += p.y > p.w ? 1 : 0;
            beyond[4] += p.w <= 0 ? 1 : 0;
        }
        return std::ranges::none_of(beyond, [](i32 n) { return n == 8; });
    }

    glm::mat4 projection(f32 aspect, const Seat& seat, f32 across)
    {
        aspect = aspect > 0 ? aspect : 1.0f;
        return glm::perspective(seat ? 2 * std::atan(std::tan(across / 2) / aspect) : kFieldOfView, aspect, seat ? kRideNear : kNear, kFar);
    }

    ParkCamera overview(const save::model::TerrainGrid& grid)
    {
        ParkCamera c;
        c.yaw = kOverviewYaw;
        if (grid.empty())
            return c;
        const f32 width = static_cast<f32>(grid.width) * grid.tile_size_x;
        const f32 depth = static_cast<f32>(grid.height) * grid.tile_size_z;
        c.target = { grid.origin_x + width / 2, 0, grid.origin_z + depth / 2 };
        c.distance = std::clamp(std::max(width, depth) * kOverviewShare, kMinDistance, kMaxDistance);
        return on_ground(c, grid);
    }

    ParkCamera framed(std::span<const glm::vec3> points, f32 yaw, f32 pitch, f32 closest)
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
        const f32 radius = glm::length(high - low) / 2;
        c.distance = std::clamp(radius / std::sin(kFieldOfView / 2), closest, kMaxDistance);
        return c;
    }

    ParkCamera panned(ParkCamera camera, f32 right, f32 forward)
    {
        const glm::vec3 ahead { std::sin(camera.yaw), 0, std::cos(camera.yaw) };
        const glm::vec3 side { ahead.z, 0, -ahead.x };
        camera.target += ahead * forward + side * right;
        return camera;
    }

    glm::vec2 edge_push(glm::vec2 at, glm::vec2 size, f32 band)
    {
        if (band <= 0)
            return { 0, 0 };
        const auto push = [band](f32 from_low, f32 from_high) {
            const f32 low = std::clamp(1 - from_low / band, 0.0f, 1.0f);
            const f32 high = std::clamp(1 - from_high / band, 0.0f, 1.0f);
            return high - low;
        };
        // Down the screen is towards the viewer: the top edge pushes forward.
        return { push(at.x, size.x - 1 - at.x), push(size.y - 1 - at.y, at.y) };
    }

    ParkCamera turned(ParkCamera camera, f32 yaw, f32 pitch, bool free)
    {
        camera.yaw += yaw;
        camera.pitch = free ? std::clamp(camera.pitch + pitch, -kMaxPitch, kMaxPitch) : std::clamp(camera.pitch + pitch, kMinPitch, kMaxPitch);
        return camera;
    }

    ParkCamera zoomed(ParkCamera camera, f32 factor, bool free)
    {
        camera.distance = free ? std::clamp(camera.distance * factor, kNear, kFar / 2) : std::clamp(camera.distance * factor, kMinDistance, kMaxDistance);
        return camera;
    }

    f32 zoom_step(f32 pending, f32 elapsed, f32 seconds)
    {
        constexpr f32 kLeftAfter = 0.05f; ///< Of a zoom, the share still to go once its `seconds` are up
        if (seconds <= 0 || std::abs(pending) < 1e-4f)
            return pending; // The tail, taken at once so it does not linger
        return pending * (1 - std::exp(std::log(kLeftAfter) * elapsed / seconds));
    }

    ParkCamera on_ground(ParkCamera camera, const save::model::TerrainGrid& grid, bool lifted)
    {
        if (grid.empty())
            return camera;
        const f32 x0 = grid.origin_x;
        const f32 z0 = grid.origin_z;
        // Just inside the far edges, where the surface is still defined.
        const f32 x1 = grid.world_x(grid.width) - kNear;
        const f32 z1 = grid.world_z(grid.height) - kNear;
        camera.target.x = std::clamp(camera.target.x, x0, x1);
        camera.target.z = std::clamp(camera.target.z, z0, z1);
        if (const auto h = grid.height_at(camera.target.x, camera.target.z))
            camera.target.y = lifted ? std::max(camera.target.y, *h) : *h;
        // Tilting down brings the eye over ground nearer the target, which may be higher still, so it settles
        // in a few steps; the tilt only grows, so it cannot swing back and forth.
        constexpr i32 kSettleSteps = 4;
        for (i32 step = 0; step < kSettleSteps; ++step)
        {
            const glm::vec3 at = eye(camera);
            // The ground a near plane's width round the eye, so a steep bank beside it does not cut in either.
            std::optional<f32> ground;
            for (const glm::vec2 by : { glm::vec2 { 0, 0 }, glm::vec2 { kNear, 0 }, glm::vec2 { -kNear, 0 }, glm::vec2 { 0, kNear }, glm::vec2 { 0, -kNear } })
                if (const auto h = grid.height_at(std::clamp(at.x + by.x, x0, x1), std::clamp(at.z + by.y, z0, z1)))
                    ground = std::max(ground.value_or(*h), *h);
            if (!ground || at.y >= *ground + kEyeAboveGround)
                break;
            const f32 rise = (*ground + kEyeAboveGround - camera.target.y) / camera.distance;
            camera.pitch = rise < std::sin(kMaxPitch) ? std::max(camera.pitch, std::asin(rise)) : kMaxPitch;
        }
        return camera;
    }

    f32 isometric_yaw(i32 step)
    {
        return glm::quarter_pi<f32>() + static_cast<f32>(((step % 4) + 4) % 4) * glm::half_pi<f32>();
    }

    i32 isometric_step(f32 yaw)
    {
        return static_cast<i32>(std::lround((yaw - glm::quarter_pi<f32>()) / glm::half_pi<f32>()));
    }

    f32 yaw_towards(f32 from, f32 to, f32 most)
    {
        const f32 full = glm::two_pi<f32>();
        const f32 left = std::remainder(to - from, full); // In -pi..pi: the shorter way
        return std::abs(left) <= most ? from + left : from + std::copysign(most, left);
    }

    ParkCamera turned_at_eye(ParkCamera camera, f32 yaw, f32 pitch)
    {
        const glm::vec3 at = eye(camera);
        camera = turned(camera, yaw, pitch, true);
        camera.target += at - eye(camera);
        return camera;
    }

    ParkCamera orbited(ParkCamera camera, glm::vec3 pivot, f32 yaw, f32 pitch)
    {
        const glm::vec3 from = eye(camera) - pivot;
        const f32 reach = glm::length(from);
        const ParkCamera turned_camera = turned(camera, yaw, pitch);
        if (reach <= 0)
            return turned_camera;
        // The eye goes round the pivot as far as the view turns, so the pivot keeps its place on the view.
        const f32 round = std::atan2(from.x, from.z) + yaw;
        const f32 up = std::asin(std::clamp(from.y / reach, -1.0f, 1.0f)) + (turned_camera.pitch - camera.pitch);
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
