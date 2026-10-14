// SPDX-License-Identifier: GPL-3.0-or-later
//
// The camera over the park: it looks at a point on the ground from some distance, turned round the vertical
// (yaw) and tilted down (pitch). Moving it slides that point over the ground, keeping the same height above
// the surface; turning and zooming happen round it.
#pragma once

#include "math/glm.hpp"
#include "save/model/world.hpp"

namespace rct3::park
{

    struct ParkCamera
    {
        glm::vec3 target { 0.0f }; ///< The point looked at
        float yaw = 0;             ///< Radians; 0 looks along +Z, growing towards +X
        float pitch = 0.6f;        ///< Radians down from level
        float distance = 60;       ///< From the eye to the target, in world units
    };

    inline constexpr float kFieldOfView = 0.9f; ///< Radians, vertical
    inline constexpr float kNear = 0.5f;
    inline constexpr float kFar = 3000;
    inline constexpr float kMinDistance = 6;
    inline constexpr float kMaxDistance = 400;
    inline constexpr float kMinPitch = 0.1f;
    inline constexpr float kMaxPitch = 1.5f;

    [[nodiscard]] glm::vec3 eye(const ParkCamera& camera);
    /// World to clip space for a view `aspect` times as wide as it is tall.
    [[nodiscard]] glm::mat4 view_projection(const ParkCamera& camera, float aspect);

    /// Looking at the middle of the map from the south-west, far enough to see much of it.
    [[nodiscard]] ParkCamera overview(const save::TerrainGrid& grid);

    /// Slid `right` and `forward` world units over the ground, as the camera faces.
    [[nodiscard]] ParkCamera panned(ParkCamera camera, float right, float forward);
    /// Turned by `yaw` and tilted by `pitch` radians, within the tilt's limits.
    [[nodiscard]] ParkCamera turned(ParkCamera camera, float yaw, float pitch);
    /// Brought `factor` times as far, within the limits.
    [[nodiscard]] ParkCamera zoomed(ParkCamera camera, float factor);
    /// Kept over the map, its target on the surface.
    [[nodiscard]] ParkCamera on_ground(ParkCamera camera, const save::TerrainGrid& grid);

} // namespace rct3::park
