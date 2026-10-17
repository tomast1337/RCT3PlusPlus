// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_patrol.hpp"

#include "park/camera.hpp"
#include "park/object_pick.hpp"
#include "park/terrain_mesh.hpp"
#include "scene/park_scene.hpp"
#include "simulation/components.hpp"
#include "simulation/systems.hpp"
#include "ui/screens/person_info.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace rct3::scene
{

    namespace
    {
        constexpr float kShownAlpha = 0.45f;
        constexpr float kOthersAlpha = 0.25f;
        constexpr float kPatrolLift = 0.05f; ///< Over the ground or the path, so the tint is not lost in it

        glm::vec3 role_colour(simulation::StaffRole role)
        {
            switch (role)
            {
                case simulation::StaffRole::Handyman:
                    return { 0.3f, 0.6f, 1.0f };
                case simulation::StaffRole::Mechanic:
                    return { 1.0f, 0.6f, 0.2f };
                case simulation::StaffRole::Guard:
                    return { 0.9f, 0.2f, 0.2f };
                default:
                    return { 0.6f, 1.0f, 0.4f };
            }
        }

        std::optional<glm::ivec2> tile_at(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            if (size.x <= 0 || size.y <= 0)
                return std::nullopt;
            const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
            const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, ndc);
            const auto distance = park::ground_hit(ray, scene.grid);
            if (!distance)
                return std::nullopt;
            const glm::vec3 hit = ray.origin + ray.direction * *distance;
            const glm::ivec2 tile { static_cast<int>(std::floor((hit.x - scene.grid.origin_x) / scene.grid.tile_size_x)), static_cast<int>(std::floor((hit.z - scene.grid.origin_z) / scene.grid.tile_size_z)) };
            return scene.grid.contains(tile.x, tile.y) ? std::optional(tile) : std::nullopt;
        }

        using PathHeights = std::map<std::pair<int, int>, float>; ///< Of the highest path on each tile with paths

        void add_area(park::TerrainMesh& mesh, const ParkScene& scene, const PathHeights& heights, const simulation::Patrol& patrol, glm::vec4 colour)
        {
            for (const glm::ivec2 tile : patrol.tiles)
            {
                // Over the highest path on the tile, else over the ground at each corner.
                const auto on = heights.find({ tile.x, tile.y });
                const float path = on != heights.end() ? on->second : -std::numeric_limits<float>::infinity();
                const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const glm::vec2 corner : { glm::vec2 { 0, 0 }, glm::vec2 { 1, 0 }, glm::vec2 { 1, 1 }, glm::vec2 { 0, 1 } })
                {
                    const float x = scene.grid.origin_x + (static_cast<float>(tile.x) + corner.x) * scene.grid.tile_size_x;
                    const float z = scene.grid.origin_z + (static_cast<float>(tile.y) + corner.y) * scene.grid.tile_size_z;
                    const float y = std::max(scene.grid.height_at(x, z).value_or(0.0f), path) + kPatrolLift;
                    mesh.vertices.push_back({ .position = { x, y, z }, .colour = colour, .normal = { 0, 1, 0 } });
                }
                for (const std::uint32_t i : { 0u, 2u, 1u, 0u, 3u, 2u })
                    mesh.indices.push_back(first + i);
            }
        }
    } // namespace

    entt::entity shown_staff(const ParkScene& scene)
    {
        if (!scene.simulate || !scene.shown_person || *scene.shown_person >= scene.shown_people.size())
            return entt::null;
        const ShownPerson& p = scene.shown_people[*scene.shown_person];
        return p.staff && scene.sim.registry.valid(p.guest) && scene.sim.registry.all_of<simulation::Staff>(p.guest) ? p.guest : entt::null;
    }

    bool painting_patrol(const ParkScene& scene)
    {
        return scene.painting_patrol && shown_staff(scene) != entt::null;
    }

    bool paint_patrol(ParkScene& scene, ui::Point at, ui::Point size, bool down)
    {
        const entt::entity staff = shown_staff(scene);
        if (!down || !scene.painting_patrol || staff == entt::null)
            return std::exchange(scene.patrol_stroke, std::nullopt).has_value();
        const auto tile = tile_at(scene, at, size);
        if (!tile)
            return false;
        if (!scene.patrol_stroke)
        {
            // Adds, unless it starts on the area.
            const auto* patrol = scene.sim.registry.try_get<simulation::Patrol>(staff);
            scene.patrol_stroke = patrol == nullptr || !std::ranges::contains(patrol->tiles, *tile);
        }
        const int across = ui::kPatrolBrushes.at(static_cast<std::size_t>(std::clamp(scene.patrol_brush, 0, static_cast<int>(ui::kPatrolBrushes.size()) - 1)));
        simulation::paint_patrol(scene.sim.registry, staff, *tile, across, *scene.patrol_stroke);
        return false;
    }

    void copy_patrol(ParkScene& scene)
    {
        const entt::entity staff = shown_staff(scene);
        if (staff == entt::null)
            return;
        auto& r = scene.sim.registry;
        const auto* patrol = r.try_get<simulation::Patrol>(staff);
        const simulation::StaffRole role = r.get<simulation::Staff>(staff).role;
        for (const auto [e, other] : r.view<const simulation::Staff>().each())
        {
            if (e == staff || other.role != role)
                continue;
            if (patrol != nullptr)
                r.emplace_or_replace<simulation::Patrol>(e, simulation::Patrol { patrol->tiles }); // The tiles, not the zone
            else
                r.remove<simulation::Patrol>(e);
        }
    }

    void draw_patrol(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        const entt::entity shown = shown_staff(scene);
        if (shown == entt::null)
            return;
        PathHeights heights;
        for (const simulation::PathNode& n : scene.sim.paths.nodes())
        {
            auto [at, added] = heights.try_emplace({ n.tile.col, n.tile.row }, n.height);
            at->second = std::max(at->second, n.height);
        }
        park::TerrainMesh mesh;
        // The others first and faint, then the shown one's over them.
        const auto& r = scene.sim.registry;
        for (const auto [e, staff, patrol] : r.view<const simulation::Staff, const simulation::Patrol>().each())
            if (e != shown)
                add_area(mesh, scene, heights, patrol, glm::vec4(role_colour(staff.role), kOthersAlpha));
        if (const auto* patrol = r.try_get<simulation::Patrol>(shown))
            add_area(mesh, scene, heights, *patrol, glm::vec4(role_colour(r.get<simulation::Staff>(shown).role), kShownAlpha));
        if (mesh.indices.empty())
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
        const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
        if (!vertices || !indices)
            return;
        render::DrawCommand draw;
        draw.pipeline = scene.ground_pipeline;
        draw.vertices = *vertices;
        draw.indices = *indices;
        draw.index_type = render::IndexType::U32;
        draw.first = 0;
        draw.count = static_cast<std::uint32_t>(mesh.indices.size());
        draw.transform = transform;
        draw.texture = scene.blank;
        draw.light = light;
        device.draw(draw);
    }

} // namespace rct3::scene
