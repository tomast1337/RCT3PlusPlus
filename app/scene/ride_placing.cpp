// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/ride_placing.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "park/bone_poses.hpp"
#include "park/build_arrows.hpp"
#include "park/camera.hpp"
#include "park/edge_walls.hpp"
#include "park/enclosures.hpp"
#include "park/item_category.hpp"
#include "park/object/pick.hpp"
#include "park/object/placement.hpp"
#include "park/off_map_trees.hpp"
#include "park/path/decks.hpp"
#include "park/path/furniture.hpp"
#include "park/ride/fences.hpp"
#include "park/ride/placement.hpp"
#include "park/support/columns.hpp"
#include "park/track/ground.hpp"
#include "park/water/fill.hpp"
#include "scene/enclosure_tools.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
#include "scene/track_building.hpp"
#include "simulation/import_park.hpp"
#include "simulation/systems.hpp"
#include "ui/screens/scenery_info.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace rct3::scene
{

    std::optional<glm::vec2> ground_spot(const park::ParkScene& scene, ui::Point at, ui::Point size)
    {
        if (size.x <= 0 || size.y <= 0)
            return std::nullopt;
        const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
        const auto distance = rct3::park::object::ground_hit(ray, scene.grid);
        if (!distance)
            return std::nullopt;
        const glm::vec3 hit = ray.origin + ray.direction * *distance;
        return glm::vec2 { (hit.x - scene.grid.origin_x) / scene.grid.tile_size_x, (hit.z - scene.grid.origin_z) / scene.grid.tile_size_z };
    }

    std::uint64_t last_id(const park::ParkScene& scene)
    {
        std::uint64_t last = 0;
        for (const save::model::PlacedObject& o : scene.objects)
            last = std::max(last, o.id);
        for (const save::model::Ride& r : scene.rides.rides)
            last = std::max(last, r.base.id);
        for (const save::model::Stall& s : scene.rides.stalls)
            last = std::max(last, s.base.id);
        return last;
    }

    std::optional<save::model::Tile> ground_tile(const park::ParkScene& scene, ui::Point at, ui::Point size)
    {
        const auto spot = ground_spot(scene, at, size);
        if (!spot)
            return std::nullopt;
        const auto col = static_cast<int>(std::floor(spot->x));
        const auto row = static_cast<int>(std::floor(spot->y));
        if (!scene.grid.contains(col, row))
            return std::nullopt;
        return save::model::Tile { col, row };
    }

    std::optional<std::int32_t> lifted_height(const rct3::park::Ray& ray, glm::vec3 at, std::int32_t height)
    {
        const glm::vec3 across = glm::normalize(glm::vec3 { ray.direction.x, 0.0f, ray.direction.z });
        const float along = glm::dot(ray.direction, across);
        if (!(along > 1e-4f))
            return std::nullopt;
        const float y = ray.origin.y + ray.direction.y * glm::dot(at - ray.origin, across) / along;
        return height + static_cast<std::int32_t>(std::lround(y - at.y));
    }

    namespace
    {
        /// How far under an object's top the pointer may meet its box and still count as on the top.
        constexpr float kTopSlack = 0.05f;
        constexpr std::int32_t kClimbReach = 24; ///< How far, in height units, an item raised with Shift climbs to find room
        constexpr std::int32_t kDigReach = 64;   ///< How far, in height units, a ride lowered with Shift goes under the ground. Chosen by eye
        /// The placing panel's tabs' places on its ring; the first is left for one that finds the item in its list.
        constexpr int kTurnSlot = 1;
        constexpr int kColourSlot = 2;
        constexpr int kSmoothSlot = 3;
        constexpr int kSharpSlot = 4;

        /// The object the ghost shows: the ride, or the entrance or exit pointed at.
        std::optional<save::model::PlacedObject> ghost_object(const RidePlacing& p)
        {
            if (!p.item || !p.site)
                return std::nullopt;
            save::model::PlacedObject o;
            o.overlay = p.item->overlay;
            o.base_height = p.site->height;
            if (p.step == RidePlacing::Step::Scenery)
            {
                o.symbol = p.item->symbol;
                o.col = p.site->col;
                o.row = p.site->row;
                o.rotation = p.site->turn.value;
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
                o.rotation = p.site->turn.value;
                return o;
            }
            if (!p.door)
                return std::nullopt;
            o.symbol = p.step == RidePlacing::Step::Entrance ? p.item->entrance : p.item->exit;
            o.overlay = p.item->door_overlay;
            o.col = p.door->col;
            o.row = p.door->row;
            o.rotation = p.door->turn.value;
            return o;
        }

        /// Adds `o` to the park with a new id, in the colours its item gives, as part of the ride being placed
        /// unless it is scenery. Its supports follow with the park's (scene/park/supports.hpp).
        std::uint64_t add_object(park::ParkScene& scene, save::model::PlacedObject o)
        {
            o.id = last_id(scene) + 1;
            o.owner_ref = o.id;
            // Scenery comes in the colours its ghost shows.
            if (scene.placing.step != RidePlacing::Step::Scenery)
            {
                if (const auto model = scene.models.find(rct3::park::object::model_ref(o)); model != scene.models.end())
                    o.colours = model->second.default_colours;
                scene.attractions[o.id] = { scene.placing.item->stall ? rct3::park::AttractionRef::Kind::Stall : rct3::park::AttractionRef::Kind::Ride, scene.placing.ride };
            }
            else
                scene.placed.insert(o.id);
            scene.objects.push_back(o);
            objects_changed(scene);
            return o.id;
        }

        /// Gives `ride` its fence round it and beside the doors that stand, in place of the one it had, and paints
        /// the doors in the fence's colours.
        void fence_ride(park::ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Ride& ride)
        {
            std::vector<std::uint64_t>& fence = scene.ride_fence_pieces[ride.base.id];
            for (save::model::PlacedObject& o : scene.objects)
            {
                if (std::ranges::contains(fence, o.id))
                    o.hidden = true;
            }
            fence.clear();
            std::unordered_map<std::uint64_t, const save::model::PlacedObject*> placed;
            for (const save::model::PlacedObject& o : scene.objects)
                placed.emplace(o.id, &o);
            std::vector<save::model::PlacedObject> pieces = rct3::park::ride::ride_fences(
                ride,
                [&](std::uint64_t object) -> const rct3::park::object::ObjectModel* {
                    const auto o = placed.find(object);
                    const auto found = o != placed.end() ? scene.models.find(rct3::park::object::model_ref(*o->second)) : scene.models.end();
                    return found != scene.models.end() ? &found->second : nullptr;
                },
                last_id(scene));
            if (pieces.empty() || !load_object_model(scene, scene.placing.models, assets, pieces.front()))
                return;
            const std::array<std::int32_t, 3> colours { ride.fence_colours[0], ride.fence_colours[1], ride.fence_colours[2] };
            // The doors are painted as the fence is.
            for (save::model::PlacedObject& o : scene.objects)
            {
                if (std::ranges::any_of(ride.stations, [&](const save::model::Station& s) { return o.id == s.entrance || o.id == s.exit; }))
                    o.colours = colours;
            }
            for (save::model::PlacedObject& piece : pieces)
            {
                piece.colours = colours;
                fence.push_back(piece.id);
                scene.objects.push_back(std::move(piece));
            }
            objects_changed(scene);
        }

        /// Has the simulation run the rides placed here whose entrances now have a path behind them, and the stalls
        /// placed here that now face one.
        void join_rides(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            RidePlacing& p = scene.placing;
            std::erase_if(p.unjoined, [&](std::size_t i) {
                if (i >= scene.rides.rides.size() || scene.rides.rides[i].base.demolished)
                    return true;
                save::model::Ride& ride = scene.rides.rides[i];
                // One the park runs already has its doors moved, or a station given doors: its trains stop at every
                // station already, and its guests find the doors from the paths.
                if (i < scene.ride_entities.size() && scene.ride_entities[i] != entt::null)
                {
                    (void)simulation::relink_attractions(scene.sim, scene.rides);
                    return true;
                }
                // A tracked ride runs its train round the course it was built with. Trains made here stand at its
                // station, from where they run up to it in turn.
                const auto& course = i < scene.courses.size() ? scene.courses[i] : std::nullopt;
                // An unfinished track waits while the panel builds it: a test of it runs a ghost till then.
                if (scene.building.ride == i && course && !course->closed)
                    if (const auto runs = simulation::coaster_of(*course, ride); runs && runs->shuttle == simulation::Shuttle::None)
                        return false;
                stand_trains(scene, i);
                const std::array coaster { course ? simulation::coaster_of(*course, ride) : std::nullopt };
                save::model::RideModel one;
                one.rides.push_back(ride);
                const auto timings = simulation::flat_ride_timings(one, scene.objects, [&](const save::model::PlacedObject& o) -> const rct3::park::object::ObjectModel* {
                    const auto found = scene.models.find(rct3::park::object::model_ref(o));
                    return found != scene.models.end() ? &found->second : nullptr;
                });
                auto added = simulation::import_rides(scene.sim, one, timings, coaster);
                if (!added.empty() && added.front() == entt::null && course)
                    added.front() = simulation::import_climb_slide(scene.sim, ride, *course);
                if (added.empty() || added.front() == entt::null)
                    return false;
                scene.ride_entities.resize(scene.rides.rides.size(), entt::null);
                scene.ride_entities[i] = added.front();
                if (const auto seating = assets.install ? simulation::ride_seatings(one, [&](std::string_view path) { return assets.install->find(path); }).front() : std::nullopt)
                    scene.ride_seatings[added.front()] = static_cast<std::uint8_t>(*seating);
                if (const auto show = std::ranges::find(scene.shows, ride.base.id, &save::model::RideShow::ride); show != scene.shows.end())
                    park::stage_show(scene, *show);
                park::seat_flat_ride(device, scene, i);
                if (assets.install && ride.kind == save::model::RideKind::Tracked)
                    park::replace_ride_cars(device, scene, *assets.install, i);
                ready_boats(scene, i);
                logging::logger("park").info("{} joins the paths", ride.base.name);
                return true;
            });
            // A stall serves from the path on the tile it faces.
            std::erase_if(p.unjoined_stalls, [&](std::size_t i) {
                if (i >= scene.rides.stalls.size() || scene.rides.stalls[i].base.demolished)
                    return true;
                save::model::RideModel one;
                one.stalls.push_back(scene.rides.stalls[i]);
                return simulation::import_stalls(scene.sim, one, scene.carried) > 0;
            });
            // A facility serves from a path on any side.
            std::erase_if(p.unjoined_facilities, [&](const std::pair<std::size_t, std::uint32_t>& f) {
                if (f.first >= scene.rides.rides.size() || scene.rides.rides[f.first].base.demolished)
                    return true;
                save::model::RideModel one;
                one.rides.push_back(scene.rides.rides[f.first]);
                return simulation::import_facilities(scene.sim, one, [&](const save::model::Ride&) { return std::optional(f.second); }) > 0;
            });
        }

        /// The middle of `site` at its height, where what it cost floats up from.
        glm::vec3 site_point(const park::ParkScene& scene, const rct3::park::ride::RideSite& site)
        {
            const save::model::TerrainGrid& g = scene.grid;
            return { g.world_x(site.col) + static_cast<float>(site.span[0]) * g.tile_size_x / 2, static_cast<float>(site.height), g.world_z(site.row) + static_cast<float>(site.span[1]) * g.tile_size_z / 2 };
        }
    } // namespace

    bool start_ride_placing(park::ParkScene& scene, const rct3::park::BuildItem& item)
    {
        // An animal house is placed as scenery is, inside an enclosure.
        const bool scenery = (item.kind == rct3::park::BuildKind::Scenery || item.kind == rct3::park::BuildKind::Shelter) && !item.symbol.empty();
        // A stall or a facility is placed whole as a flat ride is, without doors.
        const bool whole = item.kind == rct3::park::BuildKind::FlatRide || item.kind == rct3::park::BuildKind::Shop;
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

    bool start_door_placing(park::ParkScene& scene, std::size_t ride, const rct3::park::BuildItem& item, std::vector<save::model::TrackPiece> stations, bool exit)
    {
        if (item.entrance.empty() || item.exit.empty() || ride >= scene.rides.rides.size())
            return false;
        save::model::Ride& r = scene.rides.rides[ride];
        if (stations.empty() && (r.kind != save::model::RideKind::Flat || !r.base.spot))
            return false;
        RidePlacing& p = scene.placing;
        p.item = item;
        p.step = exit ? RidePlacing::Step::Exit : RidePlacing::Step::Entrance;
        p.ride = ride;
        p.site.reset();
        if (stations.empty())
        {
            // A flat ride's doors go round the ground it stands on, its fence built again as each goes in.
            const save::model::PlacementSpot& spot = *r.base.spot;
            const std::array squares { static_cast<std::int32_t>(item.width), static_cast<std::int32_t>(item.length) };
            p.site = rct3::park::ride::RideSite { .col = spot.col, .row = spot.row, .turn = rct3::park::ride::spot_turn(spot), .height = spot.height, .span = spot.direction % 2 == 0 ? squares : std::array { squares[1], squares[0] } };
            p.ground = rct3::park::ride::ride_site({ spot.col + (p.site->span[0] - 1) / 2, spot.row + (p.site->span[1] - 1) / 2 }, squares, p.site->turn, scene.grid).height;
        }
        // A station standing on a base of its own (a tower's) takes its doors round the ground the base covers.
        p.base.reset();
        for (const save::model::TrackPiece& piece : stations)
        {
            const auto o = std::ranges::find(scene.objects, piece.scenery, &save::model::PlacedObject::id);
            const auto model = piece.scenery != 0 && o != scene.objects.end() ? scene.models.find(rct3::park::object::model_ref(*o)) : scene.models.end();
            if (model == scene.models.end() || model->second.squares == std::array { 1, 1 })
                continue;
            save::model::PlacedObject at;
            at.col = piece.tile_x;
            at.row = piece.tile_z;
            at.rotation = piece.direction;
            const save::model::Tile a = rct3::park::object::square_tile(at, model->second.squares, 0, 0);
            const save::model::Tile b = rct3::park::object::square_tile(at, model->second.squares, model->second.squares[0] - 1, model->second.squares[1] - 1);
            p.base = rct3::park::ride::RideSite { .col = std::min(a.col, b.col), .row = std::min(a.row, b.row), .height = piece.height, .span = { std::abs(a.col - b.col) + 1, std::abs(a.row - b.row) + 1 } };
            break;
        }
        p.door.reset();
        p.ghost_mesh = { };
        p.panel_item.clear();
        p.stations = std::move(stations);
        // The other door keeps its tile.
        p.entrance.reset();
        if (const auto& doors = scene.rides.rides[ride].stations; !doors.empty())
        {
            if (const auto& other = exit ? doors.front().entrance_spot : doors.front().exit_spot)
                p.entrance = rct3::park::ride::DoorSpot::of(*other);
        }
        return true;
    }

    void stop_ride_placing(park::ParkScene& scene)
    {
        scene.placing.item.reset();
        scene.placing.ghost_mesh = { };
        scene.placing.preview_mesh = { };
        scene.placing.preview_bones.clear();
        scene.placing.panel_item.clear();
    }

    bool ride_placing(const park::ParkScene& scene)
    {
        return scene.placing.item.has_value();
    }

    void update_ride_placing(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, bool shift, std::span<const ui::PlayCommand> commands)
    {
        RidePlacing& p = scene.placing;
        join_rides(device, scene, assets);
        // A ride that is one building borrows the round panel while it is set down (track_building.cpp).
        if (!p.item && scene.building.ride)
            return;
        auto panel = scene.desktop->root().find_as<ui::screens::InfoPanel>(park::kPlacingName);
        if (panel && panel->take_closed())
            stop_ride_placing(scene);
        const bool whole = p.step == RidePlacing::Step::Ride || p.step == RidePlacing::Step::Scenery;
        if (panel && panel->showing() && (!p.item || !whole))
            panel->hide();
        if (!p.item)
            return;
        if (whole && !park::typing(scene))
        {
            for (const ui::PlayCommand c : commands)
            {
                if (c == ui::PlayCommand::TurnItemRight)
                    p.turn = p.turn.turned(1);
                else if (c == ui::PlayCommand::TurnItemLeft)
                    p.turn = p.turn.turned(-1);
            }
        }
        if (whole)
        {
            // The item alone, at the grid's corner, for the round view; a ride as the scenery it is placed as.
            const bool scenery = p.step == RidePlacing::Step::Scenery;
            save::model::PlacedObject alone;
            alone.symbol = scenery ? p.item->symbol : p.item->scenery;
            alone.overlay = p.item->overlay;
            alone.force_absolute_height = true;
            alone.colours = p.colours;
            const auto show_alone = [&] {
                alone.colours = p.colours;
                const std::array<save::model::PlacedObject, 1> one { alone };
                p.preview_mesh = rct3::park::object::object_mesh(one, scene.models, scene.grid);
                ghost_textures(device, scene, p.preview_mesh);
                p.preview_bones.clear();
                if (const rct3::park::object::ObjectModel& model = scene.models.at(rct3::park::object::model_key(alone)); model.rig)
                {
                    std::tie(p.preview_moving, p.preview_bones) = rct3::park::boned_meshes(alone, model, scene.grid);
                    ghost_textures(device, scene, p.preview_moving.mesh);
                }
            };
            // The panel's tabs: turning, painting scenery and, under a ride or stall that stands on dry ground,
            // levelling it smoothly or sharply.
            const bool levelled = !scenery && !p.item->water_only;
            const auto placing_tabs = [&] {
                std::vector<ui::screens::InfoTab> tabs;
                // Walls and other items that turn to the pointer have nothing to turn.
                const auto positioning = static_cast<rct3::park::object::Positioning>(scene.models.at(rct3::park::object::model_key(alone)).positioning);
                if (!scenery || !rct3::park::object::snapped_turn(positioning, { }))
                {
                    ui::screens::InfoTab& turn = tabs.emplace_back();
                    turn.icon = "IconAttractionEditorRotate";
                    turn.help = assets.strings->get("ATE_TT_Rotate");
                    turn.slot = kTurnSlot;
                    turn.action = true;
                }
                for (const ui::screens::SceneryTabInfo& t : ui::screens::scenery_tabs({ .colour_slots = rct3::park::object::colour_slots(scene.models.at(rct3::park::object::model_key(alone))), .colours = p.colours }))
                {
                    // A ride takes its own colours when placed.
                    if (t.tab != ui::screens::SceneryTab::Colour || !scenery)
                        continue;
                    ui::screens::InfoTab& colour = tabs.emplace_back();
                    colour.icon = t.icon;
                    colour.help = assets.strings->get(t.help);
                    colour.slot = kColourSlot;
                    colour.paints = t.paints;
                }
                for (const auto& [flatten, name, slot] : { std::tuple { rct3::park::ride::Flatten::Smooth, "Smooth", kSmoothSlot }, std::tuple { rct3::park::ride::Flatten::Sharp, "Sharp", kSharpSlot } })
                {
                    if (!levelled)
                        break;
                    tabs.push_back(ui::screens::InfoTab { .icon = std::format("TerrainFlattener_{}{}", name, p.flatten == flatten ? "On" : "Off"), .help = std::u16string(assets.strings->get(std::format("TerrainFlattener_{}_ToolTip", name))), .slot = slot, .choices = { }, .action = true, .paints = { }, .colours = 0 });
                }
                return tabs;
            };
            if (p.panel_item != alone.symbol && load_object_model(scene, p.models, assets, alone))
            {
                // A new pick starts in the item's own colours.
                p.panel_item = alone.symbol;
                p.colours = scene.models.at(rct3::park::object::model_key(alone)).default_colours;
                show_alone();
                if (panel)
                    panel->show(placing_tabs(), placing_tabs().size());
            }
            if (panel && panel->showing() && p.panel_item == alone.symbol)
            {
                // Each press of the turn tab turns it a quarter, always the same way. Of the levelling tabs, at most
                // one is lit; pressing the lit one puts it out.
                if (const auto pressed = panel->take_action())
                {
                    const std::size_t smooth = placing_tabs().size() - (levelled ? 2 : 0);
                    if (*pressed < smooth)
                        p.turn = p.turn.turned(1);
                    else
                    {
                        const rct3::park::ride::Flatten picked = *pressed == smooth ? rct3::park::ride::Flatten::Smooth : rct3::park::ride::Flatten::Sharp;
                        p.flatten = p.flatten == picked ? rct3::park::ride::Flatten::Off : picked;
                        panel->show(placing_tabs(), placing_tabs().size());
                    }
                }
                if (const auto paint = panel->take_paint())
                {
                    const auto slots = ui::screens::scenery_paint_slots({ .colour_slots = rct3::park::object::colour_slots(scene.models.at(rct3::park::object::model_key(alone))), .colours = p.colours });
                    if (paint->pot < slots.size())
                    {
                        p.colours.at(slots[paint->pot]) = static_cast<std::int32_t>(paint->colour);
                        show_alone();
                    }
                }
                p.preview_camera = rct3::park::framed(rct3::park::object::object_corners(alone, scene.models, scene.grid, scene.fitted), kPreviewTurn * scene.clock, kPreviewPitch, kPreviewClosest);
                // It moves as it will in the park, as scenery standing still does.
                if (const rct3::park::object::ObjectModel& model = scene.models.at(rct3::park::object::model_key(alone)); model.rig && !p.preview_bones.empty())
                {
                    const auto [motion, time] = rct3::park::flat_ride_motion(model.animation_seconds, 0, 0, false, scene.clock);
                    const std::vector<glm::mat4> moves = rct3::park::bone_moves(*model.rig, motion, time);
                    const glm::mat4 placed = rct3::park::object::placed_transform(alone, &model, scene.grid, scene.fitted);
                    std::vector<glm::mat4> places(p.preview_bones.size(), placed);
                    for (std::size_t k = 0; k < places.size(); ++k)
                        if (static_cast<std::size_t>(p.preview_bones[k]) < moves.size())
                            places[k] = placed * moves[static_cast<std::size_t>(p.preview_bones[k])];
                    rct3::park::move_meshes(p.preview_moving, places);
                }
            }
        }
        // shortcut: worked out again only when objects come or go, as path building does.
        if (p.solids_of != scene.objects.size())
        {
            p.solids = rct3::park::path::solid_tiles(scene.objects, scene.models, scene.grid);
            p.holds = rct3::park::ground::ground_holds(scene.objects, scene.models, scene.footpaths.paths, scene.grid);
            p.solids_of = scene.objects.size();
        }
        const auto show_ghost = [&] {
            p.ghost_mesh = { };
            const auto o = ghost_object(p);
            if (!o || o->symbol.empty() || !load_object_model(scene, p.models, assets, *o))
                return;
            std::vector<save::model::PlacedObject> shown { *o };
            if (const rct3::park::support::SupportSets* sets = park::support_sets(scene, assets))
            {
                for (save::model::PlacedObject& piece : park::supports_under(scene, shown, *sets))
                {
                    if (load_object_model(scene, p.models, assets, piece))
                        shown.push_back(std::move(piece));
                }
            }
            p.ghost_mesh = rct3::park::object::object_mesh(shown, scene.models, scene.grid);
            const glm::vec4 tint = p.problem == rct3::park::ride::SiteProblem::None && p.affordable ? kGhostTint : kRefusedTint;
            for (rct3::park::GroundVertex& v : p.ghost_mesh.vertices)
                v.colour *= tint;
            ghost_textures(device, scene, p.ghost_mesh);
        };
        // While Shift is held the height follows the pointer up and down: scenery not below the ground, a ride also
        // down into it, to lie buried.
        const auto follow_lift = [&](const rct3::park::Ray& ray) {
            if (const auto height = lifted_height(ray, p.lift->at, p.lift->height))
                p.site->height = std::max(p.step == RidePlacing::Step::Ride ? p.ground - kDigReach : p.ground, *height);
        };
        // Lowered into the ground, the ghost must lie wholly under it, under enough ground for a cave.
        const auto buried = [&] {
            const auto o = ghost_object(p);
            return o && load_object_model(scene, p.models, assets, *o) && rct3::park::track::lies_buried(std::span { &*o, 1 }, scene.models, scene.grid);
        };
        if (p.step == RidePlacing::Step::Scenery)
        {
            // Scenery goes where the pointer meets the ground or, nearer, an object: then on top of it.
            std::optional<glm::vec3> hit;
            std::optional<float> top;
            std::optional<rct3::park::Ray> ray;
            if (!over && size.x > 0 && size.y > 0)
            {
                ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
                if (scene.pick_boxes_of != scene.objects.size())
                    place_objects(scene);
                auto object = rct3::park::object::pick_object(*ray, scene.pick_boxes, scene.grid, [&](std::size_t i) { return scene.show_scenery && !scene.objects[i].hidden; });
                if (object)
                {
                    // Only a top pointed at takes the item; past a side, as of a tree's crown, it goes on the ground.
                    const auto box = std::ranges::find(scene.pick_boxes, object->index, &rct3::park::object::PickBox::index);
                    if ((ray->origin + ray->direction * object->distance).y >= box->park_high.y - kTopSlack)
                        top = box->park_high.y;
                    else
                        object.reset();
                }
                if (const auto distance = object ? std::optional(object->distance) : rct3::park::object::ground_hit(*ray, scene.grid))
                    hit = ray->origin + ray->direction * *distance;
            }
            const std::array squares { std::max<std::int32_t>(1, static_cast<std::int32_t>(p.item->width)), std::max<std::int32_t>(1, static_cast<std::int32_t>(p.item->length)) };
            // Benches, bins, lamps and signs go only on paths, against the side or in the middle the path offers.
            save::model::PlacedObject probe;
            probe.overlay = p.item->overlay;
            probe.symbol = p.item->symbol;
            const auto model = load_object_model(scene, p.models, assets, probe) ? scene.models.find(rct3::park::object::model_ref(probe)) : scene.models.end();
            const auto positioning = model != scene.models.end() ? static_cast<rct3::park::object::Positioning>(model->second.positioning) : rct3::park::object::Positioning::Tile;
            p.absolute = false;
            if (rct3::park::path::on_paths(positioning))
            {
                p.lift.reset();
                const auto spot = hit ? rct3::park::path::furniture_spot(scene.footpaths.paths, scene.grid, *hit, positioning, p.turn, model != scene.models.end() ? model->second.path_rules : rct3::park::object::PathRules { }) : std::nullopt;
                if (!spot)
                {
                    p.site.reset();
                    p.problem = rct3::park::ride::SiteProblem::OffPath;
                    p.ghost_mesh = { };
                    return;
                }
                p.site = rct3::park::ride::RideSite { spot->tile.col, spot->tile.row, spot->turn.value, spot->height, { 1, 1 } };
                // On the ground it follows the land; on a deck it keeps the deck's height.
                p.ground = spot->raised ? rct3::park::ride::ride_site({ spot->tile.col, spot->tile.row }, squares, { }, scene.grid).height : spot->height;
                p.absolute = spot->raised;
                const save::model::PlacedObject o = *ghost_object(p);
                p.problem = !rct3::park::object::on_owned_land(o, squares, scene.grid) ? rct3::park::ride::SiteProblem::NotOwned : spot->refused || rct3::park::path::furniture_taken(scene.objects, scene.models, o, positioning) ? rct3::park::ride::SiteProblem::Blocked : rct3::park::ride::SiteProblem::None;
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
                p.site = rct3::park::ride::ride_site({ col, row }, squares, rct3::park::object::snapped_turn(positioning, inside).value_or(p.turn), scene.grid);
                p.ground = p.site->height;
                // Rounded up, less a hair, so an item lands on a top that is a whole unit high.
                if (top)
                    p.site->height = std::max(p.site->height, static_cast<std::int32_t>(std::ceil(*top - 0.005f)));
                p.quadrant = (inside.x >= 0.5f ? 1 : 0) + (inside.y >= 0.5f ? 2 : 0);
                p.lift = shift ? std::optional(RidePlacing::Lift { *hit, p.site->height }) : std::nullopt;
            }
            const auto problem = [&] {
                const save::model::PlacedObject o = *ghost_object(p);
                if (!rct3::park::object::on_owned_land(o, squares, scene.grid))
                    return rct3::park::ride::SiteProblem::NotOwned;
                if (p.item->kind == rct3::park::BuildKind::Shelter && rct3::park::enclosure_under(scene.enclosures, { p.site->col, p.site->row }, p.site->span) < 0)
                    return rct3::park::ride::SiteProblem::OffEnclosure;
                if (!load_object_model(scene, p.models, assets, o))
                    return rct3::park::ride::SiteProblem::None;
                // The room the item's solid squares take may not be taken already, by a solid item or a path.
                const rct3::park::path::Clash clash = rct3::park::path::clash(rct3::park::path::solid_tiles(std::span(&o, 1), scene.models, scene.grid), p.solids, scene.footpaths.paths, scene.grid);
                return clash == rct3::park::path::Clash::None ? rct3::park::ride::SiteProblem::None : clash == rct3::park::path::Clash::Path ? rct3::park::ride::SiteProblem::Path : rct3::park::ride::SiteProblem::Blocked;
            };
            p.problem = problem();
            // Raised with Shift into something, it climbs onto the top of what is there; with no room near, it
            // drops back to where Shift went down.
            if (shift && p.lift && (p.problem == rct3::park::ride::SiteProblem::Blocked || p.problem == rct3::park::ride::SiteProblem::Path))
            {
                const std::int32_t from = p.site->height;
                for (std::int32_t step = 1; step <= kClimbReach && (p.problem == rct3::park::ride::SiteProblem::Blocked || p.problem == rct3::park::ride::SiteProblem::Path); ++step)
                {
                    p.site->height = from + step;
                    p.problem = problem();
                }
                if (p.problem != rct3::park::ride::SiteProblem::None)
                {
                    p.site->height = p.lift->height;
                    p.problem = problem();
                }
            }
            p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price;
            show_ghost();
            return;
        }
        if (p.step == RidePlacing::Step::Ride && shift && p.site && p.lift)
        {
            // Raised, it stays on its tiles.
            if (!over && size.x > 0 && size.y > 0)
                follow_lift(rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y }));
            p.problem = rct3::park::ride::site_problem(*p.site, scene.grid, scene.footpaths.paths, p.solids);
            if (p.problem == rct3::park::ride::SiteProblem::None)
                p.problem = rct3::park::ride::water_problem(*p.site, p.item->water_only, scene.footpaths.water.bodies);
            // Raised, it stands over the ground as it is, so none may rise into it; lowered, it lies wholly under it.
            if (p.problem == rct3::park::ride::SiteProblem::None && p.site->height > p.ground)
                p.problem = rct3::park::ride::ground_problem(*p.site, scene.grid);
            else if (p.problem == rct3::park::ride::SiteProblem::None && p.site->height < p.ground && !buried())
                p.problem = rct3::park::ride::SiteProblem::IntoGround;
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
        const std::span<const save::model::PathTile> paths = scene.footpaths.paths;
        if (p.step == RidePlacing::Step::Ride)
        {
            const std::array squares { static_cast<std::int32_t>(p.item->width), static_cast<std::int32_t>(p.item->length) };
            p.site = rct3::park::ride::ride_site({ tile->col, tile->row }, squares, p.turn, scene.grid);
            // Unless the ground is levelled for it, it stands clear of the ground.
            if (p.flatten == rct3::park::ride::Flatten::Off)
                p.site->height = rct3::park::ride::clear_height(*p.site, scene.grid);
            p.ground = p.site->height;
            // A ride that goes only on water floats on it, the lake's floor left as it is.
            if (const auto surface = rct3::park::water::water_surface(scene.footpaths.water.bodies, *tile); p.item->water_only && surface)
                p.site->height = std::max(p.site->height, *surface);
            p.lift.reset();
            if (shift && size.x > 0 && size.y > 0)
            {
                const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y });
                if (const auto distance = rct3::park::object::ground_hit(ray, scene.grid))
                    p.lift = RidePlacing::Lift { ray.origin + ray.direction * *distance, p.site->height };
            }
            p.problem = rct3::park::ride::site_problem(*p.site, scene.grid, paths, p.solids);
            if (p.problem == rct3::park::ride::SiteProblem::None)
                p.problem = rct3::park::ride::water_problem(*p.site, p.item->water_only, scene.footpaths.water.bodies);
            // The ground under it is levelled, unless a tile of its footprint may not be; then, as when it is not
            // levelled, the ground may not rise over its base. A tile round it that may not be levelled stays.
            p.levelling = p.item->water_only ? rct3::park::ground::GroundCuts { } : rct3::park::ride::levelled_ground(*p.site, p.flatten, scene.grid);
            (void)rct3::park::ground::allow_cuts(p.levelling, scene.grid, p.holds, scene.footpaths.water.bodies);
            p.levelling_cost = rct3::park::ground::cuts_cost(p.levelling, scene.grid);
            if (p.problem == rct3::park::ride::SiteProblem::None && p.levelling.footprint.empty())
                p.problem = rct3::park::ride::ground_problem(*p.site, scene.grid);
            p.affordable = !scene.park.cash || *scene.park.cash >= p.item->price + p.levelling_cost;
        }
        else
        {
            if (!p.stations.empty())
            {
                p.door = p.base ? rct3::park::ride::door_spot(*p.base, { tile->col, tile->row }) : rct3::park::ride::station_door_spot(p.stations, { tile->col, tile->row });
                p.door->height = p.base ? p.base->height : p.door->height;
                p.ground = rct3::park::ride::ride_site({ p.door->col, p.door->row }, { 1, 1 }, { }, scene.grid).height;
                p.site = rct3::park::ride::RideSite { .col = p.door->col, .row = p.door->row, .height = p.door->height };
                // The other door kept at the station this one goes to keeps its tile.
                const auto& kept = scene.rides.rides.at(p.ride).stations;
                const std::size_t k = p.base ? 0 : rct3::park::ride::door_station(p.stations, kept, *p.door);
                const auto& other = k >= kept.size() ? std::nullopt : p.step == RidePlacing::Step::Exit ? kept[k].entrance_spot : kept[k].exit_spot;
                p.entrance = other ? std::optional(rct3::park::ride::DoorSpot::of(*other)) : std::nullopt;
            }
            else
                p.door = rct3::park::ride::door_spot(*p.site, { tile->col, tile->row });
            p.problem = rct3::park::ride::door_problem(*p.site, *p.door, p.entrance, scene.grid, paths, p.solids);
            // The ride's own door of that kind standing there moves when this one is placed, so it is no block.
            const bool exit = p.step == RidePlacing::Step::Exit;
            if (p.problem == rct3::park::ride::SiteProblem::Blocked && std::ranges::any_of(scene.rides.rides.at(p.ride).stations, [&](const save::model::Station& st) {
                    const auto& old = exit ? st.exit_spot : st.entrance_spot;
                    return old && old->col == p.door->col && old->row == p.door->row;
                }))
                p.problem = rct3::park::ride::SiteProblem::None;
            p.affordable = true;
        }
        show_ghost();
    }

    void take_fence(park::ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Ride& ride, const rct3::park::BuildItem& item)
    {
        ride.fence_symbol = item.fence;
        ride.fence_overlay = item.fence_overlay;
        const save::model::PlacedObject panel { .symbol = item.fence, .overlay = item.fence_overlay };
        if (item.fence.empty() || !load_object_model(scene, scene.placing.models, assets, panel))
            return;
        const auto& colours = scene.models.at(rct3::park::object::model_key(panel)).default_colours;
        for (std::size_t i = 0; i < ride.fence_colours.size(); ++i)
            ride.fence_colours[i] = static_cast<std::uint8_t>(colours[i]);
    }

    void place_station_door(park::ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t ride, std::size_t station_index, const save::model::PlacedObject& door, std::int32_t height, bool exit)
    {
        RidePlacing& p = scene.placing;
        p.ride = ride;
        p.step = exit ? RidePlacing::Step::Exit : RidePlacing::Step::Entrance;
        const std::uint64_t object = add_object(scene, door);
        save::model::Ride& r = scene.rides.rides.at(ride);
        if (r.stations.size() <= station_index)
            r.stations.resize(station_index + 1);
        // One placed before moves: at this station, or anywhere on a ride that has just one of that door.
        const rct3::park::track::TrackRules rules = ride < scene.track_rules.size() ? scene.track_rules[ride] : rct3::park::track::TrackRules { };
        const bool only_one = (exit ? rules.exits : rules.entrances) == rct3::park::track::kOneDoor;
        for (std::size_t k = 0; k < r.stations.size(); ++k)
        {
            if (k != station_index && !only_one)
                continue;
            std::uint64_t& old = exit ? r.stations[k].exit : r.stations[k].entrance;
            if (const auto at = std::ranges::find(scene.objects, old, &save::model::PlacedObject::id); old != 0 && at != scene.objects.end())
                at->hidden = true;
            old = 0;
            (exit ? r.stations[k].exit_spot : r.stations[k].entrance_spot).reset();
        }
        save::model::Station& station = r.stations[station_index];
        (exit ? station.exit : station.entrance) = object;
        (exit ? station.exit_spot : station.entrance_spot) = save::model::PlacementSpot { object, door.col, door.row, height, static_cast<std::uint8_t>(door.rotation) };
        update_platforms(scene, ride, assets);
        fence_ride(scene, assets, r);
        park::refresh_pavings(scene);
        if (station.entrance != 0 && station.exit != 0 && !std::ranges::contains(p.unjoined, ride))
            p.unjoined.push_back(ride);
    }

    bool ride_placing_click(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        RidePlacing& p = scene.placing;
        if (!p.item)
            return false;
        const auto o = ghost_object(p);
        if (!o || p.problem != rct3::park::ride::SiteProblem::None || !p.affordable || !scene.models.contains(rct3::park::object::model_key(*o)))
            return true;
        wait_for_build(scene);
        ++scene.built;
        if (p.step == RidePlacing::Step::Scenery)
        {
            // The tool stays out for the next one, as in the game.
            const std::uint64_t id = add_object(scene, *o);
            if (p.item->kind == rct3::park::BuildKind::Shelter)
                house_placed(scene, assets, scene.objects.back(), *p.item, *p.site);
            // What guests use on the paths is theirs at once.
            const rct3::park::object::ObjectModel& model = scene.models.at(rct3::park::object::model_key(*o));
            const auto category = model.category;
            const auto kind = category == std::to_underlying(rct3::park::ItemCategory::LitterBin) ? std::optional(simulation::PathItemKind::Bin) : category == std::to_underlying(rct3::park::ItemCategory::Bench) ? std::optional(simulation::PathItemKind::Bench) : category == std::to_underlying(rct3::park::ItemCategory::Sign) ? std::optional(simulation::PathItemKind::Sign) : simulation::sight_of(category, model.feature);
            if (const auto wall = rct3::park::edge_wall(scene.objects.back(), scene.models, scene.grid))
                park::wall_changed(device, scene, assets, *wall, true);
            // Bins and signs are kept with the park; benches are found again by their kind when it loads.
            if (const entt::entity item = kind ? simulation::add_path_item(scene.sim, scene.objects.back(), *kind) : entt::null; item != entt::null)
            {
                if (*kind == simulation::PathItemKind::Bench && !model.seat_places.empty())
                    scene.sim.registry.get<simulation::PathItem>(item).seats = static_cast<std::uint8_t>(std::min<std::size_t>(model.seat_places.size(), 255));
                if (*kind == simulation::PathItemKind::Bin)
                    scene.bins.push_back({ .object_ref = id, .capacity = simulation::kNewBinPieces * simulation::kSavedBinPiece });
                else if (*kind == simulation::PathItemKind::Sign)
                    scene.signs.push_back({ .object_ref = id });
            }
            simulation::add_scenery(scene.sim, scene.objects.back(), simulation::scenery_weight(category, model.feature));
            if (model.water_capacity)
                simulation::add_flower_bed(scene.sim, scene.objects.back(), *model.water_capacity, std::nullopt);
            park::animate_scenery(device, scene, scene.objects.size() - 1);
            // The woods beyond the edge grow with the park's: a tree planted near it seeds some there.
            if (scene.off_map_stream && rct3::park::is_plant(scene.objects.back(), scene.models))
            {
                std::vector<save::model::PlacedObject> trees = rct3::park::off_map_trees(scene.objects, scene.objects.size() - 1, scene.models, scene.grid, rct3::park::land_rings(scene.grid, scene.footpaths.water), *scene.off_map_stream);
                if (!trees.empty())
                {
                    logging::logger("park").info("{} more trees beyond the edge", trees.size());
                    scene.objects.insert(scene.objects.end(), std::make_move_iterator(trees.begin()), std::make_move_iterator(trees.end()));
                    objects_changed(scene);
                }
            }
            if (scene.park.cash)
                *scene.park.cash -= p.item->price;
            park::float_money(scene, site_point(scene, *p.site), -p.item->price);
            logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, o->col, o->row, p.site->height);
            p.ghost_mesh = { };
            return true;
        }
        if (p.step == RidePlacing::Step::Ride)
        {
            // Raised or lowered with Shift, it stands over the ground as it is or lies buried in it; else the ground is
            // levelled for it, paid with it, and the scenery standing there seated again. Taken away, it leaves the
            // ground levelled.
            const bool levels = p.site->height == p.ground;
            const auto price = p.item->price + (levels ? p.levelling_cost : 0);
            if (levels)
            {
                rct3::park::ground::make_cuts(scene.grid, p.levelling);
                if (rct3::park::ground::seat_followers(scene.objects, p.holds, scene.models, scene.grid, { p.site->col - 1, p.site->row - 1 }, { p.site->col + p.site->span[0], p.site->row + p.site->span[1] }))
                    objects_changed(scene);
            }
            if (p.item->stall)
            {
                p.ride = scene.rides.stalls.size();
                const std::uint64_t object = add_object(scene, *o);
                park::animate_scenery(device, scene, scene.objects.size() - 1);
                save::model::Stall& stall = scene.rides.stalls.emplace_back();
                stall.base.id = last_id(scene) + 1;
                stall.base.symbol = std::format("{}:sta", p.item->symbol);
                stall.base.overlay_path = p.item->overlay;
                stall.base.name = rct3::park::ride::numbered_name(scene.rides.stalls, stall.base.symbol);
                // Built closed: the game sets no state when a stall goes down, so it opens from its window.
                stall.base.state = save::model::AttractionState::Closed;
                stall.base.flags = save::model::AttractionCommon::kNeverOpened;
                stall.base.placement = object;
                stall.base.spot = save::model::PlacementSpot { object, p.site->col, p.site->row, p.site->height, static_cast<std::uint8_t>(p.site->turn.value) };
                // It serves the tile it faces.
                stall.base.facing = static_cast<std::uint8_t>(p.site->turn.value);
                // Everything its kind sells, each named after its item.
                // shortcut: at the stall's own price for it; the game lowers a price over what guests would pay,
                // cap it so once carried items' price limits are read.
                for (const rct3::park::BuildItem::Goods& g : p.item->goods)
                    stall.items.push_back({ .kind_symbol = std::format("{}:cid", g.item), .price = g.price, .name = "%ME%" });
                // And the extras those take, at the usual amount, on none of them yet.
                rct3::park::ride::offer_extras(stall, scene.carried.items);
                if (scene.park.cash)
                    *scene.park.cash -= price;
                park::float_money(scene, site_point(scene, *p.site), -price);
                logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, p.site->col, p.site->row, p.site->height);
                p.unjoined_stalls.push_back(p.ride);
                // One stall a pick: the tool goes away and the stall's window opens, as in the game.
                scene.info_requested = rct3::park::AttractionRef { rct3::park::AttractionRef::Kind::Stall, p.ride };
                stop_ride_placing(scene);
                park::refresh_pavings(scene);
                park::rebuild_ground(device, scene, assets);
                return true;
            }
            p.ride = scene.rides.rides.size();
            if (p.item->kind == rct3::park::BuildKind::Shop)
            {
                // A facility (a toilet, a cash machine and the like): a special attraction, opening onto every side.
                const std::uint64_t object = add_object(scene, *o);
                save::model::Ride& facility = scene.rides.rides.emplace_back();
                facility.kind = save::model::RideKind::Special;
                facility.base.id = last_id(scene) + 1;
                facility.base.symbol = std::format("{}:sat", p.item->symbol);
                facility.base.overlay_path = p.item->overlay;
                facility.base.name = rct3::park::ride::numbered_name(scene.rides.rides, facility.base.symbol);
                // Built closed, as a stall is; it opens from its window.
                facility.base.state = save::model::AttractionState::Closed;
                facility.base.flags = save::model::AttractionCommon::kNeverOpened;
                facility.base.placement = object;
                facility.base.spot = save::model::PlacementSpot { object, p.site->col, p.site->row, p.site->height, static_cast<std::uint8_t>(p.site->turn.value) };
                facility.base.facing = static_cast<std::uint8_t>(p.site->turn.value);
                if (scene.park.cash)
                    *scene.park.cash -= price;
                park::float_money(scene, site_point(scene, *p.site), -price);
                logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, p.site->col, p.site->row, p.site->height);
                p.unjoined_facilities.emplace_back(p.ride, p.item->type);
                scene.info_requested = rct3::park::AttractionRef { rct3::park::AttractionRef::Kind::Ride, p.ride };
                stop_ride_placing(scene);
                park::refresh_pavings(scene);
                park::rebuild_ground(device, scene, assets);
                return true;
            }
            save::model::Ride& ride = scene.rides.rides.emplace_back();
            ride.kind = save::model::RideKind::Flat;
            const std::uint64_t object = add_object(scene, *o);
            ride.base.id = last_id(scene) + 1;
            ride.base.symbol = std::format("{}:anr", p.item->symbol);
            ride.base.overlay_path = p.item->overlay;
            // Its kind's name and a number, as the game names a new ride.
            ride.base.name = rct3::park::ride::numbered_name(scene.rides.rides, ride.base.symbol);
            // Built closed, as in the game: the player tests it and opens it from its window.
            ride.base.state = save::model::AttractionState::Closed;
            ride.base.flags = save::model::AttractionCommon::kNeverOpened;
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
            if (const auto model = scene.models.find(rct3::park::object::model_ref(*o)); model != scene.models.end() && p.trick_names)
                if (auto tricks = rct3::park::ride::show_tricks(p.item->symbol, *p.trick_names, model->second.animation_names, model->second.animation_seconds); !tricks.empty())
                {
                    save::model::RideShow& show = scene.shows.emplace_back(save::model::RideShow { .ride = ride.base.id, .tricks = std::move(tricks), .order = { } });
                    for (std::uint32_t t = 1; t < show.tricks.size() && t <= 2; ++t)
                        show.order.push_back(t);
                }
            ride.base.placement = object;
            ride.base.spot = save::model::PlacementSpot { object, p.site->col, p.site->row, p.site->height, static_cast<std::uint8_t>(p.site->turn.value) };
            ride.base.facing = static_cast<std::uint8_t>(p.site->turn.value);
            if (scene.park.cash)
                *scene.park.cash -= price;
            park::float_money(scene, site_point(scene, *p.site), -price);
            logging::logger("park").info("placed {} at {},{} height {}", p.item->symbol, p.site->col, p.site->row, p.site->height);
            take_fence(scene, assets, ride, *p.item);
            fence_ride(scene, assets, ride);
            p.step = RidePlacing::Step::Entrance;
            // Its window opens at once, on the tab last used, while its doors are placed.
            scene.info_requested = rct3::park::AttractionRef { rct3::park::AttractionRef::Kind::Ride, p.ride };
        }
        else
        {
            // The door stands on ground levelled to the ride's height, as the ride does, unless it lies buried.
            if (p.site->height == p.ground || (p.site->height < p.ground && !rct3::park::track::lies_buried(std::span { &*o, 1 }, scene.models, scene.grid)))
                rct3::park::ride::level_ground(scene.grid, rct3::park::ride::RideSite { p.door->col, p.door->row, 0, p.site->height, { 1, 1 } });
            logging::logger("park").info("placed {} at {},{}", o->symbol, p.door->col, p.door->row);
            if (!p.stations.empty())
            {
                // Each door goes to the station it stands beside; a station on a base of its own is the only one.
                const std::size_t k = p.base ? 0 : rct3::park::ride::door_station(p.stations, scene.rides.rides.at(p.ride).stations, *p.door);
                place_station_door(scene, assets, p.ride, k, *o, p.site->height, p.step == RidePlacing::Step::Exit);
                // The entrance down, the exit follows when its station has none, as in the game.
                const auto& doors = scene.rides.rides.at(p.ride).stations;
                if (p.step == RidePlacing::Step::Exit || k >= doors.size() || doors[k].exit_spot || !start_door_placing(scene, p.ride, rct3::park::BuildItem(*p.item), std::vector(p.stations), true))
                    stop_ride_placing(scene);
                p.door.reset();
                p.ghost_mesh = { };
                park::rebuild_ground(device, scene, assets);
                return true;
            }
            const std::uint64_t object = add_object(scene, *o);
            save::model::Ride& ride = scene.rides.rides.at(p.ride);
            if (ride.stations.empty())
                ride.stations.emplace_back();
            save::model::Station& doors = ride.stations.front();
            const bool exit = p.step == RidePlacing::Step::Exit;
            // One placed before moves.
            std::uint64_t& old = exit ? doors.exit : doors.entrance;
            if (const auto at = std::ranges::find(scene.objects, old, &save::model::PlacedObject::id); old != 0 && at != scene.objects.end())
                at->hidden = true;
            old = object;
            (exit ? doors.exit_spot : doors.entrance_spot) = save::model::PlacementSpot { object, p.door->col, p.door->row, p.site->height, static_cast<std::uint8_t>(p.door->turn.value) };
            fence_ride(scene, assets, ride);
            // The entrance down, the exit follows when the ride has none, as in the game.
            if (!exit && !doors.exit_spot)
            {
                p.entrance = p.door;
                p.step = RidePlacing::Step::Exit;
            }
            else
            {
                // One the park runs already finds its moved door from the paths there (join_rides).
                if (doors.entrance_spot && doors.exit_spot && !std::ranges::contains(p.unjoined, p.ride))
                    p.unjoined.push_back(p.ride);
                stop_ride_placing(scene);
            }
        }
        p.door.reset();
        p.ghost_mesh = { };
        park::refresh_pavings(scene);
        park::rebuild_ground(device, scene, assets);
        return true;
    }

    rct3::park::TerrainMesh stall_arrow(const park::ParkScene& scene)
    {
        const RidePlacing& p = scene.placing;
        constexpr float kArrowLift = 0.04f; ///< Over the ground or path under it, so it shows
        if (!p.item || p.item->kind != rct3::park::BuildKind::Shop || !p.item->stall || p.step != RidePlacing::Step::Ride || !p.site || p.ghost_mesh.vertices.empty())
            return { };
        return rct3::park::facing_arrow(p.site->col, p.site->row, static_cast<float>(p.site->height) + kArrowLift, rct3::park::ride::served_side(p.site->turn), scene.grid);
    }

    void draw_ride_placing_ghost(render::Device& device, const park::ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        if (scene.placing.item && !scene.placing.ghost_mesh.vertices.empty())
            draw_ghost_mesh(device, scene, scene.placing.ghost_mesh, transform, light);
        park::draw_build_arrow(device, scene, stall_arrow(scene), rct3::park::ground::kStallArrowPicture, transform);
    }

} // namespace rct3::scene
