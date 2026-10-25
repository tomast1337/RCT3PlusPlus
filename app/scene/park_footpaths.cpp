// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_footpaths.hpp"

#include "config/game_options.hpp"
#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/ground_textures.hpp"
#include "park/object_pick.hpp"
#include "park/object_placement.hpp"
#include "park/path_decks.hpp"
#include "park/path_tunnels.hpp"
#include "park/queue_fences.hpp"
#include "park/queue_lines.hpp"
#include "park/relief_maps.hpp"
#include "park/support_columns.hpp"
#include "park/surrounding_land.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "simulation/import_park.hpp"
#include "ui/extra_skins.hpp"
#include "ui/money_text.hpp"
#include "ui/ui_textures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    const park::SupportSets* support_sets(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        ParkFootpaths& f = scene.footpaths;
        if (!f.support_sets && assets.install)
        {
            if (const auto found = assets.install->find(std::format("{}.common.ovl", park::kSupportOverlay)))
            {
                if (auto sets = park::load_support_sets(*found))
                    f.support_sets = std::move(*sets);
                else
                    logging::logger("park").warning("supports: {}", sets.error());
            }
        }
        return f.support_sets ? &*f.support_sets : nullptr;
    }

    std::vector<save::PlacedObject> supports_under(const ParkScene& scene, std::span<const save::PlacedObject> ghosts, const park::SupportSets& sets)
    {
        std::vector<save::PlacedObject> objects = scene.objects;
        std::uint64_t last = 0;
        for (const save::PlacedObject& o : objects)
            last = std::max(last, o.id);
        std::set<park::SupportTile> tiles;
        std::unordered_set<std::uint64_t> ids;
        for (const save::PlacedObject& ghost : ghosts)
        {
            save::PlacedObject& o = objects.emplace_back(ghost);
            o.id = ++last;
            const auto under = park::support_tiles(o, scene.models);
            tiles.insert(under.begin(), under.end());
            ids.insert(o.id);
        }
        park::BuiltSupports built = park::support_columns(objects, scene.models, scene.footpaths.paths, scene.grid, sets, last, supports_passed(scene), tiles);
        std::vector<save::PlacedObject> out;
        for (std::size_t i = 0; i < built.pieces.size(); ++i)
        {
            if (ids.contains(built.of[i].held))
                out.push_back(std::move(built.pieces[i]));
        }
        return out;
    }

    std::unordered_set<std::uint64_t> supports_passed(const ParkScene& scene)
    {
        std::unordered_set<std::uint64_t> platforms;
        for (const save::Ride& ride : scene.rides.rides)
        {
            for (const save::TrackPiece& piece : ride.track ? ride.track->pieces : std::vector<save::TrackPiece> { })
            {
                if (piece.platform)
                    platforms.insert(piece.platform->id);
            }
        }
        // A queue's railings stand on its deck: they hold up nothing and are not in the way of its columns.
        std::set<std::pair<std::string_view, std::string_view>> railings;
        for (const save::PathTile& p : scene.footpaths.paths)
        {
            if (p.queue)
                railings.emplace(p.queue->fence_symbol, p.queue->fence_overlay);
        }
        std::unordered_set<std::uint64_t> out(scene.footpaths.floor_decks.begin(), scene.footpaths.floor_decks.end());
        for (const save::PlacedObject& o : scene.objects)
        {
            if ((o.owner_ref != o.id && platforms.contains(o.owner_ref)) || (o.fixture && railings.contains({ o.symbol, o.overlay })))
                out.insert(o.id);
        }
        return out;
    }

    namespace
    {

        constexpr std::array<std::string_view, 2> kIconFiles { park::kPathIconFile, park::kQueueIconFile };
        /// Of the next piece over the paths already laid, so the two never fight for the same depth.
        constexpr float kGhostLift = 0.02f;
        /// Of the arrow over the next piece, so it shows over the piece's own picture.
        constexpr float kArrowLift = 0.04f;
        /// The arrow's picture among the scene's ground pictures.
        constexpr std::string_view kArrowKey = "PathArrow";
        constexpr std::string_view kNoMoney = "PlaceObject_NotEnoughMoney";
        constexpr std::string_view kPickStart = "Rct3pp_Paths_PickStart";
        constexpr std::string_view kNotOwned = "Rct3pp_Paths_NotOwned";
        constexpr std::string_view kOffMap = "Rct3pp_Paths_OffMap";
        constexpr std::string_view kUnderground = "Rct3pp_Paths_Underground";
        constexpr std::string_view kOccupied = "Rct3pp_Paths_Occupied";
        constexpr std::string_view kBlocked = "Rct3pp_Paths_Blocked";
        /// How fast the following camera closes on the next piece, per second, and how near counts as there.
        constexpr float kFollowRate = 6;
        constexpr float kFollowNear = 0.05f;
        constexpr std::string_view kNoRoute = "Rct3pp_Paths_NoRoute";
        /// The railing new queues get: the one most queues in the parks seen use.
        constexpr std::string_view kQueueRailing = "Van_Scaffold2";
        constexpr std::string_view kQueueRailingOverlay = "Style\\Vanilla\\style";

        std::optional<ui::FootpathPanel&> panel_of(const ParkScene& scene)
        {
            return scene.desktop->root().find_as<ui::FootpathPanel>(kFootpathPanelName);
        }

        bool chosen_queue(const ParkFootpaths& f)
        {
            return f.style && *f.style < f.styles.size() && f.styles[*f.style].queue;
        }

        ui::Currency currency_of(const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            return ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
        }

        std::u16string name_of(const ui::InterfaceAssets& assets, const park::PathStyle& style)
        {
            const std::u16string_view text = assets.strings->get(style.name_text);
            return text.empty() ? std::u16string(style.name.begin(), style.name.end()) : std::u16string(text);
        }

        /// The price of a piece of `style` on the ground.
        std::int64_t next_cost(const park::PathStyle& style)
        {
            return park::piece_cost(style.piece_cost, false, 0);
        }

        /// The price of `piece` in `style`, dearer the higher it is raised.
        std::int64_t piece_price(const park::PathStyle& style, const park::NextPiece& piece, const save::TerrainGrid& grid)
        {
            return park::piece_price(piece, grid, style.piece_cost);
        }

        /// The object drawing raised path `path` of `style`, with the piece the paths around it call for.
        save::PlacedObject deck_object(const park::PathStyle& style, const save::PathTile& path, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, std::uint64_t id)
        {
            const auto [symbol, turns] = park::deck_for(style, path, paths, grid);
            const save::PathElevation& e = *path.elevation;
            save::PlacedObject o;
            o.id = id;
            o.owner_ref = path.id;
            o.symbol = symbol;
            o.overlay = style.overlay;
            o.col = path.col;
            o.row = path.row;
            // Stands on its low end: a piece going down is entered at its top.
            o.base_height = e.quantised_height - (park::slope_falls(e.slope) ? static_cast<std::int32_t>(park::path_rise(e.slope)) : 0);
            o.rotation = turns;
            // Held at its own height: an object with no offset from the ground would be put on the ground.
            o.force_absolute_height = true;
            return o;
        }

        /// The panel's choices: path styles, then queue styles.
        std::pair<std::vector<ui::FootpathStyle>, std::vector<ui::FootpathStyle>> panel_styles(const ParkFootpaths& f, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            const ui::Currency money = currency_of(assets, options);
            std::pair<std::vector<ui::FootpathStyle>, std::vector<ui::FootpathStyle>> out;
            for (const park::PathStyle& s : f.styles)
                (s.queue ? out.second : out.first).push_back({ s.icon, name_of(assets, s), ui::money_text(money, next_cost(s)) });
            return out;
        }

        std::optional<ui::FootpathPick> pick_of(const ParkFootpaths& f)
        {
            if (!f.style || *f.style >= f.styles.size())
                return std::nullopt;
            const bool queue = f.styles[*f.style].queue;
            const auto before = std::ranges::count_if(std::span(f.styles).first(*f.style), [&](const park::PathStyle& s) { return s.queue == queue; });
            return ui::FootpathPick { queue, static_cast<std::size_t>(before) };
        }

        std::optional<std::size_t> style_of(const ParkFootpaths& f, ui::FootpathPick pick)
        {
            std::size_t seen = 0;
            for (std::size_t i = 0; i < f.styles.size(); ++i)
            {
                if (f.styles[i].queue != pick.queue)
                    continue;
                if (seen++ == pick.index)
                    return i;
            }
            return std::nullopt;
        }

        void upload_ground(render::Device& device, ParkScene& scene, park::GroundImages& images);

        /// Has the scene's ground pictures match `styles`, the path styles in the order the ground's batches
        /// number them, loading those it lacks.
        void picture_styles(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const std::vector<std::string>& styles)
        {
            ParkFootpaths& f = scene.footpaths;
            park::GroundImages images;
            for (std::size_t i = 0; i < styles.size(); ++i)
            {
                if (i < f.pictured.size() && f.pictured[i] == styles[i])
                    continue;
                const std::string file = park::path_texture_file(styles[i]);
                const auto found = assets.install ? assets.install->find(file) : std::nullopt;
                if (!found)
                {
                    logging::logger("park").warning("{}: not found", file);
                    continue;
                }
                if (const auto added = park::add_path_images(images, *found, styles[i], static_cast<std::uint8_t>(i)); !added)
                    logging::logger("park").warning("{}: {}", file, added.error());
            }
            f.pictured = styles;
            upload_ground(device, scene, images);
        }

        /// The same for the ground queues' `paintings`.
        void picture_queues(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const std::vector<park::QueuePainting>& paintings)
        {
            ParkFootpaths& f = scene.footpaths;
            park::GroundImages images;
            for (std::size_t i = 0; i < paintings.size(); ++i)
            {
                if (i < f.queues_pictured.size() && f.queues_pictured[i] == paintings[i])
                    continue;
                const std::string file = park::queue_texture_file(paintings[i].style);
                const auto found = assets.install ? assets.install->find(file) : std::nullopt;
                const auto added = found ? park::add_queue_image(images, *found, paintings[i], static_cast<std::uint8_t>(i)) : std::expected<bool, std::string>(std::unexpected("not found"));
                if (!added)
                    logging::logger("park").warning("{}: {}", file, added.error());
            }
            f.queues_pictured = paintings;
            upload_ground(device, scene, images);
        }

        void upload_ground(render::Device& device, ParkScene& scene, park::GroundImages& images)
        {
            if (images.empty())
                return;
            fit_texture_detail(scene, images);
            if (scene.relief_maps)
                park::add_relief_maps(images);
            auto uploaded = ui::upload_textures(device, images);
            if (!uploaded)
            {
                logging::logger("park").warning("path pictures: {}", uploaded.error());
                return;
            }
            for (auto& [key, texture] : *uploaded)
            {
                if (const auto old = scene.ground_textures.find(key); old != scene.ground_textures.end())
                {
                    device.destroy_texture(old->second.handle);
                    scene.ground_textures.erase(old);
                }
                scene.ground_textures.emplace(key, texture);
            }
        }

    } // namespace

    void refresh_ride_floors(ParkScene& scene)
    {
        std::unordered_map<std::uint64_t, const save::PlacedObject*> placed;
        for (const save::PlacedObject& o : scene.objects)
            placed.emplace(o.id, &o);
        scene.footpaths.floors = park::ride_floors(
            scene.rides.rides,
            [&](std::uint64_t object) -> std::optional<std::array<std::int32_t, 2>> {
                const auto o = placed.find(object);
                if (o == placed.end())
                    return std::nullopt;
                const auto model = scene.models.find(park::model_ref(*o->second));
                return model != scene.models.end() ? std::optional(model->second.squares) : std::nullopt;
            },
            scene.grid);
        scene.footpaths.floors_stale = true;
    }

    void rebuild_ground(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        ParkFootpaths& f = scene.footpaths;
        scene.supports.stale = true; // Paths or the ground changed
        // A raised door a path leads to stands on a deck like that path's: the door's tile and the path that joins
        // it, level with the door at its back. The deck's columns stand under the door.
        std::vector<std::pair<save::PathTile, std::uint64_t>> doors; // The door's tile, as path, and the path joining it
        for (const save::Ride& ride : scene.rides.rides)
        {
            for (const save::Station& station : ride.stations)
            {
                for (const auto& door : { station.entrance_spot, station.exit_spot })
                {
                    if (!door || !scene.grid.contains(door->col, door->row) || scene.grid.at(door->col, door->row).min_height() >= static_cast<float>(door->height))
                        continue;
                    const glm::vec2 back = park::turned_x(door->direction + 2);
                    const int col = door->col + static_cast<int>(std::lround(back.x));
                    const int row = door->row + static_cast<int>(std::lround(back.y));
                    // shortcut: only a flat raised path level with the door joins it; slopes and ground paths do not yet.
                    const auto joined = std::ranges::find_if(f.paths, [&](const save::PathTile& p) { return p.col == col && p.row == row && p.elevation && !park::is_ground_queue(p) && p.elevation->slope == 0 && p.elevation->quantised_height == door->height; });
                    if (joined == f.paths.end())
                        continue;
                    save::PathTile tile = *joined;
                    tile.id = 0;
                    tile.col = static_cast<std::uint8_t>(door->col);
                    tile.row = static_cast<std::uint8_t>(door->row);
                    // A queue's piece goes straight on into the door, whatever way the queue turns before it:
                    // stored headings run towards lower columns, higher columns, lower rows, higher rows.
                    if (tile.queue)
                    {
                        const std::uint8_t in = col > door->col ? 0 : col < door->col ? 1 : row > door->row ? 2 : 3;
                        tile.queue->start_direction = in;
                        tile.queue->end_direction = in;
                    }
                    doors.emplace_back(std::move(tile), joined->id);
                }
            }
        }
        const std::vector<park::GroundOpening> hollows = park::tunnel_hollows(f.paths, scene.grid);
        park::TerrainMesh mesh = park::terrain_mesh(scene.grid, f.water, hollows);
        park::add_ground(mesh, park::tunnel_lining(hollows, scene.grid));
        park::GroundPathMap paths = park::ground_path_map(f.paths, scene.grid);
        paths.walls = f.walls;
        park::pave_ride_floors(paths, f.floors);
        // shortcut: reads the path styles again while the panel has not, each time they are needed; keep them once read when that shows.
        std::vector<park::PathStyle> read;
        const auto style_named = [&](std::string_view name) -> const park::PathStyle* {
            for (const std::vector<park::PathStyle>* styles : { &f.styles, &read })
            {
                if (const auto found = std::ranges::find(*styles, name, &park::PathStyle::name); found != styles->end())
                    return &*found;
            }
            if (!read.empty() || !assets.install)
                return nullptr;
            for (const std::string_view folder : { park::kPathStyleFolder, park::kQueueStyleFolder })
            {
                if (const auto found = assets.install->find(folder))
                {
                    std::vector<park::PathStyle> more = park::folder_path_styles(*found);
                    read.insert(read.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
                }
            }
            const auto found = std::ranges::find(read, name, &park::PathStyle::name);
            return found != read.end() ? &*found : nullptr;
        };
        std::uint64_t id = 0;
        for (const save::PathTile& p : f.paths)
            id = std::max(id, p.id);
        for (const save::PlacedObject& o : scene.objects)
            id = std::max(id, o.id);
        if (!f.door_pieces.empty() || !doors.empty())
        {
            std::erase_if(scene.objects, [&](const save::PlacedObject& o) { return std::ranges::contains(f.door_pieces, o.id); });
            f.door_pieces.clear();
            for (auto& [tile, joined] : doors)
            {
                const park::PathStyle* style = style_named(tile.surface_name);
                if (!style)
                    continue;
                // The path joining the door opens towards it: the door's tile counts as path beside it.
                if (const auto path = std::ranges::find(f.paths, joined, &save::PathTile::id); path != f.paths.end())
                {
                    std::vector<save::PathTile> with = f.paths;
                    with.push_back(tile);
                    const auto [symbol, turns] = park::deck_for(*style, *path, with, scene.grid);
                    const auto shown = std::ranges::find(scene.objects, path->elevation->scenery_ref, &save::PlacedObject::id);
                    if (!symbol.empty() && shown != scene.objects.end() && (shown->symbol != symbol || shown->rotation != turns))
                    {
                        save::PlacedObject turned = *shown;
                        turned.symbol = symbol;
                        turned.rotation = turns;
                        if (load_object_model(scene, f.models, assets, turned))
                            *shown = std::move(turned);
                    }
                }
                // The door's own tile is open all round: the ride's fence beside it is its railing.
                std::vector<save::PathTile> around { tile };
                for (const auto& [dc, dr] : { std::pair { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } })
                {
                    if (!scene.grid.contains(tile.col + dc, tile.row + dr))
                        continue;
                    save::PathTile& t = around.emplace_back(tile);
                    t.col = static_cast<std::uint8_t>(tile.col + dc);
                    t.row = static_cast<std::uint8_t>(tile.row + dr);
                }
                save::PlacedObject deck = deck_object(*style, tile, around, scene.grid, ++id);
                deck.fixture = true;
                if (!load_object_model(scene, f.models, assets, deck))
                    continue;
                tile.elevation->scenery_ref = deck.id;
                f.door_pieces.push_back(deck.id);
                scene.objects.push_back(std::move(deck));
            }
            objects_changed(scene);
        }
        if (f.floors_stale)
        {
            // A raised ride's floor is a raised path of the floors' style over its footprint, its rim closed.
            std::erase_if(scene.objects, [&](const save::PlacedObject& o) { return std::ranges::contains(f.floor_decks, o.id); });
            f.floor_decks.clear();
            const park::PathStyle* style = std::ranges::any_of(f.floors, [](const park::RideFloor& floor) { return floor.raised.has_value(); }) ? style_named(park::kRideFloorStyle) : nullptr;
            for (const park::RideFloor& floor : f.floors)
            {
                if (!floor.raised || !style)
                    continue;
                std::vector<save::PathTile> tiles;
                // A ring of tiles round it as well, so its rim is open: the ride's fence is its railing.
                for (int r = floor.row0 - 1; r <= floor.row1 + 1; ++r)
                {
                    for (int c = floor.col0 - 1; c <= floor.col1 + 1; ++c)
                    {
                        if (!scene.grid.contains(c, r))
                            continue;
                        save::PathTile& t = tiles.emplace_back();
                        t.col = static_cast<std::uint8_t>(c);
                        t.row = static_cast<std::uint8_t>(r);
                        t.surface_name = style->name;
                        t.elevation = save::PathElevation { .quantised_height = *floor.raised };
                    }
                }
                for (const save::PathTile& t : tiles)
                {
                    if (t.col < floor.col0 || t.col > floor.col1 || t.row < floor.row0 || t.row > floor.row1)
                        continue;
                    save::PlacedObject deck = deck_object(*style, t, tiles, scene.grid, ++id);
                    deck.fixture = true;
                    // The scaffold's tops end at the floor's height; a hair over them keeps the two from flickering.
                    deck.height_adjust = 0.02f;
                    if (!load_object_model(scene, f.models, assets, deck))
                        break;
                    f.floor_decks.push_back(deck.id);
                    scene.objects.push_back(std::move(deck));
                }
            }
            objects_changed(scene);
        }
        f.floors_stale = false;
        park::add_ground(mesh, park::ground_path_mesh(paths, scene.grid));
        park::GroundQueues queues = park::ground_queues(f.paths, scene.grid);
        park::add_ground(mesh, std::move(queues.mesh));
        std::vector<park::WaterVertex> water = std::move(mesh.water);
        std::ranges::copy(park::pool_water(scene.grid, f.pools), std::back_inserter(water));
        park::TerrainMesh surroundings = park::surrounding_land(scene.grid, f.water);
        water.insert(water.end(), surroundings.water.begin(), surroundings.water.end());
        park::add_ground(mesh, std::move(surroundings));
        if (auto replaced = replace_park_water(device, scene.water, water); !replaced)
            logging::logger("park").warning("water: {}", replaced.error());
        picture_styles(device, scene, assets, paths.styles);
        picture_queues(device, scene, assets, queues.paintings);
        auto vertices = device.create_buffer({ render::BufferUsage::Vertex, std::as_bytes(std::span(mesh.vertices)), "ground vertices" });
        auto indices = device.create_buffer({ render::BufferUsage::Index, std::as_bytes(std::span(mesh.indices)), "ground indices" });
        if (!vertices || !indices)
        {
            logging::logger("park").warning("ground: {}", !vertices ? vertices.error() : indices.error());
            if (vertices)
                device.destroy_buffer(*vertices);
            if (indices)
                device.destroy_buffer(*indices);
            return;
        }
        device.destroy_buffer(scene.ground_vertices);
        device.destroy_buffer(scene.ground_indices);
        scene.ground_vertices = *vertices;
        scene.ground_indices = *indices;
        scene.batches = std::move(mesh.batches);
        scene.grid_lines.stale = true;
        scene.contour_lines.stale = true;
    }

    namespace
    {
        /// The stroke as it would be laid now, or the path a click on the tile pointed at would lay; nothing
        /// when there is neither or a queue is chosen.
        std::optional<park::DragPlan> stroke_plan(const ParkScene& scene)
        {
            const ParkFootpaths& f = scene.footpaths;
            // shortcut: strokes lay paths on the ground only, no queues or raised runs; upgrade with the A-to-B route.
            if (!f.style || *f.style >= f.styles.size() || f.styles[*f.style].queue)
                return std::nullopt;
            if (!f.brush.empty())
                return park::plan_drag(f.brush, scene.grid, f.paths, f.solids);
            if (!f.hover_tile)
                return std::nullopt;
            park::DragPlan plan = park::plan_drag(std::span(&*f.hover_tile, 1), scene.grid, f.paths, f.solids);
            return plan.lay.empty() ? std::nullopt : std::optional(std::move(plan));
        }

        std::int64_t stroke_cost(const ParkFootpaths& f, const park::DragPlan& plan)
        {
            return static_cast<std::int64_t>(plan.lay.size()) * next_cost(f.styles[*f.style]);
        }

        /// The route to the tile pointed at, with routing on, and whether it only goes through what is in the
        /// way: then it is the route that ignores that, shown refused so the player sees what blocks it.
        std::pair<std::optional<std::vector<park::RouteStep>>, bool> route_for(const ParkScene& scene)
        {
            const ParkFootpaths& f = scene.footpaths;
            if (!f.routing || !f.route_goal || !f.builder.start || !f.style || *f.style >= f.styles.size())
                return { };
            const auto cost = static_cast<std::int32_t>(next_cost(f.styles[*f.style]));
            if (auto route = park::find_route(f.builder, *f.route_goal, scene.grid, f.paths, cost, f.route_goal_height, f.solids))
                return { std::move(route), false };
            auto through = park::find_route(f.builder, *f.route_goal, scene.grid, f.paths, cost, f.route_goal_height);
            const bool blocked = through.has_value();
            return { std::move(through), blocked };
        }

        std::int64_t route_cost(const ParkScene& scene, const std::vector<park::RouteStep>& route)
        {
            const ParkFootpaths& f = scene.footpaths;
            return park::route_price(route, f.builder, scene.grid, f.paths, static_cast<std::int32_t>(next_cost(f.styles[*f.style])));
        }

        std::uint64_t unused_id(const ParkScene& scene)
        {
            std::uint64_t last = 0;
            for (const save::PathTile& p : scene.footpaths.paths)
                last = std::max(last, p.id);
            for (const save::PlacedObject& o : scene.objects)
                last = std::max(last, o.id);
            return last + 1;
        }

        /// The tiles of `spots` and those beside them, as a mask over the grid.
        std::vector<bool> around(const save::TerrainGrid& grid, std::span<const save::Tile> spots)
        {
            std::vector<bool> keep(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), false);
            for (const save::Tile spot : spots)
            {
                for (const save::Tile near : { spot, park::step(spot, 0), park::step(spot, 1), park::step(spot, 2), park::step(spot, 3) })
                {
                    if (grid.contains(near.col, near.row))
                        keep[static_cast<std::size_t>(near.row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(near.col)] = true;
                }
            }
            return keep;
        }

        /// The ground's paths and queues as they would be with `with`, on the tiles of `spots` and beside them:
        /// the new tiles and the neighbours whose edges they change, as the ground will draw them.
        park::TerrainMesh ground_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const save::PathTile> with, std::span<const save::Tile> spots, glm::vec4 tint)
        {
            park::GroundPathMap map = park::ground_path_map(with, scene.grid);
            map.walls = scene.footpaths.walls;
            park::pave_ride_floors(map, scene.footpaths.floors);
            picture_styles(device, scene, assets, map.styles);
            park::TerrainMesh mesh = park::ground_path_mesh(map, scene.grid);
            park::GroundQueues queues = park::ground_queues(with, scene.grid);
            picture_queues(device, scene, assets, queues.paintings);
            park::add_ground(mesh, queues.mesh);
            park::TerrainMesh ghost = park::only_tiles(mesh, scene.grid, around(scene.grid, spots));
            for (park::GroundVertex& v : ghost.vertices)
            {
                v.colour = tint;
                v.position.y += kGhostLift;
            }
            return ghost;
        }

        /// The style `path` was laid in.
        const park::PathStyle* style_named(const ParkFootpaths& f, const save::PathTile& path)
        {
            const bool queue = path.kind == save::PathKind::Queue;
            const auto found = std::ranges::find_if(f.styles, [&](const park::PathStyle& s) { return s.queue == queue && s.name == path.surface_name; });
            return found != f.styles.end() ? &*found : nullptr;
        }

        /// Pays back what laying `path` cost, floating it up from there.
        void refund(ParkScene& scene, const save::PathTile& path)
        {
            const park::PathStyle* style = style_named(scene.footpaths, path);
            const std::int64_t back = style != nullptr ? park::laid_price(path, scene.grid, style->piece_cost) : 0;
            if (scene.park.cash)
                *scene.park.cash += back;
            glm::vec3 at = tile_point(scene.grid, { path.col, path.row });
            if (path.elevation && !park::is_ground_queue(path))
                at.y = static_cast<float>(path.elevation->quantised_height);
            float_money(scene, at, back);
        }

        /// The paths the remove tab would take away, red over themselves: those a press went over and the one
        /// pointed at.
        void remove_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            std::vector<save::PathTile> ground;
            std::vector<save::Tile> spots;
            std::vector<save::PlacedObject> decks;
            for (const save::PathTile& p : f.paths)
            {
                if (!std::ranges::contains(f.remove_marked, p.id) && p.id != f.remove_hover)
                    continue;
                if (!p.elevation || park::is_ground_queue(p))
                {
                    ground.push_back(p);
                    spots.push_back({ p.col, p.row });
                }
                else if (const auto deck = std::ranges::find(scene.objects, p.elevation->scenery_ref, &save::PlacedObject::id); deck != scene.objects.end())
                    decks.push_back(*deck);
            }
            f.ghost = ground_ghost(device, scene, assets, ground, spots, kRefusedTint);
            f.raised_ghost = park::object_mesh(decks, scene.models, scene.grid);
            for (park::GroundVertex& v : f.raised_ghost.vertices)
                v.colour *= kRefusedTint;
            ghost_textures(device, scene, f.raised_ghost);
        }

        /// The route shown as it would be laid: its ground pieces over the ground, its raised ones as their decks
        /// and supports.
        void route_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            const park::PathStyle& style = f.styles[*f.style];
            const bool affordable = !scene.park.cash || *scene.park.cash >= route_cost(scene, *f.route);
            const glm::vec4 tint = affordable && !f.route_blocked ? kGhostTint : kRefusedTint;
            std::vector<save::PathTile> with = f.paths;
            park::FootpathBuilder trial = f.builder;
            std::uint64_t id = unused_id(scene);
            std::vector<save::Tile> ground;
            std::vector<std::size_t> raised;
            for (const park::RouteStep& step : *f.route)
            {
                trial.heading = step.heading;
                trial.slope = step.slope;
                if (!park::lay_path(trial, with, step.piece, style.name, id, step.piece.ground ? 0 : id + 1))
                    continue;
                if (style.queue)
                    with.back().surface_colours = std::array<std::int32_t, 3> { };
                if (step.piece.ground)
                    ground.push_back(step.piece.spot);
                else
                    raised.push_back(with.size() - 1);
                id += 2;
            }
            f.ghost = ground_ghost(device, scene, assets, with, ground, tint);
            // shortcut: ground cut away for the route and raised paths beside it that would take new pieces are
            // not shown; show them once routes are laid often enough to need it.
            std::vector<save::PlacedObject> shown;
            for (const std::size_t i : raised)
                shown.push_back(deck_object(style, with[i], with, scene.grid, with[i].elevation->scenery_ref));
            std::erase_if(shown, [&](const save::PlacedObject& o) { return !load_object_model(scene, f.models, assets, o); });
            if (const park::SupportSets* sets = support_sets(scene, assets); sets != nullptr && !shown.empty())
            {
                for (save::PlacedObject& o : supports_under(scene, shown, *sets))
                {
                    if (load_object_model(scene, f.models, assets, o))
                        shown.push_back(std::move(o));
                }
            }
            f.raised_ghost = park::object_mesh(shown, scene.models, scene.grid);
            for (park::GroundVertex& v : f.raised_ghost.vertices)
                v.colour *= tint;
            ghost_textures(device, scene, f.raised_ghost);
        }

        /// The next piece faint, as it will look once laid: its deck and supports when raised, the ground's
        /// pictures when on the ground, and the pieces beside it that it changes. Tinted when it cannot be laid.
        void build_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, bool allowed)
        {
            ParkFootpaths& f = scene.footpaths;
            // Pointing at a path's edge shows what would go on from there instead of the trail's next piece.
            const park::FootpathBuilder& b = f.hover ? *f.hover : f.builder;
            if (f.hover)
                allowed = park::lay_problem(b, scene.grid, f.paths, f.solids) == park::LayProblem::None;
            f.ghost = { };
            f.raised_ghost = { };
            f.arrow = { };
            f.under_ground = false;
            if (const auto next = park::next_piece(b, scene.grid); next && f.brush.empty())
            {
                f.arrow = park::heading_arrow(*next, b.heading, scene.grid);
                for (park::GroundVertex& v : f.arrow.vertices)
                {
                    v.colour = allowed ? glm::vec4(1.0f) : kRefusedTint;
                    v.position.y += kArrowLift;
                }
            }
            if (f.removing)
            {
                remove_ghost(device, scene, assets);
                return;
            }
            if (const auto& plan = f.plan)
            {
                const bool affordable = !scene.park.cash || *scene.park.cash >= stroke_cost(f, *plan);
                std::vector<save::PathTile> with = f.paths;
                for (const save::Tile spot : plan->lay)
                    with.push_back(park::ground_path(spot, f.styles[*f.style].name, 0));
                f.ghost = ground_ghost(device, scene, assets, with, plan->lay, affordable ? kGhostTint : kRefusedTint);
                return;
            }
            if (f.route && !f.route->empty() && f.style && *f.style < f.styles.size())
            {
                f.under_ground = std::ranges::any_of(*f.route, [](const park::RouteStep& s) { return s.piece.tunnel; });
                route_ghost(device, scene, assets);
                return;
            }
            const auto piece = park::next_piece(b, scene.grid);
            f.under_ground = piece && piece->tunnel;
            if (!piece || !f.style || *f.style >= f.styles.size())
                return;
            const glm::vec4 tint = allowed ? kGhostTint : kRefusedTint;
            const park::PathStyle& style = f.styles[*f.style];
            std::vector<save::PathTile> with = f.paths;
            park::FootpathBuilder trial = b;
            const std::uint64_t id = unused_id(scene);
            (void)park::lay_path(trial, with, *piece, style.name, id, piece->ground ? 0 : id + 1);
            if (with.size() == f.paths.size())
                return; // The same piece is already there
            if (style.queue)
                with.back().surface_colours = std::array<std::int32_t, 3> { };
            if (piece->ground)
            {
                f.ghost = ground_ghost(device, scene, assets, with, std::span(&piece->spot, 1), tint);
                return;
            }

            // The new deck, the decks beside it that take another piece, and the supports under the new one.
            std::vector<save::PlacedObject> shown { deck_object(style, with.back(), with, scene.grid, id + 1) };
            for (const save::PathTile& p : std::span(with).first(with.size() - 1))
            {
                if (!p.elevation || park::is_ground_queue(p) || std::abs(p.col - piece->spot.col) > 1 || std::abs(p.row - piece->spot.row) > 1)
                    continue;
                const park::PathStyle* own = style_named(f, p);
                const auto now = std::ranges::find(scene.objects, p.elevation->scenery_ref, &save::PlacedObject::id);
                if (own == nullptr || now == scene.objects.end() || now->hidden)
                    continue;
                save::PlacedObject next = deck_object(*own, p, with, scene.grid, 0);
                if (next.symbol != now->symbol || next.rotation != now->rotation)
                    shown.push_back(std::move(next));
            }
            std::erase_if(shown, [&](const save::PlacedObject& o) { return !load_object_model(scene, f.models, assets, o); });
            if (shown.empty() || shown.front().owner_ref != id)
                return;
            if (const park::SupportSets* sets = support_sets(scene, assets))
            {
                for (save::PlacedObject& o : supports_under(scene, std::span(&shown.front(), 1), *sets))
                {
                    if (load_object_model(scene, f.models, assets, o))
                        shown.push_back(std::move(o));
                }
            }
            f.raised_ghost = park::object_mesh(shown, scene.models, scene.grid);
            for (park::GroundVertex& v : f.raised_ghost.vertices)
                v.colour *= tint;
            ghost_textures(device, scene, f.raised_ghost);
        }

        /// Gives the raised paths around `spot` the pieces their neighbours call for, loading new models.
        /// Gives the raised pieces on the tiles from `low` to `high` and those beside them the pieces they call for.
        void refresh_area(ParkScene& scene, const ui::InterfaceAssets& assets, save::Tile low, save::Tile high)
        {
            ParkFootpaths& f = scene.footpaths;
            if (park::refresh_decks(f.paths, scene.objects, scene.grid, f.styles, low.col - 1, low.row - 1, high.col + 1, high.row + 1, f.walls) == 0)
                return;
            for (const save::PlacedObject& o : scene.objects)
            {
                if (o.col >= low.col - 1 && o.col <= high.col + 1 && o.row >= low.row - 1 && o.row <= high.row + 1 && !o.hidden && !o.overlay.empty())
                    (void)load_object_model(scene, f.models, assets, o);
            }
        }

        void refresh_around(ParkScene& scene, const ui::InterfaceAssets& assets, save::Tile spot)
        {
            refresh_area(scene, assets, spot, spot);
        }

        /// Hides `objects` of the scene's objects.
        void hide(ParkScene& scene, std::span<const std::uint64_t> objects)
        {
            for (save::PlacedObject& o : scene.objects)
            {
                if (std::ranges::contains(objects, o.id))
                    o.hidden = true;
            }
        }

        /// Builds the railings along the queue tiles `tiles` (by id) again.
        void rail(ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::uint64_t> tiles)
        {
            ParkFootpaths& f = scene.footpaths;
            for (const std::uint64_t id : tiles)
            {
                const auto tile = std::ranges::find(f.paths, id, &save::PathTile::id);
                if (tile == f.paths.end() || !tile->queue)
                    continue;
                std::vector<std::uint64_t>& railings = f.railings[id];
                hide(scene, railings);
                railings.clear();
                park::QueueFences fences = park::queue_fences(std::span(&*tile, 1), scene.grid, unused_id(scene));
                for (save::PlacedObject& o : fences.pieces)
                {
                    if (!load_object_model(scene, f.models, assets, o))
                        continue;
                    if (const auto lean = fences.leans.find(o.id); lean != fences.leans.end())
                        scene.leans[o.id] = lean->second;
                    railings.push_back(o.id);
                    scene.objects.push_back(std::move(o));
                }
            }
        }

        /// Tells the simulation about paths laid (or changed) and taken away, so guests use them at once and
        /// rides find their way in.
        void sim_paths(ParkScene& scene, std::span<const save::PathTile> laid, std::span<const save::PathTile> gone)
        {
            for (const save::PathTile& p : gone)
                simulation::remove_path(scene.sim, p, scene.grid);
            for (const save::PathTile& p : laid)
                (void)simulation::add_path(scene.sim, p, scene.grid);
            (void)simulation::relink_attractions(scene.sim, scene.rides);
        }

        /// Lays the paths of the stroke just ended.
        void lay_stroke(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            const auto plan = stroke_plan(scene);
            const std::vector<save::Tile> brush = std::exchange(f.brush, { });
            if (!plan || plan->lay.empty())
            {
                // A click where no path can lie picks where the trail starts instead, as raised paths may.
                if (brush.size() == 1)
                    park::start_at(f.builder, brush.front());
                return;
            }
            const std::int64_t cost = stroke_cost(f, *plan);
            if (scene.park.cash && *scene.park.cash < cost)
            {
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            wait_for_build(scene);
            std::uint64_t id = unused_id(scene);
            f.stroke.clear();
            for (const save::Tile spot : plan->lay)
            {
                f.paths.push_back(park::ground_path(spot, f.styles[*f.style].name, id));
                f.stroke.push_back(id++);
            }
            sim_paths(scene, std::span(f.paths).last(plan->lay.size()), { });
            if (scene.park.cash)
                *scene.park.cash -= cost;
            float_money(scene, tile_point(scene.grid, plan->lay.back()), -cost);
            // The trail goes on from the last tile, the way the stroke last went.
            const save::Tile last = brush.back();
            if (brush.size() > 1)
            {
                const save::Tile before = brush[brush.size() - 2];
                f.builder.heading = last.row > before.row ? 0 : last.col > before.col ? 1 : last.row < before.row ? 2 : 3;
            }
            const auto there = park::ground_path_at(f.paths, last);
            if (!there || !park::start_from_edge(f.builder, f.paths[*there], scene.grid, f.builder.heading))
                park::start_at(f.builder, last);
            save::Tile low = last;
            save::Tile high = last;
            for (const save::Tile spot : brush)
            {
                low = { std::min(low.col, spot.col), std::min(low.row, spot.row) };
                high = { std::max(high.col, spot.col), std::max(high.row, spot.row) };
            }
            refresh_area(scene, assets, low, high);
            objects_changed(scene);
            rebuild_ground(device, scene, assets);
        }

        /// Takes the paths of the last stroke away.
        void remove_stroke(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            wait_for_build(scene);
            const std::vector<std::uint64_t> gone = std::exchange(f.stroke, { });
            save::Tile low { scene.grid.width, scene.grid.height };
            save::Tile high { -1, -1 };
            std::vector<save::PathTile> taken;
            std::erase_if(f.paths, [&](const save::PathTile& p) {
                if (!std::ranges::contains(gone, p.id))
                    return false;
                taken.push_back(p);
                low = { std::min<int>(low.col, p.col), std::min<int>(low.row, p.row) };
                high = { std::max<int>(high.col, p.col), std::max<int>(high.row, p.row) };
                return true;
            });
            if (high.col < 0)
                return;
            for (const save::PathTile& p : taken)
                refund(scene, p);
            sim_paths(scene, { }, taken);
            refresh_area(scene, assets, low, high);
            objects_changed(scene);
            rebuild_ground(device, scene, assets);
        }

        /// Lays the next piece; `refresh` false leaves building the ground and the objects again to the caller.
        void lay(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, bool refresh = true)
        {
            ParkFootpaths& f = scene.footpaths;
            if (park::lay_problem(f.builder, scene.grid, f.paths, f.solids) != park::LayProblem::None)
                return;
            const park::PathStyle& style = f.styles[*f.style];
            const park::NextPiece piece = *park::next_piece(f.builder, scene.grid);
            const bool there = park::same_piece_at(f.paths, piece, f.builder).has_value();
            const std::int64_t cost = piece_price(style, piece, scene.grid);
            if (!there && scene.park.cash && *scene.park.cash < cost)
            {
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            const std::uint64_t id = unused_id(scene);
            wait_for_build(scene);
            if (!there && piece.cut)
                park::cut_down(scene.grid, piece, f.builder.heading);
            if (!park::lay_path(f.builder, f.paths, piece, style.name, id, piece.ground ? 0 : id + 1))
                return;
            f.stroke.clear();
            f.route_laid = 0;
            f.last_height = piece.end;
            if (scene.park.cash)
                *scene.park.cash -= cost;
            const glm::vec3 ground = tile_point(scene.grid, piece.spot);
            float_money(scene, { ground.x, static_cast<float>(std::max(piece.start, piece.end)), ground.z }, -cost);
            // The queue tile laid before may now open towards this one.
            std::vector<std::uint64_t> railed { f.paths.back().id };
            if (f.builder.trail.size() > 1 && f.builder.trail[f.builder.trail.size() - 2].laid != 0)
                railed.push_back(f.builder.trail[f.builder.trail.size() - 2].laid);
            if (!piece.ground)
            {
                const save::PathTile& laid = f.paths.back();
                save::PlacedObject deck = deck_object(style, laid, f.paths, scene.grid, id + 1);
                if (load_object_model(scene, f.models, assets, deck))
                {
                    scene.objects.push_back(std::move(deck));
                }
                else
                    logging::logger("park").warning("{}: no model for {}", style.overlay, deck.symbol);
            }
            if (f.builder.queue)
            {
                save::PathTile& laid = f.paths.back();
                laid.surface_colours = std::array<std::int32_t, 3> { };
                // A line's railing is its ride's fence; a queue of no ride yet has the plain one.
                if (laid.queue->fence_symbol.empty())
                {
                    laid.queue->fence_symbol = kQueueRailing;
                    laid.queue->fence_overlay = kQueueRailingOverlay;
                }
                const std::int32_t height = laid.elevation ? laid.elevation->quantised_height : piece.start;
                std::optional<std::uint64_t> line;
                if (const auto entrance = park::entrance_beside(scene.rides, piece.spot, height))
                {
                    line = park::join_entrance(scene.rides, f.paths, scene.grid, f.paths.size() - 1, *entrance, unused_id(scene));
                    logging::logger("park").info("queue joins ride {} on line {}", entrance->ride, *line);
                }
                else
                    line = park::join_line(scene.rides, f.paths, scene.grid, f.paths.size() - 1);
                for (const save::PathTile& p : f.paths)
                {
                    if (line && p.queue && p.queue->queue_line_ref == *line && !std::ranges::contains(railed, p.id))
                        railed.push_back(p.id);
                }
            }
            // A queue's open end bends to a path beside it: the queue just laid, or those beside the path just laid.
            bool turned = false;
            const save::PathTile laid = f.paths.back();
            for (std::size_t i = 0; i < f.paths.size(); ++i)
            {
                const bool candidate = f.builder.queue ? i + 1 == f.paths.size() : f.paths[i].queue && std::abs(f.paths[i].col - laid.col) + std::abs(f.paths[i].row - laid.row) == 1;
                if (candidate && park::turn_to_path(f.paths, scene.grid, i))
                {
                    turned = true;
                    if (!std::ranges::contains(railed, f.paths[i].id))
                        railed.push_back(f.paths[i].id);
                }
            }
            if (f.builder.queue || turned)
                rail(scene, assets, railed);
            // Raised queue tiles whose line changed take new pieces too.
            for (const std::uint64_t changed : railed)
            {
                if (const auto at = std::ranges::find(f.paths, changed, &save::PathTile::id); at != f.paths.end())
                    refresh_around(scene, assets, { at->col, at->row });
            }
            std::vector<save::PathTile> changed;
            std::ranges::copy_if(f.paths, std::back_inserter(changed), [&](const save::PathTile& p) { return std::ranges::contains(railed, p.id); });
            sim_paths(scene, changed, { });
            if (!refresh)
                return;
            objects_changed(scene);
            // A ground queue's picture follows its open sides, which laying the next tile changes.
            if (piece.ground || piece.cut || piece.tunnel || f.builder.queue)
                rebuild_ground(device, scene, assets);
        }

        /// Takes away what draws and holds up `removed`, just taken out of the paths.
        void take_away(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const save::PathTile& removed)
        {
            ParkFootpaths& f = scene.footpaths;
            refund(scene, removed);
            wait_for_build(scene);
            std::vector<std::uint64_t> gone = std::move(f.railings[removed.id]);
            f.railings.erase(removed.id);
            if (removed.elevation)
                gone.push_back(removed.elevation->scenery_ref);
            hide(scene, gone);
            sim_paths(scene, { }, std::span(&removed, 1));
            if (removed.queue && removed.queue->queue_line_ref != 0)
            {
                for (save::Ride& ride : scene.rides.rides)
                {
                    for (save::Station& station : ride.stations)
                    {
                        if (station.queue_line == removed.queue->queue_line_ref)
                            station.queue_tiles = std::max(station.queue_tiles - 1, 0);
                    }
                }
            }
            // A raised piece beside may lose a joined side.
            refresh_around(scene, assets, { removed.col, removed.row });
            objects_changed(scene);
            if (!removed.elevation || park::is_ground_queue(removed) || removed.elevation->underground)
                rebuild_ground(device, scene, assets);
        }

        /// Takes the last piece of the trail away, with what draws and holds it up.
        void remove(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            if (const auto removed = park::remove_last(scene.footpaths.builder, scene.footpaths.paths))
                take_away(device, scene, assets, *removed);
        }

        /// Takes paths `ids` away, the trail backing up over its own (park::remove_paths).
        void take_paths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::uint64_t> ids)
        {
            ParkFootpaths& f = scene.footpaths;
            // Indices into the paths may point at what is gone, and the last stroke or route may be part of it.
            f.hover.reset();
            f.hover_edge.reset();
            f.stroke.clear();
            f.route_laid = 0;
            // shortcut: builds the ground again for each path taken; once per drag if long drags are slow.
            for (const save::PathTile& gone : park::remove_paths(f.builder, f.paths, ids))
                take_away(device, scene, assets, gone);
        }

        std::u16string problem_text(const ui::InterfaceAssets& assets, park::LayProblem problem)
        {
            switch (problem)
            {
                case park::LayProblem::NoStart:
                    return std::u16string(assets.strings->get(kPickStart));
                case park::LayProblem::OffMap:
                    return std::u16string(assets.strings->get(kOffMap));
                case park::LayProblem::NotOwned:
                    return std::u16string(assets.strings->get(kNotOwned));
                case park::LayProblem::Underground:
                    return std::u16string(assets.strings->get(kUnderground));
                case park::LayProblem::Occupied:
                    return std::u16string(assets.strings->get(kOccupied));
                case park::LayProblem::Blocked:
                    return std::u16string(assets.strings->get(kBlocked));
                case park::LayProblem::None:
                    break;
            }
            return { };
        }

        /// Lays the route shown, piece by piece as the controls would, so it can be taken away the same way.
        void lay_route(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            const auto route = std::exchange(f.route, std::nullopt);
            if (!route || route->empty())
                return;
            if (f.route_blocked)
            {
                f.notice = std::u16string(assets.strings->get(kBlocked));
                return;
            }
            if (scene.park.cash && *scene.park.cash < route_cost(scene, *route))
            {
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            const park::PathSlope slope = f.builder.slope;
            const std::size_t before = f.builder.trail.size();
            for (const park::RouteStep& step : *route)
            {
                f.builder.heading = step.heading;
                f.builder.slope = step.slope;
                const std::size_t had = f.builder.trail.size();
                lay(device, scene, assets, false);
                if (f.builder.trail.size() == had)
                    break; // Refused part way; what was laid stays
            }
            f.builder.slope = slope;
            f.route_laid = f.builder.trail.size() - before;
            objects_changed(scene);
            rebuild_ground(device, scene, assets);
        }

    } // namespace

    void wall_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const park::EdgeWall& wall, bool standing)
    {
        scene.footpaths.walls = park::edge_walls(scene.objects, scene.models, scene.grid);
        simulation::set_wall(scene.sim, wall, standing);
        refresh_around(scene, assets, wall.tile);
        objects_changed(scene);
        rebuild_ground(device, scene, assets);
    }

    void toggle_footpath_panel(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options)
    {
        auto panel = panel_of(scene);
        if (!panel)
            return;
        if (panel->showing())
        {
            close_footpath_panel(scene);
            return;
        }
        ParkFootpaths& f = scene.footpaths;
        if (!f.loaded)
        {
            f.loaded = true;
            const std::size_t skins = ui::add_extra_skins(device, assets, kIconFiles);
            if (assets.install)
            {
                for (const std::string_view folder : { park::kPathStyleFolder, park::kQueueStyleFolder })
                {
                    if (const auto found = assets.install->find(folder))
                    {
                        std::vector<park::PathStyle> more = park::folder_path_styles(*found);
                        f.styles.insert(f.styles.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
                    }
                }
            }
            logging::logger("park").info("{} path and queue styles, {} pictures", f.styles.size(), skins);
            park::GroundImages arrow;
            const auto tool = assets.install ? assets.install->find(park::kPathToolFile) : std::nullopt;
            const auto added = tool ? park::add_named_image(arrow, *tool, park::kPathArrowPicture, kArrowKey) : std::expected<bool, std::string>(false);
            if (!added || !*added)
                logging::logger("park").warning("{}: no {}", park::kPathToolFile, park::kPathArrowPicture);
            upload_ground(device, scene, arrow);
            const auto first = std::ranges::find_if(f.styles, [](const park::PathStyle& s) { return !s.queue; });
            if (first != f.styles.end())
                f.style = static_cast<std::size_t>(first - f.styles.begin());
        }
        auto [paths, queues] = panel_styles(f, assets, options);
        panel->show(std::move(paths), std::move(queues));
        panel->set_chosen(pick_of(f));
        panel->set_following(options.flag(config::GameOption::PathCameraFollow));
        f.dirty = true;
    }

    void close_footpath_panel(ParkScene& scene)
    {
        if (auto panel = panel_of(scene); panel && panel->showing())
            panel->hide();
        scene.footpaths.ghost = { };
        scene.footpaths.raised_ghost = { };
        scene.footpaths.under_ground = false;
        scene.footpaths.brush.clear();
        scene.footpaths.hover.reset();
        scene.footpaths.hover_edge.reset();
        scene.footpaths.hover_tile.reset();
        scene.footpaths.route_goal.reset();
        scene.footpaths.route_goal_height.reset();
        scene.footpaths.camera_goal.reset();
        scene.footpaths.route.reset();
        scene.footpaths.route_press = false;
        scene.footpaths.slope_press.reset();
        scene.footpaths.arrow = { };
        scene.footpaths.plan.reset();
        scene.footpaths.drop = false;
    }

    void update_footpaths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const config::GameOptions& options)
    {
        auto panel = panel_of(scene);
        ParkFootpaths& f = scene.footpaths;
        if (!panel || !panel->showing())
        {
            if (panel && panel->take_closed())
                close_footpath_panel(scene);
            return;
        }
        if (const auto pick = panel->take_style())
        {
            f.style = style_of(f, *pick);
            f.notice.clear();
            f.dirty = true;
        }
        f.builder.queue = chosen_queue(f);
        if (std::exchange(f.lay_next, false))
        {
            // Shift held on, the next piece is picked up afresh.
            f.lift.reset();
            f.notice.clear();
            lay(device, scene, assets);
            f.dirty = true;
        }
        f.took_click = false;
        if (std::exchange(f.drop, false))
        {
            f.notice.clear();
            lay_stroke(device, scene, assets);
            f.dirty = true;
        }
        if (panel->routing() != f.routing)
        {
            f.routing = panel->routing();
            f.route_goal.reset();
            f.route_goal_height.reset();
            f.dirty = true;
        }
        if (std::exchange(f.lay_route, false))
        {
            f.notice.clear();
            lay_route(device, scene, assets);
            f.route_goal.reset();
            f.route_goal_height.reset();
            f.dirty = true;
        }
        if (panel->following() != options.flag(config::GameOption::PathCameraFollow))
            f.follow_changed = panel->following();
        if (panel->removing() != f.removing)
        {
            f.removing = panel->removing();
            f.remove_hover.reset();
            f.remove_marked.clear();
            f.hover.reset();
            f.hover_edge.reset();
            f.hover_door.reset();
            f.hover_tile.reset();
            f.dirty = true;
        }
        if (const auto id = std::exchange(f.sell, std::nullopt))
        {
            f.notice.clear();
            take_paths(device, scene, assets, std::span(&*id, 1));
            f.dirty = true;
        }
        if (std::exchange(f.remove_now, false))
        {
            f.notice.clear();
            take_paths(device, scene, assets, std::exchange(f.remove_marked, { }));
            f.remove_hover.reset();
            f.dirty = true;
        }
        if (const auto control = panel->take_control(); control && *control < park::kPathControlCount)
        {
            f.notice.clear();
            const park::PathStep step = park::press(f.builder, static_cast<park::PathControl>(*control));
            if (step == park::PathStep::Lay)
                lay(device, scene, assets);
            else if (step == park::PathStep::Remove && f.route_laid > 0)
            {
                for (std::size_t n = std::exchange(f.route_laid, 0); n > 0; --n)
                    remove(device, scene, assets);
            }
            else if (step == park::PathStep::Remove && !f.stroke.empty())
                remove_stroke(device, scene, assets);
            else if (step == park::PathStep::Remove)
                remove(device, scene, assets);
            f.dirty = true;
        }

        // shortcut: worked out again only when objects come or go, not when one is moved or hidden; also on
        // those once objects can move while the path panel is open.
        if (f.solids_of != scene.objects.size())
        {
            f.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
            f.solids_of = scene.objects.size();
        }
        const park::LayProblem problem = park::lay_problem(f.builder, scene.grid, f.paths, f.solids);
        if (const auto next = park::next_piece(f.builder, scene.grid); panel->following() && next && next->spot != f.followed)
        {
            f.followed = next->spot;
            f.camera_goal = glm::vec3 { scene.grid.world_x(next->spot.col) + scene.grid.tile_size_x / 2, static_cast<float>(next->start), scene.grid.world_z(next->spot.row) + scene.grid.tile_size_z / 2 };
        }
        else if (!panel->following())
            f.camera_goal.reset();
        // Once a trail is started only the controls that lead to a piece that fits; before, any slope may be
        // picked for the first piece.
        std::array<bool, ui::kFootpathControls> usable { };
        if (f.builder.start)
            usable = park::fitting_controls(f.builder, scene.grid, f.paths, f.solids);
        else
        {
            usable.fill(true);
            usable.at(static_cast<std::size_t>(park::PathControl::TurnLeft)) = park::can_turn(f.builder);
            usable.at(static_cast<std::size_t>(park::PathControl::TurnRight)) = park::can_turn(f.builder);
            usable.at(static_cast<std::size_t>(park::PathControl::Straight)) = problem == park::LayProblem::None;
        }
        usable.at(static_cast<std::size_t>(park::PathControl::Remove)) = !f.builder.trail.empty() || !f.stroke.empty() || f.route_laid > 0;
        panel->set_controls(usable, static_cast<std::size_t>(park::slope_control(f.builder.slope)), f.builder.cut_into_ground);

        std::u16string status = f.notice;
        if (status.empty())
            status = problem_text(assets, problem);
        if (f.dirty)
        {
            f.plan = stroke_plan(scene);
            std::tie(f.route, f.route_blocked) = route_for(scene);
        }
        if (const auto& plan = f.plan)
            status = name_of(assets, f.styles[*f.style]) + u"  " + ui::money_text(currency_of(assets, options), stroke_cost(f, *plan));
        else if (f.route && f.route_blocked)
            status = std::u16string(assets.strings->get(kBlocked));
        else if (f.route)
            status = name_of(assets, f.styles[*f.style]) + u"  " + ui::money_text(currency_of(assets, options), route_cost(scene, *f.route));
        else if (f.route_goal && f.routing && f.notice.empty())
            status = std::u16string(assets.strings->get(kNoRoute));
        else if (status.empty() && f.style && *f.style < f.styles.size())
        {
            const auto piece = park::next_piece(f.builder, scene.grid);
            const std::int64_t cost = piece ? piece_price(f.styles[*f.style], *piece, scene.grid) : next_cost(f.styles[*f.style]);
            status = name_of(assets, f.styles[*f.style]) + u"  " + ui::money_text(currency_of(assets, options), cost);
        }
        panel->set_status(std::move(status));

        if (f.dirty)
        {
            build_ghost(device, scene, assets, problem == park::LayProblem::None);
            f.dirty = false;
        }
    }

    namespace
    {
        /// The view's ray through `at` while the path panel shows its controls.
        std::optional<park::Ray> controls_ray(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            auto panel = panel_of(scene);
            if (!panel || !panel->showing() || panel->page() != ui::FootpathPage::Controls || size.x <= 0 || size.y <= 0)
                return std::nullopt;
            const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
            return park::view_ray(scene.camera, size.x / size.y, ndc);
        }

        /// The tile of the ground under `at` while the path panel shows its controls.
        std::optional<save::Tile> controls_tile(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            const auto ray_at = controls_ray(scene, at, size);
            if (!ray_at)
                return std::nullopt;
            const park::Ray& ray = *ray_at;
            const auto distance = park::ground_hit(ray, scene.grid);
            if (!distance)
                return std::nullopt;
            const glm::vec3 hit = ray.origin + ray.direction * *distance;
            const auto col = static_cast<int>(std::floor((hit.x - scene.grid.origin_x) / scene.grid.tile_size_x));
            const auto row = static_cast<int>(std::floor((hit.z - scene.grid.origin_z) / scene.grid.tile_size_z));
            if (!scene.grid.contains(col, row))
                return std::nullopt;
            return save::Tile { col, row };
        }

        /// Begins a trail at `tile`: on its ground, or with `control` at the last piece's height.
        void begin_trail(ParkFootpaths& f, const save::RideModel& rides, save::Tile tile, bool control)
        {
            park::start_at(f.builder, tile);
            if (control)
                f.builder.start_height = f.last_height;
            // A queue begun beside a ride's entrance goes away from it, so it leads guests there.
            if (chosen_queue(f))
            {
                if (const auto entrance = park::entrance_beside(rides, { tile.col, tile.row }, std::nullopt))
                    f.builder.heading = static_cast<std::uint8_t>((entrance->side + 2) % 4);
            }
            f.notice.clear();
            f.dirty = true;
        }

        /// The rides' entrances and exits.
        std::vector<save::PlacementSpot> ride_doors(const save::RideModel& rides)
        {
            std::vector<save::PlacementSpot> doors;
            for (const save::Ride& ride : rides.rides)
            {
                for (const save::Station& station : ride.stations)
                {
                    for (const auto& door : { station.entrance_spot, station.exit_spot })
                    {
                        if (door)
                            doors.push_back(*door);
                    }
                }
            }
            return doors;
        }
    } // namespace

    void footpath_drag(ParkScene& scene, ui::Point at, ui::Point size, bool down, bool over, bool cancel, bool shift, bool control)
    {
        ParkFootpaths& f = scene.footpaths;
        // A press on an arrow picks the slope by moving up or down, and lays the piece when let go.
        if (f.slope_press)
        {
            if (cancel)
                f.builder.slope = f.slope_before;
            else if (!down)
                f.lay_next = f.took_click = true;
            else
            {
                const auto steps = static_cast<int>(std::lround((*f.slope_press - at.y) / park::kSlopeDragStep));
                if (const park::PathSlope slope = park::stepped_slope(f.slope_before, steps); slope != f.builder.slope)
                {
                    f.builder.slope = slope;
                    f.dirty = true;
                }
                return;
            }
            f.slope_press.reset();
            f.dirty = true;
            return;
        }
        // With the remove tab on, a press takes away the path pointed at and every path passed over until let go.
        if (f.removing)
        {
            if (cancel)
            {
                if (!std::exchange(f.remove_marked, { }).empty())
                    f.dirty = true;
                return;
            }
            const auto ray = over ? std::nullopt : controls_ray(scene, at, size);
            const auto under = ray ? park::path_under(*ray, f.paths, scene.grid, ground_faded(scene)) : std::nullopt;
            const auto id = under ? std::optional(f.paths[under->path].id) : std::nullopt;
            if (id != f.remove_hover)
            {
                f.remove_hover = id;
                f.dirty = true;
            }
            if (down && id && !std::ranges::contains(f.remove_marked, *id))
            {
                f.remove_marked.push_back(*id);
                f.dirty = true;
            }
            else if (!down && !f.remove_marked.empty())
                f.remove_now = true;
            return;
        }
        // Shift raises or sinks the trail's first piece with the pointer; a press lays it, as on the arrow.
        if (!shift)
            f.lift.reset();
        else if (f.brush.empty() && !cancel)
        {
            const auto ray = over ? std::nullopt : controls_ray(scene, at, size);
            if (!f.lift && ray)
            {
                const auto tile = controls_tile(scene, at, size);
                const auto distance = park::ground_hit(*ray, scene.grid);
                if (tile && distance)
                {
                    // The trail's next piece pointed at is picked up where it is; elsewhere a trail begins.
                    if (const auto next = park::next_piece(f.builder, scene.grid); next && next->spot == *tile)
                    {
                        park::start_at(f.builder, *tile);
                        f.builder.start_height = next->start;
                    }
                    else
                        begin_trail(f, scene.rides, *tile, control);
                    f.lift = ParkFootpaths::Lift { ray->origin + ray->direction * *distance, park::next_piece(f.builder, scene.grid)->start };
                    f.hover.reset();
                    f.hover_edge.reset();
                    f.hover_door.reset();
                    f.hover_tile.reset();
                    f.dirty = true;
                }
            }
            else if (f.lift && ray)
            {
                // Up and down an upright plane through where Shift went down, facing the camera.
                const glm::vec3 across = glm::normalize(glm::vec3 { ray->direction.x, 0.0f, ray->direction.z });
                const float along = glm::dot(ray->direction, across);
                if (along > 1e-4f)
                {
                    const float y = ray->origin.y + ray->direction.y * glm::dot(f.lift->at - ray->origin, across) / along;
                    const std::int32_t height = f.lift->height + static_cast<std::int32_t>(std::lround(y - f.lift->at.y));
                    if (height != f.builder.start_height)
                    {
                        f.builder.start_height = height;
                        f.dirty = true;
                    }
                }
            }
            if (f.lift)
            {
                if (down)
                {
                    f.slope_press = at.y;
                    f.slope_before = f.builder.slope;
                }
                return;
            }
        }
        // With routing on, a click away from the arrow lays the route to where it was.
        if (f.routing && f.builder.start && f.brush.empty())
        {
            auto tile = over ? std::nullopt : controls_tile(scene, at, size);
            // A path off the ground pointed at is the goal itself, at its height.
            std::optional<std::int32_t> height;
            if (const auto ray = over ? std::nullopt : controls_ray(scene, at, size))
            {
                if (const auto under = park::path_under(*ray, f.paths, scene.grid, ground_faded(scene)))
                {
                    const save::PathTile& p = f.paths[under->path];
                    tile = save::Tile { p.col, p.row };
                    if (p.elevation && !park::is_ground_queue(p))
                        height = p.elevation->quantised_height;
                }
            }
            const bool on_arrow = tile && tile == park::next_spot(f.builder);
            if (cancel)
            {
                f.route_press = false;
                return;
            }
            if (!down && std::exchange(f.route_press, false))
            {
                f.lay_route = f.took_click = true;
                return;
            }
            if (down && !on_arrow)
            {
                f.route_press = f.route_press || tile.has_value();
                return;
            }
            if (!down)
            {
                const auto goal = on_arrow ? std::nullopt : tile;
                if (goal != f.route_goal || height != f.route_goal_height)
                {
                    f.route_goal = goal;
                    f.route_goal_height = goal ? height : std::nullopt;
                    f.dirty = true;
                }
                return;
            }
        }
        if (cancel)
        {
            if (!f.brush.empty())
            {
                f.brush.clear();
                f.dirty = true;
            }
            return;
        }
        if (!down)
        {
            if (!f.brush.empty())
            {
                f.drop = f.took_click = true;
                return;
            }
            // The path edge pointed at, if any.
            // Or a ride's door, before what is behind it.
            std::optional<std::pair<std::size_t, std::uint8_t>> edge;
            std::optional<std::array<std::int32_t, 4>> door;
            // While a trail goes on its arrow holds still: nothing else pointed at shows.
            const bool held = f.builder.start.has_value();
            if (const auto ray = over || held ? std::nullopt : controls_ray(scene, at, size))
            {
                const std::vector<save::PlacementSpot> doors = ride_doors(scene.rides);
                if (const auto d = park::door_under(*ray, doors, scene.grid, ground_faded(scene)))
                {
                    const save::PlacementSpot& spot = doors[d->door];
                    door = std::array { spot.col, spot.row, spot.height, static_cast<std::int32_t>(d->side) };
                }
                else if (const auto under = park::path_under(*ray, f.paths, scene.grid, ground_faded(scene)); under && under->side)
                    edge = std::pair { under->path, *under->side };
            }
            if (edge != f.hover_edge || door != f.hover_door)
            {
                f.hover_edge = edge;
                f.hover_door = door;
                f.hover.reset();
                park::FootpathBuilder b = f.builder;
                if (edge && park::start_from_edge(b, f.paths[edge->first], scene.grid, edge->second))
                    f.hover = std::move(b);
                else if (door)
                {
                    park::start_from_door(b, save::PlacementSpot { 0, (*door)[0], (*door)[1], (*door)[2], 0 }, static_cast<std::uint8_t>((*door)[3]));
                    f.hover = std::move(b);
                }
                f.dirty = true;
            }
            // Bare ground pointed at, away from the trail's next piece, which has its own arrow.
            const std::optional<save::Tile> tile = edge || door || over || held ? std::nullopt : controls_tile(scene, at, size);
            if (tile != f.hover_tile)
            {
                f.hover_tile = tile;
                f.dirty = true;
            }
            return;
        }
        const auto tile = controls_tile(scene, at, size);
        if (f.brush.empty())
        {
            // Pressing on an arrow: at a path's edge, or over the trail's next piece.
            const bool on_next = f.builder.start && tile && tile == park::next_spot(f.builder);
            if (f.hover || on_next)
            {
                if (f.hover)
                    f.builder = *std::exchange(f.hover, std::nullopt);
                f.hover_edge.reset();
                f.hover_door.reset();
                f.slope_press = at.y;
                f.slope_before = f.builder.slope;
                f.dirty = true;
                return;
            }
            // shortcut: queues are not painted, a click picks where one starts; paint them once queue lines
            // can be joined tile by tile in any order.
            if (!tile || chosen_queue(f))
                return;
            f.brush.push_back(*tile);
            f.dirty = true;
            return;
        }
        if (!tile || *tile == f.brush.back())
            return;
        for (const save::Tile spot : park::brush_line(f.brush.back(), *tile))
        {
            if (!std::ranges::contains(f.brush, spot))
                f.brush.push_back(spot);
        }
        f.dirty = true;
    }

    bool footpath_click(ParkScene& scene, ui::Point at, ui::Point size, bool control)
    {
        if (std::exchange(scene.footpaths.took_click, false) || scene.footpaths.removing)
            return true;
        const auto tile = controls_tile(scene, at, size);
        if (!tile)
            return false;
        begin_trail(scene.footpaths, scene.rides, *tile, control);
        return true;
    }

    void follow_footpaths(ParkScene& scene, float elapsed)
    {
        ParkFootpaths& f = scene.footpaths;
        if (!f.camera_goal || scene.dragging || scene.turning)
        {
            f.camera_goal.reset();
            return;
        }
        const glm::vec3 to = *f.camera_goal - scene.camera.target;
        if (glm::length(to) < kFollowNear)
            f.camera_goal.reset();
        scene.camera.target += to * (1 - std::exp(-kFollowRate * elapsed));
    }

    bool footpath_sell(ParkScene& scene, ui::Point at, ui::Point size)
    {
        ParkFootpaths& f = scene.footpaths;
        if (!f.brush.empty() || f.slope_press)
            return false;
        // A trail going on lets go of its arrow first, as in the game; the next right click sells.
        if (drop_footpath_trail(scene))
            return true;
        const auto ray = controls_ray(scene, at, size);
        const auto under = ray ? park::path_under(*ray, f.paths, scene.grid, ground_faded(scene)) : std::nullopt;
        if (!under)
            return false;
        f.sell = f.paths[under->path].id;
        return true;
    }

    bool drop_footpath_trail(ParkScene& scene)
    {
        ParkFootpaths& f = scene.footpaths;
        if (!f.builder.start)
            return false;
        f.builder.start.reset();
        f.builder.start_height.reset();
        f.builder.trail.clear();
        f.builder.on_slope = false;
        f.lift.reset();
        f.route_goal.reset();
        f.dirty = true;
        return true;
    }

    namespace
    {
        /// Draws `ghost` with the ground's pictures, or all of it with picture `only` when given.
        void draw_ground_ghost(render::Device& device, const ParkScene& scene, const park::TerrainMesh& ghost, std::optional<std::string_view> only, const glm::mat4& transform, const render::SceneLight& light)
        {
            if (ghost.indices.empty())
                return;
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(ghost.vertices)));
            const auto indices = device.allocate_transient(std::as_bytes(std::span(ghost.indices)));
            if (!vertices || !indices)
                return;
            for (const park::GroundBatch& batch : ghost.batches)
            {
                const auto texture = only ? scene.ground_textures.find(*only) : scene.ground_textures.find(park::ground_key(batch.paint, batch.index));
                render::DrawCommand draw;
                draw.pipeline = scene.ground_pipeline;
                draw.vertices = *vertices;
                draw.indices = *indices;
                draw.index_type = render::IndexType::U32;
                draw.first = batch.first;
                draw.count = batch.count;
                draw.transform = transform;
                draw.texture = texture != scene.ground_textures.end() ? texture->second.handle : scene.blank;
                draw.filter = render::Filter::Linear;
                draw.address = render::AddressMode::Repeat;
                draw.light = ghost_light(light, scene.clock);
                device.draw(draw);
            }
        }
    } // namespace

    bool ground_faded(const ParkScene& scene)
    {
        return scene.see_through_ground || scene.footpaths.under_ground;
    }

    void draw_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        draw_ground_ghost(device, scene, scene.footpaths.ghost, std::nullopt, transform, light);
        if (scene.ground_textures.contains(kArrowKey))
            draw_ground_ghost(device, scene, scene.footpaths.arrow, kArrowKey, transform, light);
    }

    void draw_raised_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        draw_ghost_mesh(device, scene, scene.footpaths.raised_ghost, transform, light);
    }

} // namespace rct3::scene
