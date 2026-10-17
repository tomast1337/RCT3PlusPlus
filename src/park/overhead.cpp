// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/overhead.hpp"

#include "park/item_category.hpp"
#include "park/object_pick.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace rct3::park
{

    namespace
    {
        constexpr float kCell = 4; ///< Park units across a cell objects are sorted into for a quick first look

        std::uint64_t cell_key(int x, int z)
        {
            return std::uint64_t { static_cast<std::uint32_t>(x) } << 32 | static_cast<std::uint32_t>(z);
        }

        int cell_of(float v)
        {
            return static_cast<int>(std::floor(v / kCell));
        }

        /// How far along `ray` it meets the triangle a, b, c, if it does at all ahead.
        std::optional<float> triangle_hit(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c)
        {
            const glm::vec3 ab = b - a;
            const glm::vec3 ac = c - a;
            const glm::vec3 p = glm::cross(ray.direction, ac);
            const float det = glm::dot(ab, p);
            if (std::abs(det) < 1e-8f)
                return std::nullopt;
            const glm::vec3 t = ray.origin - a;
            const float u = glm::dot(t, p) / det;
            if (u < 0 || u > 1)
                return std::nullopt;
            const glm::vec3 q = glm::cross(t, ab);
            const float v = glm::dot(ray.direction, q) / det;
            if (v < 0 || u + v > 1)
                return std::nullopt;
            const float along = glm::dot(ac, q) / det;
            return along >= 0 ? std::optional { along } : std::nullopt;
        }

        bool meets_shape(const Ray& ray, const ObjectModel& model, float reach)
        {
            for (const ModelPart& part : model.levels.front().parts)
                for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3)
                {
                    const auto at = [&](std::size_t k) { return part.vertices[part.indices[i + k]].position; };
                    if (part.indices[i + 2] >= part.vertices.size() || part.indices[i] >= part.vertices.size() || part.indices[i + 1] >= part.vertices.size())
                        continue;
                    if (const auto hit = triangle_hit(ray, at(0), at(1), at(2)); hit && *hit <= reach)
                        return true;
                }
            return false;
        }
    } // namespace

    bool is_roof(std::uint32_t category) noexcept
    {
        using enum ItemCategory;
        for (const ItemCategory c : { Wall, SmallScenery, MediumScenery, Animatronic, ParkEntrance, Ride, RideEntrance, RideExit, Stall, Floor, Roof, WallCorner })
            if (category == static_cast<std::uint32_t>(c))
                return true;
        return false;
    }

    std::vector<Overhead> overheads(std::span<const glm::vec3> points, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted)
    {
        std::vector<Overhead> out(points.size());
        const std::vector<PickBox> boxes = pick_boxes(objects, models, grid, fitted);
        std::vector<const ObjectModel*> shapes(boxes.size(), nullptr);
        std::unordered_map<std::uint64_t, std::vector<std::size_t>> by_cell;
        for (std::size_t b = 0; b < boxes.size(); ++b)
        {
            const save::PlacedObject& o = objects[boxes[b].index];
            const ObjectModel& model = models.find(model_key(o))->second;
            const bool tree = model.category == static_cast<std::uint32_t>(ItemCategory::Tree);
            if (o.hidden || (!tree && !is_roof(model.category)))
                continue;
            shapes[b] = &model;
            for (int x = cell_of(boxes[b].park_low.x); x <= cell_of(boxes[b].park_high.x); ++x)
                for (int z = cell_of(boxes[b].park_low.z); z <= cell_of(boxes[b].park_high.z); ++z)
                    by_cell[cell_key(x, z)].push_back(b);
        }
        for (std::size_t p = 0; p < points.size(); ++p)
        {
            const glm::vec3 from = points[p] + glm::vec3(0.0f, kHeadroom, 0.0f);
            const float length = kRoofReach - kHeadroom;
            const auto cell = by_cell.find(cell_key(cell_of(from.x), cell_of(from.z)));
            if (cell == by_cell.end())
                continue;
            for (const std::size_t b : cell->second)
            {
                const PickBox& box = boxes[b];
                if (from.x < box.park_low.x || from.x > box.park_high.x || from.z < box.park_low.z || from.z > box.park_high.z || box.park_high.y < from.y || box.park_low.y > from.y + length)
                    continue;
                // Into the object's own space, the direction kept unscaled so distances along it match.
                const Ray own { glm::vec3(box.back * glm::vec4(from, 1.0f)), glm::vec3(box.back * glm::vec4(0.0f, length, 0.0f, 0.0f)) };
                if (shapes[b]->category == static_cast<std::uint32_t>(ItemCategory::Tree))
                {
                    if (const auto hit = box_hit(own, box.low, box.high); hit && *hit <= 1)
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
