// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/ride_placing.hpp"

#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/object_pick.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_scene.hpp"
#include "simulation/import_park.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::scene
{

    std::optional<glm::vec2> ground_spot(const ParkScene& scene, ui::Point at, ui::Point size)
    {
        if (size.x <= 0 || size.y <= 0)
            return std::nullopt;
        const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
        const auto distance = park::ground_hit(ray, scene.grid);
        if (!distance)
            return std::nullopt;
        const glm::vec3 hit = ray.origin + ray.direction * *distance;
        return glm::vec2 { (hit.x - scene.grid.origin_x) / scene.grid.tile_size_x, (hit.z - scene.grid.origin_z) / scene.grid.tile_size_z };
    }

    std::optional<park::PathSpot> ground_tile(const ParkScene& scene, ui::Point at, ui::Point size)
    {
        const auto spot = ground_spot(scene, at, size);
        if (!spot)
            return std::nullopt;
        const auto col = static_cast<int>(std::floor(spot->x));
        const auto row = static_cast<int>(std::floor(spot->y));
        if (!scene.grid.contains(col, row))
            return std::nullopt;
        return park::PathSpot { col, row };
    }

    namespace
    {
        /// The object the ghost shows: the ride, or the entrance or exit pointed at.
        std::optional<save::PlacedObject> ghost_object(const RidePlacing& p)
        {
            if (!p.item || !p.site)
                return std::nullopt;
            save::PlacedObject o;
            o.overlay = p.item->overlay;
            o.base_height = p.site->height;
            o.force_absolute_height = true;
            if (p.step == RidePlacing::Step::Ride)
            {
                o.symbol = p.item->scenery;
                o.col = p.site->col;
                o.row = p.site->row;
                o.rotation = static_cast<std::uint8_t>(p.site->turns);
                return o;
            }
            if (!p.door)
                return std::nullopt;
            o.symbol = p.step == RidePlacing::Step::Entrance ? p.item->entrance : p.item->exit;
            o.overlay = p.item->door_overlay;
            o.col = p.door->col;
            o.row = p.door->row;
            o.rotation = p.door->direction;
            return o;
        }

        std::uint64_t last_id(const ParkScene& scene)
        {
            std::uint64_t last = 0;
            for (const save::PlacedObject& o : scene.objects)
                last = std::max(last, o.id);
            for (const save::Ride& r : scene.rides.rides)
                last = std::max(last, r.base.id);
            for (const save::Stall& s : scene.rides.stalls)
                last = std::max(last, s.base.id);
            return last;
        }

        /// Adds `o` to the park with a new id, in the colours its item gives, as part of the ride being placed.
        std::uint64_t add_object(ParkScene& scene, save::PlacedObject o)
        {
            o.id = last_id(scene) + 1;
            o.owner_ref = o.id;
            if (const auto model = scene.models.find(park::model_key(o)); model != scene.models.end())
                o.colours = model->second.default_colours;
            scene.attractions[o.id] = { park::AttractionRef::Kind::Ride, scene.placing.ride };
            scene.objects.push_back(o);
            objects_changed(scene);
            return o.id;
        }

        /// Has the simulation run the rides placed here whose entrances now have a path behind them.
        void join_rides(render::Device& device, ParkScene& scene)
        {
            RidePlacing& p = scene.placing;
            std::erase_if(p.unjoined, [&](std::size_t i) {
                if (i >= scene.rides.rides.size())
                    return true;
                save::RideModel one;
                one.rides.push_back(scene.rides.rides[i]);
                const auto timings = simulation::flat_ride_timings(one, scene.objects, [&](const save::PlacedObject& o) -> const park::ObjectModel* {
                    const auto found = scene.models.find(park::model_key(o));
                    return found != scene.models.end() ? &found->second : nullptr;
                });
                const auto added = simulation::import_rides(scene.sim, one, timings);
                if (added.empty() || added.front() == entt::null)
                    return false;
                scene.ride_entities.resize(scene.rides.rides.size(), entt::null);
                scene.ride_entities[i] = added.front();
                seat_flat_ride(device, scene, i);
                logging::logger("park").info("{} joins the paths", scene.rides.rides[i].base.name);
                return true;
            });
        }
    } // namespace

    bool start_ride_placing(ParkScene& scene, const park::BuildItem& item)
    {
        if (item.kind != park::BuildKind::FlatRide || item.scenery.empty() || item.width == 0 || item.length == 0)
            return false;
        RidePlacing& p = scene.placing;
        p.item = item;
        p.step = RidePlacing::Step::Ride;
        p.site.reset();
        p.door.reset();
        p.entrance.reset();
        p.ghost_mesh = { };
        return true;
    }

    void stop_ride_placing(ParkScene& scene)
    {
        scene.placing.item.reset();
        scene.placing.ghost_mesh = { };
    }

    bool ride_placing(const ParkScene& scene)
    {
        return scene.placing.item.has_value();
    }

    void update_ride_placing(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, const ui::KeyboardState& keys)
    {
        RidePlacing& p = scene.placing;
        join_rides(device, scene);
        if (!p.item)
            return;
        if (p.step == RidePlacing::Step::Ride && !typing(scene))
        {
            for (const ui::KeyPress& k : keys.presses)
                if (k.key == ui::Key::Letter && k.character == U'z')
                    p.turns = (p.turns + 1) % 4;
        }
        // shortcut: worked out again only when objects come or go, as path building does.
        if (p.solids_of != scene.objects.size())
        {
            p.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
            p.solids_of = scene.objects.size();
        }
        const auto tile = over ? std::nullopt : ground_tile(scene, at, size);
        if (!tile)
        {
            if (p.step == RidePlacing::Step::Ride)
                p.site.reset();
            p.door.reset();
            p.ghost_mesh = { };
            return;
        }
        const std::span<const save::PathTile> paths = scene.footpaths.paths;
        if (p.step == RidePlacing::Step::Ride)
        {
            const std::array squares { static_cast<std::int32_t>(p.item->width), static_cast<std::int32_t>(p.item->length) };
            p.site = park::ride_site(tile->col, tile->row, squares, p.turns, scene.grid);
            p.problem = park::site_problem(*p.site, scene.grid, paths, p.solids);
            p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price;
        }
        else
        {
            p.door = park::door_spot(*p.site, tile->col, tile->row);
            p.problem = park::door_problem(*p.site, *p.door, p.step == RidePlacing::Step::Exit ? p.entrance : std::nullopt, scene.grid, paths, p.solids);
            p.affordable = true;
        }
        p.ghost_mesh = { };
        const auto o = ghost_object(p);
        if (!o || o->symbol.empty() || !load_object_model(scene, p.models, assets, *o))
            return;
        const std::array<save::PlacedObject, 1> one { *o };
        p.ghost_mesh = park::object_mesh(one, scene.models, scene.grid);
        const glm::vec4 tint = p.problem == park::SiteProblem::None && p.affordable ? kGhostTint : kRefusedTint;
        for (park::GroundVertex& v : p.ghost_mesh.vertices)
            v.colour *= tint;
        ghost_textures(device, scene, p.ghost_mesh);
    }

    bool ride_placing_click(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        RidePlacing& p = scene.placing;
        if (!p.item)
            return false;
        const auto o = ghost_object(p);
        if (!o || p.problem != park::SiteProblem::None || !p.affordable || !scene.models.contains(park::model_key(*o)))
            return true;
        wait_for_build(scene);
        if (p.step == RidePlacing::Step::Ride)
        {
            park::level_ground(scene.grid, *p.site);
            rebuild_ground(device, scene, assets);
            p.ride = scene.rides.rides.size();
            save::Ride& ride = scene.rides.rides.emplace_back();
            ride.kind = save::RideKind::Flat;
            const std::uint64_t object = add_object(scene, *o);
            ride.base.id = last_id(scene) + 1;
            ride.base.symbol = p.item->symbol + ":anr";
            ride.base.overlay_path = p.item->overlay;
            ride.base.name = p.item->name_text;
            // shortcut: opens at once, as rides cannot be opened from their window yet; build them closed, as
            // the game does, once they can.
            ride.base.state = save::AttractionState::Open;
            ride.base.placement = object;
            ride.base.spot = save::PlacementSpot { object, p.site->col, p.site->row, p.site->height, static_cast<std::uint8_t>(p.site->turns) };
            ride.base.facing = static_cast<std::uint8_t>(p.site->turns);
            if (scene.park.cash)
                *scene.park.cash -= p.item->price;
            logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, p.site->col, p.site->row, p.site->height);
            p.step = RidePlacing::Step::Entrance;
        }
        else
        {
            // The door stands on ground levelled to the ride's height, as the ride does.
            park::level_ground(scene.grid, park::RideSite { p.door->col, p.door->row, 0, p.site->height, { 1, 1 } });
            rebuild_ground(device, scene, assets);
            const std::uint64_t object = add_object(scene, *o);
            save::Ride& ride = scene.rides.rides.at(p.ride);
            if (ride.stations.empty())
                ride.stations.emplace_back();
            const save::PlacementSpot spot { object, p.door->col, p.door->row, p.site->height, p.door->direction };
            if (p.step == RidePlacing::Step::Entrance)
            {
                ride.stations.front().entrance = object;
                ride.stations.front().entrance_spot = spot;
                p.entrance = p.door;
                p.step = RidePlacing::Step::Exit;
            }
            else
            {
                ride.stations.front().exit = object;
                ride.stations.front().exit_spot = spot;
                p.unjoined.push_back(p.ride);
                stop_ride_placing(scene);
            }
        }
        p.door.reset();
        p.ghost_mesh = { };
        return true;
    }

    void draw_ride_placing_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.placing.item && !scene.placing.ghost_mesh.vertices.empty())
            draw_ghost_mesh(device, scene, scene.placing.ghost_mesh, transform, light);
    }

} // namespace rct3::scene
