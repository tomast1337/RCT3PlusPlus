// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the pointer is on in the park: the nearest of some placed objects or people that a line of sight passes
// through, unless the ground is in front of it. An object counts as the box round its finest shape, turned and
// moved as it is drawn, which is close enough for the large, solid things a player clicks on. A person counts
// as a box a little wider than a person, so that someone small on screen is still easy to hit.
#pragma once

#include "park/camera.hpp"
#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>

namespace rct3::park
{

    /// How far along `ray` it first meets the ground, if it does within `reach`.
    [[nodiscard]] std::optional<float> ground_hit(const Ray& ray, const save::TerrainGrid& grid, float reach = kFar);

    /// How far along `ray` it first enters the box from `low` to `high`, if it does.
    [[nodiscard]] std::optional<float> box_hit(const Ray& ray, glm::vec3 low, glm::vec3 high);

    /// One of several things a line of sight passes through, and how far along it.
    struct PickHit
    {
        std::size_t index = 0;
        float distance = 0;
    };

    /// The box a person is hit in, in their own space: standing at the origin, head up along y.
    inline constexpr glm::vec3 kPersonLow { -0.4f, 0.0f, -0.4f };
    inline constexpr glm::vec3 kPersonHigh { 0.4f, 1.9f, 0.4f };

    /// The nearest object among `objects` for which `wanted` holds that `ray` passes through before it reaches
    /// the ground.
    [[nodiscard]] std::optional<PickHit> pick_object(const Ray& ray, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const std::function<bool(const save::PlacedObject&)>& wanted);

    /// The nearest of the people placed in the park by `placements` (each from their own space) that `ray`
    /// passes through before it reaches the ground.
    [[nodiscard]] std::optional<PickHit> pick_person(const Ray& ray, std::span<const glm::mat4> placements, const save::TerrainGrid& grid);

} // namespace rct3::park
