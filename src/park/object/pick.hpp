// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the pointer is on in the park: the nearest of some placed objects or people that a line of sight passes
// through, unless the ground is in front of it. An object counts as the box round its finest shape, turned and
// moved as it is drawn, which is close enough for the large, solid things a player clicks on. A person counts
// as a box a little wider than a person, so that someone small on screen is still easy to hit.
#pragma once

#include "core/types.hpp"
#include "park/camera.hpp"
#include "park/object/mesh.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park::object
{

    /// How far along `ray` it first meets the ground, if it does within `reach`.
    [[nodiscard]] std::optional<f32> ground_hit(const Ray& ray, const save::model::TerrainGrid& grid, f32 reach = kFar);

    /// How far along `ray` it first enters the box from `low` to `high`, if it does.
    [[nodiscard]] std::optional<f32> box_hit(const Ray& ray, glm::vec3 low, glm::vec3 high);

    /// One of several things a line of sight passes through, and how far along it.
    struct PickHit
    {
        usize index = 0;
        f32 distance = 0;
    };

    /// The box a person is hit in, in their own space: standing at the origin, head up along y.
    inline constexpr glm::vec3 kPersonLow { -0.4f, 0.0f, -0.4f };
    inline constexpr glm::vec3 kPersonHigh { 0.4f, 1.9f, 0.4f };
    /// Farther than this from a person's feet, a line misses their box whichever way they face.
    inline constexpr f32 kPersonReach = 2.0f;

    /// The box an object is hit in, worked out once: in the object's own space, the way back there from the
    /// park, and a box square to the park's axes round it for a quick first look.
    struct PickBox
    {
        usize object_index = 0;
        glm::mat4 back { 1.0f };
        glm::vec3 low { 0.0f }, high { 0.0f };
        glm::vec3 park_low { 0.0f }, park_high { 0.0f };
    };

    /// The boxes of those of `objects` that have a model with a shape, by rising object index.
    [[nodiscard]] std::vector<PickBox> pick_boxes(std::span<const save::model::PlacedObject> objects, const ObjectModels& models, const save::model::TerrainGrid& grid, const Fitted& fitted);

    /// Brings `boxes`, by rising object index, up to date with `places`: the objects in `changed` (by index,
    /// rising) get theirs worked out again, if their model has a shape, and the others keep the ones they had.
    void update_pick_boxes(std::vector<PickBox>& boxes, const ObjectPlaces& places, std::span<const usize> changed);

    /// The nearest of `boxes` for whose object `wanted` holds (by index) that `ray` passes through before it
    /// reaches the ground.
    [[nodiscard]] std::optional<PickHit> pick_object(const Ray& ray, std::span<const PickBox> boxes, const save::model::TerrainGrid& grid, const std::function<bool(usize)>& wanted);

    /// The nearest object among `objects` for which `wanted` holds that `ray` passes through before it reaches
    /// the ground.
    [[nodiscard]] std::optional<PickHit> pick_object(const Ray& ray, std::span<const save::model::PlacedObject> objects, const ObjectModels& models, const save::model::TerrainGrid& grid, const Fitted& fitted, const std::function<bool(const save::model::PlacedObject&)>& wanted);

    /// The corners of the box `object` is hit in, in the park; none when it has no model.
    [[nodiscard]] std::vector<glm::vec3> object_corners(const save::model::PlacedObject& object, const ObjectModels& models, const save::model::TerrainGrid& grid, const Fitted& fitted);

    /// The nearest of the people placed in the park by `placements` (each from their own space) that `ray`
    /// passes through before it reaches the ground.
    [[nodiscard]] std::optional<PickHit> pick_person(const Ray& ray, std::span<const glm::mat4> placements, const save::model::TerrainGrid& grid);

} // namespace rct3::park::object