// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's water as the renderer draws it: the surfaces of the water bodies, grouped by the height they stand
// at, and the planes the scene is mirrored in for their reflections.
//
// A reflection is the scene drawn again from the eye mirrored in the surface, into a picture the water then
// reads where it is seen. Each height of water needs its own, which costs a whole draw of the scene, so only
// the few heights that cover most of the view get one; water at another height borrows the nearest one close
// enough, and the rest reflects only the sky. What lies under the surface is cut away from the mirrored draw by
// moving the camera's near plane onto the surface (an oblique near plane), which needs no help from the
// shaders.
#pragma once

#include "park/camera.hpp"
#include "park/terrain_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// The water at one height: a run of triangles of WaterSurface::vertices.
    struct WaterBatch
    {
        float level = 0;
        std::uint32_t first = 0; ///< Vertices, three per triangle
        std::uint32_t count = 0;
        glm::vec3 low { 0.0f }; ///< Corners of the box round it
        glm::vec3 high { 0.0f };
    };

    /// Water drawn as ground is, with the depth of water at each corner in the first texture coordinate.
    struct WaterSurface
    {
        std::vector<GroundVertex> vertices;
        std::vector<WaterBatch> batches; ///< Lowest first, one per height
    };

    [[nodiscard]] WaterSurface water_surface(std::span<const WaterVertex> water);

    /// How far apart two heights of water may stand for one to show the other's reflection: the mirror image is
    /// then off by twice the difference, which reads as a reflection still.
    inline constexpr float kReflectionReach = 0.6f;
    /// The mirrored draw keeps what is a little under the surface too, so the shore meets its reflection.
    inline constexpr float kReflectionClipBias = 0.05f;

    /// The heights to draw reflections for, at most `count`: those of the water that covers most of the view
    /// `view_projection` gives, from `eye`. Water at or above the eye reflects nothing seen from it.
    [[nodiscard]] std::vector<float> reflection_levels(const WaterSurface& surface, const glm::mat4& view_projection, glm::vec3 eye, std::size_t count);

    /// Of `levels`, the one whose reflection water at `level` shows: the nearest within kReflectionReach.
    [[nodiscard]] std::optional<std::size_t> reflection_for(float level, std::span<const float> levels);

    /// Mirrors world space in the level plane at height `level`.
    [[nodiscard]] glm::mat4 mirror(float level);

    /// `transform` with its near plane moved onto `plane` (a, b, c, d: kept where a x + b y + c z + d > 0, in the
    /// space `transform` maps from), so that what lies on the other side is cut away. The far plane is tilted so
    /// it still bounds the view. The eye must be on the side cut away.
    [[nodiscard]] glm::mat4 oblique_clip(const glm::mat4& transform, glm::vec4 plane);

    /// World to clip space for the scene mirrored in the water at `level`, as seen through `view_projection`,
    /// with what lies under the water cut away.
    [[nodiscard]] glm::mat4 reflection_transform(const glm::mat4& view_projection, float level);

    /// How far along `ray` it first meets the water's surface, if it does.
    [[nodiscard]] std::optional<float> water_hit(const Ray& ray, const WaterSurface& surface);

    /// The water a point is in: the height of the surface over it and the water's colour.
    struct WaterAbove
    {
        float level = 0;
        glm::vec4 colour { 1.0f };
    };

    /// The water `point` is under, if it is under any: the eye there sees the park from inside the water.
    [[nodiscard]] std::optional<WaterAbove> water_above(const WaterSurface& surface, glm::vec3 point);

} // namespace rct3::park
