// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_pick.hpp"

#include "park/object_placement.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace rct3::park
{

    namespace
    {

        /// The length of the steps a line of sight is followed in over the ground, in park units.
        constexpr float kGroundStep = 0.5f;
        /// Halvings of the last step to find where the ground is met.
        constexpr int kGroundRefinements = 12;

        /// Above (true) or below the ground at `p`; off the map counts as above.
        bool above(const save::TerrainGrid& grid, glm::vec3 p)
        {
            const auto h = grid.height_at(p.x, p.z);
            return !h || p.y >= *h;
        }

        std::optional<std::pair<glm::vec3, glm::vec3>> shape_box(const ObjectModel& model)
        {
            glm::vec3 low { std::numeric_limits<float>::max() };
            glm::vec3 high { std::numeric_limits<float>::lowest() };
            for (const ModelPart& part : model.levels.front().parts)
            {
                for (const ModelVertex& v : part.vertices)
                {
                    low = glm::min(low, v.position);
                    high = glm::max(high, v.position);
                }
            }
            if (low.x > high.x)
                return std::nullopt;
            return std::pair { low, high };
        }

    } // namespace

    std::optional<float> ground_hit(const Ray& ray, const save::TerrainGrid& grid, float reach)
    {
        if (grid.empty() || !above(grid, ray.origin))
            return std::nullopt;
        const auto steps = static_cast<int>(reach / kGroundStep);
        for (int step = 1; step <= steps; ++step)
        {
            const float t = static_cast<float>(step) * kGroundStep;
            if (above(grid, ray.origin + ray.direction * t))
                continue;
            float from = t - kGroundStep;
            float to = t;
            for (int i = 0; i < kGroundRefinements; ++i)
            {
                const float middle = (from + to) / 2;
                (above(grid, ray.origin + ray.direction * middle) ? from : to) = middle;
            }
            return to;
        }
        return std::nullopt;
    }

    std::optional<float> box_hit(const Ray& ray, glm::vec3 low, glm::vec3 high)
    {
        float enter = 0;
        float leave = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis)
        {
            const float o = ray.origin[axis];
            const float d = ray.direction[axis];
            if (d == 0)
            {
                if (o < low[axis] || o > high[axis])
                    return std::nullopt;
                continue;
            }
            float a = (low[axis] - o) / d;
            float b = (high[axis] - o) / d;
            if (a > b)
                std::swap(a, b);
            enter = std::max(enter, a);
            leave = std::min(leave, b);
            if (enter > leave)
                return std::nullopt;
        }
        return enter;
    }

    std::optional<PickHit> pick_object(const Ray& ray, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const std::function<bool(const save::PlacedObject&)>& wanted)
    {
        std::optional<PickHit> best;
        float nearest = ground_hit(ray, grid).value_or(kFar);
        std::map<std::string, std::optional<std::pair<glm::vec3, glm::vec3>>, std::less<>> boxes;
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const save::PlacedObject& o = objects[i];
            if (o.hidden || !wanted(o))
                continue;
            const std::string key = model_key(o);
            const auto model = models.find(key);
            if (model == models.end() || model->second.levels.empty())
                continue;
            auto box = boxes.find(key);
            if (box == boxes.end())
                box = boxes.emplace(key, shape_box(model->second)).first;
            if (!box->second)
                continue;
            const auto own = fitted.find(o.id);
            const glm::mat4 transform = own != fitted.end() ? own->second : object_transform(o, static_cast<Positioning>(model->second.positioning), grid, model->second.squares);
            // In the model's own space the box is square to the axes; the transform keeps distances along the
            // line in step, so the hit's distance holds in the park too.
            const glm::mat4 back = glm::inverse(transform);
            const Ray local { glm::vec3(back * glm::vec4(ray.origin, 1.0f)), glm::vec3(back * glm::vec4(ray.direction, 0.0f)) };
            if (const auto t = box_hit(local, box->second->first, box->second->second); t && *t < nearest)
            {
                nearest = *t;
                best = PickHit { i, *t };
            }
        }
        return best;
    }

    std::vector<glm::vec3> object_corners(const save::PlacedObject& object, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted)
    {
        const auto model = models.find(model_key(object));
        if (model == models.end() || model->second.levels.empty())
            return { };
        const auto box = shape_box(model->second);
        if (!box)
            return { };
        const auto own = fitted.find(object.id);
        const glm::mat4 transform = own != fitted.end() ? own->second : object_transform(object, static_cast<Positioning>(model->second.positioning), grid, model->second.squares);
        std::vector<glm::vec3> corners;
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 local { (i & 1) != 0 ? box->second.x : box->first.x, (i & 2) != 0 ? box->second.y : box->first.y, (i & 4) != 0 ? box->second.z : box->first.z };
            corners.emplace_back(transform * glm::vec4(local, 1.0f));
        }
        return corners;
    }

    std::optional<PickHit> pick_person(const Ray& ray, std::span<const glm::mat4> placements, const save::TerrainGrid& grid)
    {
        std::optional<PickHit> best;
        float nearest = ground_hit(ray, grid).value_or(kFar);
        for (std::size_t i = 0; i < placements.size(); ++i)
        {
            // Like objects, a person is moved and turned without being stretched, so distances hold.
            const glm::mat4 back = glm::inverse(placements[i]);
            const Ray local { glm::vec3(back * glm::vec4(ray.origin, 1.0f)), glm::vec3(back * glm::vec4(ray.direction, 0.0f)) };
            if (const auto t = box_hit(local, kPersonLow, kPersonHigh); t && *t < nearest)
            {
                nearest = *t;
                best = PickHit { i, *t };
            }
        }
        return best;
    }

} // namespace rct3::park
