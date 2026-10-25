// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/ride_placing.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "park/bone_poses.hpp"
#include "park/camera.hpp"
#include "park/edge_walls.hpp"
#include "park/item_category.hpp"
#include "park/object_pick.hpp"
#include "park/path_decks.hpp"
#include "park/path_furniture.hpp"
#include "park/ride_fences.hpp"
#include "park/ride_placement.hpp"
#include "park/support_columns.hpp"
#include "park/water_fill.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "scene/track_building.hpp"
#include "simulation/import_park.hpp"
#include "ui/screens/scenery_info.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

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

    std::optional<save::Tile> ground_tile(const ParkScene& scene, ui::Point at, ui::Point size)
    {
        const auto spot = ground_spot(scene, at, size);
        if (!spot)
            return std::nullopt;
        const auto col = static_cast<int>(std::floor(spot->x));
        const auto row = static_cast<int>(std::floor(spot->y));
        if (!scene.grid.contains(col, row))
            return std::nullopt;
        return save::Tile { col, row };
    }

    namespace
    {
        /// How far under an object's top the pointer may meet its box and still count as on the top.
        constexpr float kTopSlack = 0.05f;
        /// The placing panel's tabs' places on its ring; the first is left for one that finds the item in its list.
        constexpr int kTurnSlot = 1;
        constexpr int kColourSlot = 2;

        /// The object the ghost shows: the ride, or the entrance or exit pointed at.
        std::optional<save::PlacedObject> ghost_object(const RidePlacing& p)
        {
            if (!p.item || !p.site)
                return std::nullopt;
            save::PlacedObject o;
            o.overlay = p.item->overlay;
            o.base_height = p.site->height;
            if (p.step == RidePlacing::Step::Scenery)
            {
                o.symbol = p.item->symbol;
                o.col = p.site->col;
                o.row = p.site->row;
                o.rotation = p.site->turns;
                o.quadrant = p.quadrant;
                o.height_offset = p.site->height - p.ground;
                o.force_absolute_height = p.absolute;
                o.colours = p.colours;
                return o;
            }
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

        /// Adds `o` to the park with a new id, in the colours its item gives, as part of the ride being placed
        /// unless it is scenery. Its supports follow with the park's (scene/park_supports.hpp).
        std::uint64_t add_object(ParkScene& scene, save::PlacedObject o)
        {
            o.id = last_id(scene) + 1;
            o.owner_ref = o.id;
            // Scenery comes in the colours its ghost shows.
            if (scene.placing.step != RidePlacing::Step::Scenery)
            {
                if (const auto model = scene.models.find(park::model_ref(o)); model != scene.models.end())
                    o.colours = model->second.default_colours;
                scene.attractions[o.id] = { scene.placing.item->stall ? park::AttractionRef::Kind::Stall : park::AttractionRef::Kind::Ride, scene.placing.ride };
            }
            scene.objects.push_back(o);
            objects_changed(scene);
            return o.id;
        }

        /// Gives `ride` its fence round it and beside the doors that stand, in place of the one it had, and paints
        /// the doors in the fence's colours.
        void fence_ride(ParkScene& scene, const ui::InterfaceAssets& assets, save::Ride& ride)
        {
            std::vector<std::uint64_t>& fence = scene.placing.fence;
            for (save::PlacedObject& o : scene.objects)
            {
                if (std::ranges::contains(fence, o.id))
                    o.hidden = true;
            }
            fence.clear();
            if (scene.placing.item)
            {
                ride.fence_symbol = scene.placing.item->fence;
                ride.fence_overlay = scene.placing.item->fence_overlay;
            }
            const park::ObjectModel* item = nullptr;
            for (const save::PlacedObject& o : scene.objects)
            {
                if (ride.base.spot && o.id == ride.base.spot->object)
                {
                    const auto found = scene.models.find(park::model_ref(o));
                    item = found != scene.models.end() ? &found->second : nullptr;
                }
            }
            std::vector<save::PlacedObject> pieces = park::ride_fences(ride, item, last_id(scene));
            if (pieces.empty() || !load_object_model(scene, scene.placing.models, assets, pieces.front()))
                return;
            const auto& colours = scene.models.at(park::model_key(pieces.front())).default_colours;
            for (std::size_t i = 0; i < ride.fence_colours.size(); ++i)
                ride.fence_colours[i] = static_cast<std::uint8_t>(colours[i]);
            // The doors are painted as the fence is.
            for (save::PlacedObject& o : scene.objects)
            {
                if (!ride.stations.empty() && (o.id == ride.stations.front().entrance || o.id == ride.stations.front().exit))
                    o.colours = colours;
            }
            for (save::PlacedObject& piece : pieces)
            {
                piece.colours = colours;
                fence.push_back(piece.id);
                scene.objects.push_back(std::move(piece));
            }
            objects_changed(scene);
        }

        /// Has the simulation run the rides placed here whose entrances now have a path behind them, and the stalls
        /// placed here that now face one.
        void join_rides(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            RidePlacing& p = scene.placing;
            std::erase_if(p.unjoined, [&](std::size_t i) {
                if (i >= scene.rides.rides.size())
                    return true;
                save::Ride& ride = scene.rides.rides[i];
                // A tracked ride runs its train round the course it was built with.
                const auto& course = i < scene.courses.size() ? scene.courses[i] : std::nullopt;
                std::array coaster { course ? simulation::coaster_of(*course, ride) : std::nullopt };
                // Trains built with the ride stand at its station, the first's front where trains set off and each
                // other queued behind the one before, from where they run up to the station in turn.
                if (const auto fits = scene.car_fits.find(i); coaster[0] && fits != scene.car_fits.end() && !ride.trains.empty())
                {
                    float front = coaster[0]->start;
                    for (save::Train& train : ride.trains)
                    {
                        park::stand_train(*course, train, fits->second, std::fmod(front + course->length(), course->length()));
                        front -= train.length + park::kTrainGap;
                    }
                    coaster[0] = simulation::coaster_of(*course, ride);
                }
                save::RideModel one;
                one.rides.push_back(ride);
                const auto timings = simulation::flat_ride_timings(one, scene.objects, [&](const save::PlacedObject& o) -> const park::ObjectModel* {
                    const auto found = scene.models.find(park::model_ref(o));
                    return found != scene.models.end() ? &found->second : nullptr;
                });
                const auto added = simulation::import_rides(scene.sim, one, timings, coaster);
                if (added.empty() || added.front() == entt::null)
                    return false;
                scene.ride_entities.resize(scene.rides.rides.size(), entt::null);
                scene.ride_entities[i] = added.front();
                if (const auto show = std::ranges::find(scene.shows, ride.base.id, &save::RideShow::ride); show != scene.shows.end())
                    stage_show(scene, *show);
                seat_flat_ride(device, scene, i);
                if (assets.install)
                {
                    scene.train_limits.erase(i);
                    if (scene.car_fits.erase(i) > 0)
                        add_ride_cars(device, scene, *assets.install, i, 1);
                    board_tracked_ride(device, scene, *assets.install, i);
                }
                logging::logger("park").info("{} joins the paths", ride.base.name);
                return true;
            });
            // A stall serves from the path on the tile it faces.
            std::erase_if(p.unjoined_stalls, [&](std::size_t i) {
                if (i >= scene.rides.stalls.size())
                    return true;
                save::RideModel one;
                one.stalls.push_back(scene.rides.stalls[i]);
                return simulation::import_stalls(scene.sim, one) > 0;
            });
        }

        /// The middle of `site` at its height, where what it cost floats up from.
        glm::vec3 site_point(const ParkScene& scene, const park::RideSite& site)
        {
            const save::TerrainGrid& g = scene.grid;
            return { g.world_x(site.col) + static_cast<float>(site.span[0]) * g.tile_size_x / 2, static_cast<float>(site.height), g.world_z(site.row) + static_cast<float>(site.span[1]) * g.tile_size_z / 2 };
        }
    } // namespace

    bool start_ride_placing(ParkScene& scene, const park::BuildItem& item)
    {
        const bool scenery = item.kind == park::BuildKind::Scenery && !item.symbol.empty();
        // A stall is placed whole as a flat ride is, without doors.
        const bool whole = item.kind == park::BuildKind::FlatRide || (item.kind == park::BuildKind::Shop && item.stall);
        if (!scenery && (!whole || item.scenery.empty() || item.width == 0 || item.length == 0))
            return false;
        RidePlacing& p = scene.placing;
        p.item = item;
        p.step = scenery ? RidePlacing::Step::Scenery : RidePlacing::Step::Ride;
        p.site.reset();
        p.door.reset();
        p.entrance.reset();
        p.stations.clear();
        p.ghost_mesh = { };
        p.panel_item.clear();
        return true;
    }

    bool start_door_placing(ParkScene& scene, std::size_t ride, const park::BuildItem& item, std::vector<save::TrackPiece> stations, bool exit)
    {
        if (stations.empty() || item.entrance.empty() || item.exit.empty() || ride >= scene.rides.rides.size())
            return false;
        RidePlacing& p = scene.placing;
        p.item = item;
        p.step = exit ? RidePlacing::Step::Exit : RidePlacing::Step::Entrance;
        p.ride = ride;
        p.site.reset();
        p.door.reset();
        p.ghost_mesh = { };
        p.panel_item.clear();
        p.stations = std::move(stations);
        // The other door keeps its tile.
        p.entrance.reset();
        if (const auto& doors = scene.rides.rides[ride].stations; !doors.empty())
        {
            if (const auto& other = exit ? doors.front().entrance_spot : doors.front().exit_spot)
                p.entrance = park::DoorSpot { other->col, other->row, other->direction, other->height };
        }
        return true;
    }

    void stop_ride_placing(ParkScene& scene)
    {
        scene.placing.item.reset();
        scene.placing.ghost_mesh = { };
        scene.placing.preview_mesh = { };
        scene.placing.preview_bones.clear();
        scene.placing.panel_item.clear();
    }

    bool ride_placing(const ParkScene& scene)
    {
        return scene.placing.item.has_value();
    }

    void update_ride_placing(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, bool shift, std::span<const ui::PlayCommand> commands)
    {
        RidePlacing& p = scene.placing;
        join_rides(device, scene, assets);
        // A ride that is one building borrows the round panel while it is set down (track_building.cpp).
        if (!p.item && scene.building.ride)
            return;
        auto panel = scene.desktop->root().find_as<ui::InfoPanel>(kPlacingName);
        if (panel && panel->take_closed())
            stop_ride_placing(scene);
        const bool whole = p.step == RidePlacing::Step::Ride || p.step == RidePlacing::Step::Scenery;
        if (panel && panel->showing() && (!p.item || !whole))
            panel->hide();
        if (!p.item)
            return;
        if (whole && !typing(scene))
        {
            for (const ui::PlayCommand c : commands)
            {
                if (c == ui::PlayCommand::TurnItemRight)
                    p.turns = (p.turns + 1) % 4;
                else if (c == ui::PlayCommand::TurnItemLeft)
                    p.turns = (p.turns + 3) % 4;
            }
        }
        if (whole)
        {
            // The item alone, at the grid's corner, for the round view; a ride as the scenery it is placed as.
            const bool scenery = p.step == RidePlacing::Step::Scenery;
            save::PlacedObject alone;
            alone.symbol = scenery ? p.item->symbol : p.item->scenery;
            alone.overlay = p.item->overlay;
            alone.force_absolute_height = true;
            alone.colours = p.colours;
            const auto show_alone = [&] {
                alone.colours = p.colours;
                const std::array<save::PlacedObject, 1> one { alone };
                p.preview_mesh = park::object_mesh(one, scene.models, scene.grid);
                ghost_textures(device, scene, p.preview_mesh);
                p.preview_bones.clear();
                if (const park::ObjectModel& model = scene.models.at(park::model_key(alone)); model.rig)
                {
                    std::tie(p.preview_moving, p.preview_bones) = park::boned_meshes(alone, model, scene.grid);
                    ghost_textures(device, scene, p.preview_moving.mesh);
                }
            };
            if (p.panel_item != alone.symbol && load_object_model(scene, p.models, assets, alone))
            {
                // A new pick starts in the item's own colours.
                p.panel_item = alone.symbol;
                p.colours = scene.models.at(park::model_key(alone)).default_colours;
                show_alone();
                if (panel)
                {
                    std::vector<ui::InfoTab> tabs;
                    // Walls and other items that turn to the pointer have nothing to turn.
                    const auto positioning = static_cast<park::Positioning>(scene.models.at(park::model_key(alone)).positioning);
                    if (!scenery || !park::snapped_turns(positioning, { }))
                    {
                        ui::InfoTab& turn = tabs.emplace_back();
                        turn.icon = "IconAttractionEditorRotate";
                        turn.help = assets.strings->get("ATE_TT_Rotate");
                        turn.slot = kTurnSlot;
                        turn.action = true;
                    }
                    for (const ui::SceneryTabInfo& t : ui::scenery_tabs({ .colour_slots = park::colour_slots(scene.models.at(park::model_key(alone))), .colours = p.colours }))
                    {
                        // A ride takes its own colours when placed.
                        if (t.tab != ui::SceneryTab::Colour || !scenery)
                            continue;
                        ui::InfoTab& colour = tabs.emplace_back();
                        colour.icon = t.icon;
                        colour.help = assets.strings->get(t.help);
                        colour.slot = kColourSlot;
                        colour.paints = t.paints;
                    }
                    const std::size_t none = tabs.size();
                    panel->show(std::move(tabs), none);
                }
            }
            if (panel && panel->showing() && p.panel_item == alone.symbol)
            {
                // Each press of the tab turns it a quarter, always the same way.
                if (panel->take_action())
                    p.turns = (p.turns + 1) % 4;
                if (const auto paint = panel->take_paint())
                {
                    const auto slots = ui::scenery_paint_slots({ .colour_slots = park::colour_slots(scene.models.at(park::model_key(alone))), .colours = p.colours });
                    if (paint->pot < slots.size())
                    {
                        p.colours.at(slots[paint->pot]) = static_cast<std::int32_t>(paint->colour);
                        show_alone();
                    }
                }
                p.preview_camera = park::framed(park::object_corners(alone, scene.models, scene.grid, scene.fitted), kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
                // It moves as it will in the park, as scenery standing still does.
                if (const park::ObjectModel& model = scene.models.at(park::model_key(alone)); model.rig && !p.preview_bones.empty())
                {
                    const auto [motion, time] = park::flat_ride_motion(model.animation_seconds, 0, 0, false, scene.clock);
                    const std::vector<glm::mat4> moves = park::bone_moves(*model.rig, motion, time);
                    const glm::mat4 placed = park::placed_transform(alone, &model, scene.grid, scene.fitted);
                    std::vector<glm::mat4> places(p.preview_bones.size(), placed);
                    for (std::size_t k = 0; k < places.size(); ++k)
                        if (static_cast<std::size_t>(p.preview_bones[k]) < moves.size())
                            places[k] = placed * moves[static_cast<std::size_t>(p.preview_bones[k])];
                    park::move_meshes(p.preview_moving, places);
                }
            }
        }
        // shortcut: worked out again only when objects come or go, as path building does.
        if (p.solids_of != scene.objects.size())
        {
            p.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
            p.solids_of = scene.objects.size();
        }
        const auto show_ghost = [&] {
            p.ghost_mesh = { };
            const auto o = ghost_object(p);
            if (!o || o->symbol.empty() || !load_object_model(scene, p.models, assets, *o))
                return;
            std::vector<save::PlacedObject> shown { *o };
            if (const park::SupportSets* sets = support_sets(scene, assets))
            {
                for (save::PlacedObject& piece : supports_under(scene, shown, *sets))
                {
                    if (load_object_model(scene, p.models, assets, piece))
                        shown.push_back(std::move(piece));
                }
            }
            p.ghost_mesh = park::object_mesh(shown, scene.models, scene.grid);
            const glm::vec4 tint = p.problem == park::SiteProblem::None && p.affordable ? kGhostTint : kRefusedTint;
            for (park::GroundVertex& v : p.ghost_mesh.vertices)
                v.colour *= tint;
            ghost_textures(device, scene, p.ghost_mesh);
        };
        // While Shift is held the height follows the pointer up and down an upright plane through where Shift
        // went down, square to the view.
        const auto follow_lift = [&](const park::Ray& ray) {
            const glm::vec3 across = glm::normalize(glm::vec3 { ray.direction.x, 0.0f, ray.direction.z });
            const float along = glm::dot(ray.direction, across);
            if (along > 1e-4f)
            {
                const float y = ray.origin.y + ray.direction.y * glm::dot(p.lift->at - ray.origin, across) / along;
                p.site->height = std::max(p.ground, p.lift->height + static_cast<std::int32_t>(std::lround(y - p.lift->at.y)));
            }
        };
        if (p.step == RidePlacing::Step::Scenery)
        {
            // Scenery goes where the pointer meets the ground or, nearer, an object: then on top of it.
            std::optional<glm::vec3> hit;
            std::optional<float> top;
            std::optional<park::Ray> ray;
            if (!over && size.x > 0 && size.y > 0)
            {
                ray = park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
                if (scene.pick_boxes_of != scene.objects.size())
                {
                    scene.pick_boxes = park::pick_boxes(scene.objects, scene.models, scene.grid, scene.fitted);
                    scene.pick_boxes_of = scene.objects.size();
                }
                auto object = park::pick_object(*ray, scene.pick_boxes, scene.grid, [&](std::size_t i) { return scene.show_scenery && !scene.objects[i].hidden; });
                if (object)
                {
                    // Only a top pointed at takes the item; past a side, as of a tree's crown, it goes on the ground.
                    const auto box = std::ranges::find(scene.pick_boxes, object->index, &park::PickBox::index);
                    if ((ray->origin + ray->direction * object->distance).y >= box->park_high.y - kTopSlack)
                        top = box->park_high.y;
                    else
                        object.reset();
                }
                if (const auto distance = object ? std::optional(object->distance) : park::ground_hit(*ray, scene.grid))
                    hit = ray->origin + ray->direction * *distance;
            }
            const std::array squares { std::max<std::int32_t>(1, static_cast<std::int32_t>(p.item->width)), std::max<std::int32_t>(1, static_cast<std::int32_t>(p.item->length)) };
            // Benches, bins, lamps and signs go only on paths, against the side or in the middle the path offers.
            save::PlacedObject probe;
            probe.overlay = p.item->overlay;
            probe.symbol = p.item->symbol;
            const auto model = load_object_model(scene, p.models, assets, probe) ? scene.models.find(park::model_ref(probe)) : scene.models.end();
            const auto positioning = model != scene.models.end() ? static_cast<park::Positioning>(model->second.positioning) : park::Positioning::Tile;
            p.absolute = false;
            if (park::on_paths(positioning))
            {
                p.lift.reset();
                const auto spot = hit ? park::furniture_spot(scene.footpaths.paths, scene.grid, *hit, positioning, p.turns) : std::nullopt;
                if (!spot)
                {
                    p.site.reset();
                    p.problem = park::SiteProblem::OffPath;
                    p.ghost_mesh = { };
                    return;
                }
                p.site = park::RideSite { spot->tile.col, spot->tile.row, spot->turns, spot->height, { 1, 1 } };
                // On the ground it follows the land; on a deck it keeps the deck's height.
                p.ground = spot->raised ? park::ride_site({ spot->tile.col, spot->tile.row }, squares, 0, scene.grid).height : spot->height;
                p.absolute = spot->raised;
                const save::PlacedObject o = *ghost_object(p);
                p.problem = !park::on_owned_land(o, squares, scene.grid) ? park::SiteProblem::NotOwned : park::furniture_taken(scene.objects, scene.models, o, positioning) ? park::SiteProblem::Blocked : park::SiteProblem::None;
                p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price;
                show_ghost();
                return;
            }
            if (shift && p.site && p.lift)
            {
                if (ray)
                    follow_lift(*ray);
            }
            else
            {
                const glm::vec2 spot = hit ? glm::vec2 { (hit->x - scene.grid.origin_x) / scene.grid.tile_size_x, (hit->z - scene.grid.origin_z) / scene.grid.tile_size_z } : glm::vec2 { -1.0f };
                const auto col = static_cast<std::int32_t>(std::floor(spot.x));
                const auto row = static_cast<std::int32_t>(std::floor(spot.y));
                if (!hit || !scene.grid.contains(col, row))
                {
                    p.site.reset();
                    p.lift.reset();
                    p.ghost_mesh = { };
                    return;
                }
                const glm::vec2 inside { spot.x - static_cast<float>(col), spot.y - static_cast<float>(row) };
                p.site = park::ride_site({ col, row }, squares, park::snapped_turns(positioning, inside).value_or(p.turns), scene.grid);
                p.ground = p.site->height;
                // Rounded up, less a hair, so an item lands on a top that is a whole unit high.
                if (top)
                    p.site->height = std::max(p.site->height, static_cast<std::int32_t>(std::ceil(*top - 0.005f)));
                p.quadrant = (inside.x >= 0.5f ? 1 : 0) + (inside.y >= 0.5f ? 2 : 0);
                p.lift = shift ? std::optional(RidePlacing::Lift { *hit, p.site->height }) : std::nullopt;
            }
            const save::PlacedObject o = *ghost_object(p);
            p.problem = park::on_owned_land(o, squares, scene.grid) ? park::SiteProblem::None : park::SiteProblem::NotOwned;
            if (p.problem == park::SiteProblem::None && load_object_model(scene, p.models, assets, o))
            {
                // The room the item's solid squares take may not be taken already, by a solid item or a path.
                const park::Clash clash = park::clash(park::solid_tiles(std::span(&o, 1), scene.models, scene.grid), p.solids, scene.footpaths.paths, scene.grid);
                if (clash != park::Clash::None)
                    p.problem = clash == park::Clash::Path ? park::SiteProblem::Path : park::SiteProblem::Blocked;
            }
            p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price;
            show_ghost();
            return;
        }
        if (p.step == RidePlacing::Step::Ride && shift && p.site && p.lift)
        {
            // Raised, it stays on its tiles.
            if (!over && size.x > 0 && size.y > 0)
                follow_lift(park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y }));
            p.problem = park::site_problem(*p.site, scene.grid, scene.footpaths.paths, p.solids);
            if (p.problem == park::SiteProblem::None)
                p.problem = park::water_problem(*p.site, p.item->water_only, scene.footpaths.water.bodies);
            p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price;
            show_ghost();
            return;
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
            p.site = park::ride_site({ tile->col, tile->row }, squares, p.turns, scene.grid);
            p.ground = p.site->height;
            // A ride that goes only on water floats on it, the lake's floor left as it is.
            if (const auto surface = park::water_surface(scene.footpaths.water.bodies, *tile); p.item->water_only && surface)
                p.site->height = std::max(p.site->height, *surface);
            p.lift.reset();
            if (shift && size.x > 0 && size.y > 0)
            {
                const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
                if (const auto distance = park::ground_hit(ray, scene.grid))
                    p.lift = RidePlacing::Lift { ray.origin + ray.direction * *distance, p.site->height };
            }
            p.problem = park::site_problem(*p.site, scene.grid, paths, p.solids);
            if (p.problem == park::SiteProblem::None)
                p.problem = park::water_problem(*p.site, p.item->water_only, scene.footpaths.water.bodies);
            p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price;
        }
        else
        {
            if (!p.stations.empty())
            {
                p.door = park::station_door_spot(p.stations, { tile->col, tile->row });
                p.ground = park::ride_site({ p.door->col, p.door->row }, { 1, 1 }, 0, scene.grid).height;
                p.site = park::RideSite { p.door->col, p.door->row, 0, p.door->height, { 1, 1 } };
            }
            else
                p.door = park::door_spot(*p.site, { tile->col, tile->row });
            p.problem = park::door_problem(*p.site, *p.door, p.step == RidePlacing::Step::Exit || !p.stations.empty() ? p.entrance : std::nullopt, scene.grid, paths, p.solids);
            // The ride's own door of that kind standing there moves when this one is placed, so it is no block.
            const bool exit = p.step == RidePlacing::Step::Exit;
            if (p.problem == park::SiteProblem::Blocked && !p.stations.empty() && std::ranges::any_of(scene.rides.rides.at(p.ride).stations, [&](const save::Station& st) {
                    const auto& old = exit ? st.exit_spot : st.entrance_spot;
                    return old && old->col == p.door->col && old->row == p.door->row;
                }))
                p.problem = park::SiteProblem::None;
            p.affordable = true;
        }
        show_ghost();
    }

    void place_station_door(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t ride, std::size_t station_index, const save::PlacedObject& door, std::int32_t height, bool exit)
    {
        RidePlacing& p = scene.placing;
        p.ride = ride;
        p.step = exit ? RidePlacing::Step::Exit : RidePlacing::Step::Entrance;
        const std::uint64_t object = add_object(scene, door);
        save::Ride& r = scene.rides.rides.at(ride);
        if (r.stations.size() <= station_index)
            r.stations.resize(station_index + 1);
        // One placed before moves: at this station, or anywhere on a ride that has just one of that door.
        const park::TrackRules rules = ride < scene.track_rules.size() ? scene.track_rules[ride] : park::TrackRules { };
        const bool only_one = (exit ? rules.exits : rules.entrances) == park::kOneDoor;
        for (std::size_t k = 0; k < r.stations.size(); ++k)
        {
            if (k != station_index && !only_one)
                continue;
            std::uint64_t& old = exit ? r.stations[k].exit : r.stations[k].entrance;
            if (const auto at = std::ranges::find(scene.objects, old, &save::PlacedObject::id); old != 0 && at != scene.objects.end())
                at->hidden = true;
            old = 0;
            (exit ? r.stations[k].exit_spot : r.stations[k].entrance_spot).reset();
        }
        save::Station& station = r.stations[station_index];
        (exit ? station.exit : station.entrance) = object;
        (exit ? station.exit_spot : station.entrance_spot) = save::PlacementSpot { object, door.col, door.row, height, static_cast<std::uint8_t>(door.rotation) };
        update_platforms(scene, ride, assets);
        if (station.entrance != 0 && station.exit != 0 && !std::ranges::contains(p.unjoined, ride))
            p.unjoined.push_back(ride);
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
        if (p.step == RidePlacing::Step::Scenery)
        {
            // The tool stays out for the next one, as in the game.
            const std::uint64_t id = add_object(scene, *o);
            // What guests use on the paths is theirs at once.
            const park::ObjectModel& model = scene.models.at(park::model_key(*o));
            const auto category = model.category;
            const auto kind = category == std::to_underlying(park::ItemCategory::LitterBin) ? std::optional(simulation::PathItemKind::Bin) : category == std::to_underlying(park::ItemCategory::Bench) ? std::optional(simulation::PathItemKind::Bench) : category == std::to_underlying(park::ItemCategory::Sign) ? std::optional(simulation::PathItemKind::Sign) : park::is_sight(category) ? std::optional(model.photo_spot ? simulation::PathItemKind::PhotoSpot : simulation::PathItemKind::Sight) : std::nullopt;
            if (const auto wall = park::edge_wall(scene.objects.back(), scene.models, scene.grid))
                wall_changed(device, scene, assets, *wall, true);
            // Bins and signs are kept with the park; benches are found again by their kind when it loads.
            if (kind && simulation::add_path_item(scene.sim, scene.objects.back(), *kind) != entt::null)
            {
                if (*kind == simulation::PathItemKind::Bin)
                    scene.bins.push_back({ .object_ref = id, .capacity = simulation::kNewBinPieces * simulation::kSavedBinPiece });
                else if (*kind == simulation::PathItemKind::Sign)
                    scene.signs.push_back({ .object_ref = id });
            }
            animate_scenery(device, scene, scene.objects.size() - 1);
            if (scene.park.cash)
                *scene.park.cash -= p.item->price;
            float_money(scene, site_point(scene, *p.site), -p.item->price);
            logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, o->col, o->row, p.site->height);
            p.ghost_mesh = { };
            return true;
        }
        if (p.step == RidePlacing::Step::Ride)
        {
            // Raised with Shift, it stands over the ground as it is.
            if (p.site->height <= p.ground)
                park::level_ground(scene.grid, *p.site);
            if (p.item->stall)
            {
                p.ride = scene.rides.stalls.size();
                const std::uint64_t object = add_object(scene, *o);
                save::Stall& stall = scene.rides.stalls.emplace_back();
                stall.base.id = last_id(scene) + 1;
                stall.base.symbol = std::format("{}:sta", p.item->symbol);
                stall.base.overlay_path = p.item->overlay;
                stall.base.name = park::numbered_name(scene.rides.stalls, stall.base.symbol);
                // Built closed: the game sets no state when a stall goes down, so it opens from its window.
                stall.base.state = save::AttractionState::Closed;
                stall.base.placement = object;
                stall.base.spot = save::PlacementSpot { object, p.site->col, p.site->row, p.site->height, static_cast<std::uint8_t>(p.site->turns) };
                // It serves the tile it faces.
                stall.base.facing = static_cast<std::uint8_t>(p.site->turns);
                // Everything its kind sells, each named after its item.
                // shortcut: at the stall's own price for it; the game lowers a price over what guests would pay,
                // cap it so once carried items' price limits are read.
                for (const park::BuildItem::Goods& g : p.item->goods)
                    stall.items.push_back({ .kind_symbol = std::format("{}:cid", g.item), .price = g.price, .name = "%ME%" });
                if (scene.park.cash)
                    *scene.park.cash -= p.item->price;
                float_money(scene, site_point(scene, *p.site), -p.item->price);
                logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, p.site->col, p.site->row, p.site->height);
                p.unjoined_stalls.push_back(p.ride);
                // One stall a pick: the tool goes away and the stall's window opens, as in the game.
                scene.info_requested = park::AttractionRef { park::AttractionRef::Kind::Stall, p.ride };
                stop_ride_placing(scene);
                rebuild_ground(device, scene, assets);
                return true;
            }
            p.ride = scene.rides.rides.size();
            save::Ride& ride = scene.rides.rides.emplace_back();
            ride.kind = save::RideKind::Flat;
            const std::uint64_t object = add_object(scene, *o);
            ride.base.id = last_id(scene) + 1;
            ride.base.symbol = std::format("{}:anr", p.item->symbol);
            ride.base.overlay_path = p.item->overlay;
            // Its kind's name and a number, as the game names a new ride.
            ride.base.name = park::numbered_name(scene.rides.rides, ride.base.symbol);
            // Built closed, as in the game: the player tests it and opens it from its window.
            ride.base.state = save::AttractionState::Closed;
            // A show ride knows the tricks the game's main file names for its kind (each has a picture there,
            // "<trick>Skin"), and puts on the first two after its idle until its window changes
            // them.
            if (!p.trick_names && assets.install)
            {
                p.trick_names.emplace();
                const auto main = assets.install->find("Main.common.ovl");
                const auto overlay = main ? ovl::Overlay::load(*main) : std::unexpected(ovl::Error { });
                for (const ovl::Symbol& symbol : overlay ? overlay->common().symbols : std::vector<ovl::Symbol> { })
                    if (const ovl::SymbolName n = ovl::split_symbol_name(symbol.name); n.tag == "txt" && n.name.contains("Show") && n.name.ends_with("Skin"))
                        p.trick_names->emplace_back(n.name.substr(0, n.name.size() - 4));
            }
            if (const auto model = scene.models.find(park::model_ref(*o)); model != scene.models.end() && p.trick_names)
                if (auto tricks = park::show_tricks(p.item->symbol, *p.trick_names, model->second.animation_names, model->second.animation_seconds); !tricks.empty())
                {
                    save::RideShow& show = scene.shows.emplace_back(save::RideShow { .ride = ride.base.id, .tricks = std::move(tricks), .order = { } });
                    for (std::uint32_t t = 1; t < show.tricks.size() && t <= 2; ++t)
                        show.order.push_back(t);
                }
            ride.base.placement = object;
            ride.base.spot = save::PlacementSpot { object, p.site->col, p.site->row, p.site->height, static_cast<std::uint8_t>(p.site->turns) };
            ride.base.facing = static_cast<std::uint8_t>(p.site->turns);
            if (scene.park.cash)
                *scene.park.cash -= p.item->price;
            float_money(scene, site_point(scene, *p.site), -p.item->price);
            logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, p.site->col, p.site->row, p.site->height);
            p.fence.clear();
            fence_ride(scene, assets, ride);
            p.step = RidePlacing::Step::Entrance;
            // Its window opens at once, on the tab last used, while its doors are placed.
            scene.info_requested = park::AttractionRef { park::AttractionRef::Kind::Ride, p.ride };
        }
        else
        {
            // The door stands on ground levelled to the ride's height, as the ride does.
            if (p.site->height <= p.ground)
                park::level_ground(scene.grid, park::RideSite { p.door->col, p.door->row, 0, p.site->height, { 1, 1 } });
            logging::logger("park").info("placed {} at {},{}", o->symbol, p.door->col, p.door->row);
            if (!p.stations.empty())
            {
                // shortcut: doors placed by hand go to the ride's first station, wherever along the track they stand;
                // give each its own station's once rides of several stations are built here.
                place_station_door(scene, assets, p.ride, 0, *o, p.site->height, p.step == RidePlacing::Step::Exit);
                // The entrance down, the exit follows when the ride has none, as in the game.
                const auto& doors = scene.rides.rides.at(p.ride).stations;
                if (p.step == RidePlacing::Step::Exit || doors.empty() || doors.front().exit_spot || !start_door_placing(scene, p.ride, park::BuildItem(*p.item), std::vector(p.stations), true))
                    stop_ride_placing(scene);
                p.door.reset();
                p.ghost_mesh = { };
                refresh_ride_floors(scene);
                rebuild_ground(device, scene, assets);
                return true;
            }
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
                fence_ride(scene, assets, ride);
            }
            else
            {
                ride.stations.front().exit = object;
                ride.stations.front().exit_spot = spot;
                fence_ride(scene, assets, ride);
                p.unjoined.push_back(p.ride);
                stop_ride_placing(scene);
            }
        }
        p.door.reset();
        p.ghost_mesh = { };
        refresh_ride_floors(scene);
        rebuild_ground(device, scene, assets);
        return true;
    }

    void draw_ride_placing_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.placing.item && !scene.placing.ghost_mesh.vertices.empty())
            draw_ghost_mesh(device, scene, scene.placing.ghost_mesh, transform, light);
    }

} // namespace rct3::scene
