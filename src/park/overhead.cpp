// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/overhead.hpp"

#include "core/types.hpp"
#include "park/item_category.hpp"
#include "park/object/pick.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace rct3::park
{

    namespace
    {
        constexpr f32 kCell = 4; ///< Park units across a cell objects are sorted into for a quick first look

        u64 cell_key(i32 x, i32 z)
        {
            return u64 { static_cast<u32>(x) } << 32 | static_cast<u32>(z);
        }

        i32 cell_of(f32 v)
        {
            return static_cast<i32>(std::floor(v / kCell));
        }

        /// How far along `ray` it meets the triangle a, b, c, if it does at all ahead.
        std::optional<f32> triangle_hit(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c)
        {
            const glm::vec3 ab = b - a;
            const glm::vec3 ac = c - a;
            const glm::vec3 p = glm::cross(ray.direction, ac);
            const f32 det = glm::dot(ab, p);
            if (std::abs(det) < 1e-8f)
                return std::nullopt;
            const glm::vec3 t = ray.origin - a;
            const f32 u = glm::dot(t, p) / det;
            if (u < 0 || u > 1)
                return std::nullopt;
            const glm::vec3 q = glm::cross(t, ab);
            const f32 v = glm::dot(ray.direction, q) / det;
            if (v < 0 || u + v > 1)
                return std::nullopt;
            const f32 along = glm::dot(ac, q) / det;
            return along >= 0 ? std::optional { along } : std::nullopt;
        }

        bool meets_shape(const Ray& ray, const object::ObjectModel& model, f32 reach)
        {
            for (const object::ModelPart& part : model.levels.front().parts)
                for (usize i = 0; i + 2 < part.indices.size(); i += 3)
                {
                    const auto at = [&](usize k) { return part.vertices[part.indices[i + k]].position; };
                    if (part.indices[i + 2] >= part.vertices.size() || part.indices[i] >= part.vertices.size() || part.indices[i + 1] >= part.vertices.size())
                        continue;
                    if (const auto hit = triangle_hit(ray, at(0), at(1), at(2)); hit && *hit <= reach)
                        return true;
                }
            return false;
        }
    } // namespace

    bool is_roof(u32 category) noexcept
    {
        using enum ItemCategory;
        // Not the park's gate: guests out of the rain would crowd it, blocking those coming in and going home.
        for (const ItemCategory c : { Wall, SmallScenery, MediumScenery, Animatronic, Ride, RideEntrance, RideExit, Stall, Floor, Roof, WallCorner })
            if (category == static_cast<u32>(c))
                return true;
        return false;
    }

    std::vector<Overhead> overheads(std::span<const glm::vec3> points, std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const save::model::TerrainGrid& grid, const object::Fitted& fitted)
    {
        std::vector<Overhead> out(points.size());
        const std::vector<object::PickBox> boxes = object::pick_boxes(objects, models, grid, fitted);
        std::vector<const object::ObjectModel*> shapes(boxes.size(), nullptr);
        std::unordered_map<u64, std::vector<usize>> by_cell;
        for (usize b = 0; b < boxes.size(); ++b)
        {
            const save::model::PlacedObject& o = objects[boxes[b].object_index];
            const object::ObjectModel& model = models.find(object::model_ref(o))->second;
            const bool tree = model.category == static_cast<u32>(ItemCategory::Tree);
            if (o.hidden || (!tree && !is_roof(model.category)))
                continue;
            shapes[b] = &model;
            for (i32 x = cell_of(boxes[b].park_low.x); x <= cell_of(boxes[b].park_high.x); ++x)
                for (i32 z = cell_of(boxes[b].park_low.z); z <= cell_of(boxes[b].park_high.z); ++z)
                    by_cell[cell_key(x, z)].push_back(b);
        }
        for (usize p = 0; p < points.size(); ++p)
        {
            const glm::vec3 from = points[p] + glm::vec3(0.0f, kHeadroom, 0.0f);
            const f32 length = kRoofReach - kHeadroom;
            const auto cell = by_cell.find(cell_key(cell_of(from.x), cell_of(from.z)));
            if (cell == by_cell.end())
                continue;
            for (const usize b : cell->second)
            {
                const object::PickBox& box = boxes[b];
                if (from.x < box.park_low.x || from.x > box.park_high.x || from.z < box.park_low.z || from.z > box.park_high.z || box.park_high.y < from.y || box.park_low.y > from.y + length)
                    continue;
                // Into the object's own space, the direction kept unscaled so distances along it match.
                const Ray own { glm::vec3(box.back * glm::vec4(from, 1.0f)), glm::vec3(box.back * glm::vec4(0.0f, length, 0.0f, 0.0f)) };
                if (shapes[b]->category == static_cast<u32>(ItemCategory::Tree))
                {
                    if (const auto hit = object::box_hit(own, box.low, box.high); hit && *hit <= 1)
                        out[p].shade = true;
                }
                else if (meets_shape(own, *shapes[b], 1))
                {
                    out[p] = { true, true };
                    break;
                }
            }
        }
        return out;
    }

} // namespace rct3::park
