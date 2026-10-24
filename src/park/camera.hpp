// SPDX-License-Identifier: GPL-3.0-or-later
//
// The camera over the park: it looks at a point on the ground from some distance, turned round the vertical
// (yaw) and tilted down (pitch). Moving it slides that point over the ground, keeping the same height above
// the surface; turning and zooming happen round it. Riding along in a ride's car, it looks out from the
// rider's eyes instead, turning and leaning with the car.
#pragma once

#include "math/glm.hpp"
#include "ovl/ride_train.hpp"
#include "save/model/world.hpp"

#include <optional>
#include <span>

namespace rct3::park
{

    struct ParkCamera
    {
        glm::vec3 target { 0.0f }; ///< The point looked at
        float yaw = 0;             ///< Radians; 0 looks along +Z, growing towards +X
        float pitch = 0.6f;        ///< Radians down from level
        float distance = 60;       ///< From the eye to the target, in world units
    };

    /// Riding a car: where the rider's eyes are, looking along its +Z with its +Y up. Given to the functions
    /// below, it takes the camera's place.
    using Seat = std::optional<glm::mat4>;

    inline constexpr float kFieldOfView = 0.9f; ///< Radians, vertical
    /// Riding along, how far the view takes in across the screen, in radians; the height follows the screen's shape.
    inline constexpr float kRideWideView = 1.75f;  ///< From a car or a ride's fixed camera, to take in the track ahead
    inline constexpr float kRideNarrowView = 1.4f; ///< From a flat ride's seat or a person's eyes
    inline constexpr float kNear = 0.5f;
    inline constexpr float kRideNear = 0.1f; ///< Riding along: the car's front is close by
    inline constexpr float kFar = 3000;
    inline constexpr float kMinDistance = 6;
    inline constexpr float kMaxDistance = 400;
    inline constexpr float kMinPitch = 0.1f;
    inline constexpr float kMaxPitch = 1.5f;

    [[nodiscard]] glm::vec3 eye(const ParkCamera& camera, const Seat& seat = std::nullopt);

    /// What riding along keeps from frame to frame; started afresh in each car.
    struct RideView
    {
        std::optional<glm::vec3> last; ///< The car's eyes a frame ago
        std::optional<glm::vec3> velocity;
        glm::vec3 sway { };            ///< Of the head, against the car's pull
        std::optional<glm::mat3> axes; ///< The way the view looked, following the car's turns
    };

    /// The rider's eyes this frame, as a seat: `eye` is the car's camera bone in the park, `ahead` the same
    /// bone as far along the track as the train looks ahead (nothing not to look ahead), and `dice` two
    /// numbers in -1..1 for the shake. The view looks at the track ahead, leaning into its turns, follows the
    /// car's turns a little late, sways against the car's pull, and shakes faster the faster the car goes.
    inline constexpr float kRideLookPerScreen = glm::pi<float>(); ///< A drag the screen's width turns the head this far
    inline constexpr float kRideLookMostPitch = 1.3f;             ///< How far up or down it turns

    /// The rider's eyes `seat` with their head turned `turn` (yaw to the left, then pitch up, in radians).
    [[nodiscard]] glm::mat4 looked_round(const glm::mat4& seat, glm::vec2 turn);
    /// `turn` with its pitch kept within reach and its yaw within a turn either way.
    [[nodiscard]] glm::vec2 look_round_limited(glm::vec2 turn);

    [[nodiscard]] glm::mat4 ride_look(RideView& view, const glm::mat4& eye, std::optional<glm::vec3> ahead, bool sways, const ovl::RideCameraTuning& tuning, float elapsed, glm::vec2 dice);
    /// The camera with its eye at `from` looking at `at`, within the tilt's and distance's limits.
    [[nodiscard]] ParkCamera looking(glm::vec3 from, glm::vec3 at);

    /// A half line from `origin` along `direction`, which has length 1.
    struct Ray
    {
        glm::vec3 origin { 0.0f };
        glm::vec3 direction { 0.0f, 0.0f, 1.0f };
    };

    /// The line of sight through `at` on the view, from -1 to 1 left to right and bottom to top, for a view
    /// `aspect` times as wide as it is tall.
    [[nodiscard]] Ray view_ray(const ParkCamera& camera, float aspect, glm::vec2 at);
    /// World to clip space for a view `aspect` times as wide as it is tall.
    /// Riding, the view takes in `across` radians across the screen.
    [[nodiscard]] glm::mat4 view_projection(const ParkCamera& camera, float aspect, const Seat& seat = std::nullopt, float across = kRideWideView);
    /// The part of view_projection after the camera's turn and place: from the eye's space, looking along +z.
    [[nodiscard]] glm::mat4 projection(float aspect, const Seat& seat = std::nullopt, float across = kRideWideView);

    /// Looking at the middle of the map from the south-west, far enough to see much of it.
    [[nodiscard]] ParkCamera overview(const save::TerrainGrid& grid);

    /// Slid `right` and `forward` world units over the ground, as the camera faces.
    [[nodiscard]] ParkCamera panned(ParkCamera camera, float right, float forward);
    /// How hard the pointer at `at` pushes the view towards each edge of a screen `size` across: 0 more than
    /// `band` in from the edges, rising to 1 at an edge (-1 at the left and the bottom), in X and Y with Y
    /// down the screen given as forward positive.
    [[nodiscard]] glm::vec2 edge_push(glm::vec2 at, glm::vec2 size, float band);
    /// Turned by `yaw` and tilted by `pitch` radians, within the tilt's limits; unless `free`, which only stops
    /// short of looking straight up or down.
    [[nodiscard]] ParkCamera turned(ParkCamera camera, float yaw, float pitch, bool free = false);
    /// Brought `factor` times as far, within the limits; unless `free`, which allows from the near plane to the
    /// far one.
    [[nodiscard]] ParkCamera zoomed(ParkCamera camera, float factor, bool free = false);
    /// How much of a zoom still to come to take this frame, `elapsed` seconds long, so it eases out: each
    /// `seconds` takes all but a twentieth of what is left.
    [[nodiscard]] float zoom_step(float pending, float elapsed, float seconds);
    /// Looking at the middle of the box round `points` from `yaw`, tilted `pitch` down, near enough that the
    /// box fills a square view without leaving it.
    [[nodiscard]] ParkCamera framed(std::span<const glm::vec3> points, float yaw, float pitch, float closest = kMinDistance);
    /// Kept over the map, its target on the surface.
    [[nodiscard]] ParkCamera on_ground(ParkCamera camera, const save::TerrainGrid& grid);

} // namespace rct3::park
