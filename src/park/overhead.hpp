// SPDX-License-Identifier: GPL-3.0-or-later
//
// What is overhead at a point in the park: a roof that keeps the rain off, or trees that only shade it. A
// line goes straight up from a little above the point; it is under a roof when it meets a triangle of the
// finest shape of a building, a stall, a ride or a piece of scenery, and shaded when it passes through the
// box round a tree. Track, paths, supports, effects and things lying about are no roof.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <span>
#include <vector>

namespace rct3::park
{

    struct Overhead
    {
        bool roof = false;
        bool shade = false; ///< Under a roof or trees
    };

    /// Whether an item of `category` keeps the rain off what is under it.
    [[nodiscard]] bool is_roof(std::uint32_t category) noexcept;

    inline constexpr float kHeadroom = 2.2f;   ///< Above a point, where the line starts: over people's heads
    inline constexpr float kRoofReach = 15.0f; ///< Above a point, where it ends: higher is sky

    /// What is over each of `points`, on a line up from kHeadroom above it to kRoofReach above it.
    /// shortcut: trees drawn only as flat pictures have no box and give no shade; upgrade by sizing them from
    /// their pictures if parks loaded that way miss their shade.
    [[nodiscard]] std::vector<Overhead> overheads(std::span<const glm::vec3> points, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted);

} // namespace rct3::park
