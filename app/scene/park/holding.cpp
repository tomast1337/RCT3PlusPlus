// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/holding.hpp"

#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/object/pick.hpp"
#include "park/path/drag.hpp"
#include "park/water/surface.hpp"
#include "scene/park/scene.hpp"
#include "simulation/systems.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace rct3::scene::park
{

    bool pick_up_person(ParkScene& scene, std::size_t shown)
    {
        if (!scene.simulate || shown >= scene.shown_people.size())
            return false;
        const entt::entity who = scene.shown_people[shown].guest;
        auto& r = scene.sim.registry;
        if (!r.valid(who) || !r.all_of<simulation::Pose>(who))
            return false;
        const glm::vec3 from = r.get<simulation::Pose>(who).position;
        if (!simulation::pick_up(scene.sim, who))
            return false;
        scene.holding = HeldPerson { .who = who, .from = from };
        return true;
    }

    bool hold_person(ParkScene& scene, ui::Point at, ui::Point size, bool click, bool cancel)
    {
        if (!scene.holding)
            return false;
        auto& r = scene.sim.registry;
        HeldPerson& held = *scene.holding;
        if (!r.valid(held.who) || !r.all_of<simulation::Held, simulation::Pose>(held.who))
        {
            scene.holding.reset();
            return false;
        }
        if (cancel)
        {
            simulation::let_go(scene.sim, held.who, held.from);
            scene.holding.reset();
            return true;
        }
        if (size.x > 0 && size.y > 0)
        {
            const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
            const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, ndc);
            const auto ground = rct3::park::object::ground_hit(ray, scene.grid);
            std::optional<glm::vec3> spot;
            bool allowed = false;
            // A path under the pointer, at the height the simulation walks it there.
            if (const auto under = rct3::park::path::path_under(ray, scene.footpaths.paths, scene.grid); under && std::abs(ray.direction.y) > 1e-6f)
            {
                const save::model::PathTile& p = scene.footpaths.paths[under->path];
                float nearest = std::numeric_limits<float>::max();
                for (const simulation::PathNode& n : scene.sim.paths.nodes())
                {
                    const float t = (n.height - ray.origin.y) / ray.direction.y;
                    const glm::vec3 hit = ray.origin + ray.direction * t;
                    if (n.removed || n.tile.col != p.col || n.tile.row != p.row || t <= 0 || t >= nearest || std::floor((hit.x - scene.grid.origin_x) / scene.grid.tile_size_x) != static_cast<float>(p.col) || std::floor((hit.z - scene.grid.origin_z) / scene.grid.tile_size_z) != static_cast<float>(p.row))
                        continue;
                    nearest = t;
                    spot = hit;
                    allowed = true;
                }
            }
            // A guest over a pool complex's water, paving or spa pool, nearer than any path: let go, they swim there.
            if (r.all_of<simulation::Visitor>(held.who) && std::abs(ray.direction.y) > 1e-6f)
            {
                const float half = scene.sim.paths.tile_size / 2;
                float nearest = spot ? glm::distance(ray.origin, *spot) / glm::length(ray.direction) : std::numeric_limits<float>::max();
                for (const auto [e, pool] : r.view<const simulation::SwimPool>().each())
                    for (const auto* squares : { &pool.water, &pool.paved, &pool.spa })
                        for (const glm::vec3& square : *squares)
                        {
                            const float t = (square.y - ray.origin.y) / ray.direction.y;
                            const glm::vec3 hit = ray.origin + ray.direction * t;
                            if (t <= 0 || t >= nearest || std::abs(hit.x - square.x) > half || std::abs(hit.z - square.z) > half || !simulation::pool_door_at(scene.sim, hit))
                                continue;
                            nearest = t;
                            spot = hit;
                            allowed = true;
                        }
            }
            if (!spot && ground)
            {
                spot = ray.origin + ray.direction * *ground;
                const int col = static_cast<int>(std::floor((spot->x - scene.grid.origin_x) / scene.grid.tile_size_x));
                const int row = static_cast<int>(std::floor((spot->z - scene.grid.origin_z) / scene.grid.tile_size_z));
                // shortcut: the park's lakes never take them, as nobody treads water there yet; let them in once they do.
                const auto water = rct3::park::water::water_hit(ray, scene.water.surface);
                allowed = scene.grid.contains(col, row) && rct3::park::owned_outright(scene.grid.at(col, row)) && !(water && *water < *ground + 1e-3f);
            }
            if (spot)
            {
                held.spot = spot;
                held.allowed = allowed;
                auto& pose = r.get<simulation::Pose>(held.who);
                pose.position = pose.was = *spot + glm::vec3 { 0, r.get<simulation::Held>(held.who).height.value, 0 };
            }
        }
        if (click && held.spot && held.allowed && simulation::let_go(scene.sim, held.who, *held.spot))
            scene.holding.reset();
        return true;
    }

} // namespace rct3::scene::park
