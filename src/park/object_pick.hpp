// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the pointer is on in the park: the nearest of some placed objects that a line of sight passes through,
// unless the ground is in front of it. An object counts as the box round its finest shape, turned and moved
// as it is drawn, which is close enough for the large, solid things a player clicks on.
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

    /// The index of the nearest object among `objects` for which `wanted` holds that `ray` passes through
    /// before it reaches the ground.
    [[nodiscard]] std::optional<std::size_t> pick_object(const Ray& ray, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const std::function<bool(const save::PlacedObject&)>& wanted);

} // namespace rct3::park
