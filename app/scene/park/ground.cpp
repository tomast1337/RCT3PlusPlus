// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/ground.hpp"

#include "logging/log.hpp"
#include "park/enclosures.hpp"
#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/ground/textures.hpp"
#include "park/object/placement.hpp"
#include "park/path/decks.hpp"
#include "park/path/tunnels.hpp"
#include "park/relief_maps.hpp"
#include "park/surrounding_land.hpp"
#include "park/track/ground.hpp"
#include "profiling/profiler.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "ui/ui_textures.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {
        void upload_ground(render::Device& device, ParkScene& scene, rct3::park::ground::GroundImages& images);
    } // namespace

    void picture_styles(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const std::vector<std::string>& styles)
    {
        ParkFootpaths& f = scene.footpaths;
        rct3::park::ground::GroundImages images;
        for (std::size_t i = 0; i < styles.size(); ++i)
        {
            if (i < f.pictured.size() && f.pictured[i] == styles[i])
                continue;
            const std::string file = rct3::park::ground::path_texture_file(styles[i]);
            const auto found = assets.install ? assets.install->find(file) : std::nullopt;
            if (!found)
            {
                logging::logger("park").warning("{}: not found", file);
                continue;
            }
            if (const auto added = rct3::park::ground::add_path_images(images, *found, styles[i], static_cast<std::uint8_t>(i)); !added)
                logging::logger("park").warning("{}: {}", file, added.error());
        }
        f.pictured = styles;
        upload_ground(device, scene, images);
    }

    void picture_queues(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const std::vector<rct3::park::ground::QueuePainting>& paintings)
    {
        ParkFootpaths& f = scene.footpaths;
        rct3::park::ground::GroundImages images;
        for (std::size_t i = 0; i < paintings.size(); ++i)
        {
            if (i < f.queues_pictured.size() && f.queues_pictured[i] == paintings[i])
                continue;
            const std::string file = rct3::park::ground::queue_texture_file(paintings[i].style);
            const auto found = assets.install ? assets.install->find(file) : std::nullopt;
            const auto added = found ? rct3::park::ground::add_queue_image(images, *found, paintings[i], static_cast<std::uint8_t>(i)) : std::expected<bool, std::string>(std::unexpected("not found"));
            if (!added)
                logging::logger("park").warning("{}: {}", file, added.error());
        }
        f.queues_pictured = paintings;
        upload_ground(device, scene, images);
    }

    namespace
    {
        void upload_ground(render::Device& device, ParkScene& scene, rct3::park::ground::GroundImages& images)
        {
            if (images.empty())
                return;
            fit_texture_detail(scene, images);
            if (scene.relief_maps)
                rct3::park::add_relief_maps(images);
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

    void refresh_pavings(ParkScene& scene)
    {
        std::unordered_map<std::uint64_t, const save::model::PlacedObject*> placed;
        for (const save::model::PlacedObject& o : scene.objects)
            placed.emplace(o.id, &o);
        // A track piece's footprint is paved only under a base that asks for it.
        const auto footprint_of = [&](const save::model::PlacementSpot& spot, bool piece) {
            std::vector<save::model::Tile> tiles;
            const auto o = placed.find(spot.object);
            if (o == placed.end())
                return tiles;
            const auto model = scene.models.find(rct3::park::object::model_ref(*o->second));
            if (model == scene.models.end() || (piece && !model->second.paved_under))
                return tiles;
            const rct3::park::object::ObjectModel& item = model->second;
            save::model::PlacedObject at;
            at.col = spot.col;
            at.row = spot.row;
            at.rotation = spot.direction;
            const auto [xs, zs] = item.squares;
            for (int z = 0; z < zs; ++z)
            {
                for (int x = 0; x < xs; ++x)
                {
                    // An irregular footprint paves only the squares it really stands on.
                    if (item.fence_outline.empty() || item.fence_outline[static_cast<std::size_t>(z) * static_cast<std::size_t>(xs) + static_cast<std::size_t>(x)])
                        tiles.push_back(rct3::park::object::square_tile(at, item.squares, x, z));
                }
            }
            return tiles;
        };
        scene.footpaths.pavings = rct3::park::ground::ride_pavings(scene.rides, [&](const save::model::PlacementSpot& spot) { return footprint_of(spot, false); }, [&](const save::model::PlacementSpot& spot) { return footprint_of(spot, true); });
        // A changing room's floor is paved as a stall's is, opening onto the tile on its side away from the pool.
        for (const rct3::park::pools::PoolRoom& room : scene.pools.rooms)
        {
            const auto o = std::ranges::find_if(scene.objects, [&](const save::model::PlacedObject& p) { return p.col == room.col && p.row == room.row && p.symbol == room.symbol; });
            if (o == scene.objects.end())
                continue;
            const auto [dc, dr] = room.side.step();
            const save::model::Tile door { room.col - dc, room.row - dr };
            scene.footpaths.pavings.push_back({ .tiles = { { room.col, room.row } }, .height = o->base_height.value_or(0), .source = door, .links = { door }, .owner = o->id });
        }
        scene.footpaths.pavings_stale = true;
    }

    void rebuild_ground(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<rct3::park::TileRect> touched)
    {
        const profiling::Scope zone("RebuildGround");
        ParkFootpaths& f = scene.footpaths;
        scene.supports.stale = true; // Paths or the ground changed
        const std::vector<rct3::park::ground::PavedTile> laid = profiling::section("Pavings", [&] { return rct3::park::ground::lay_pavings(f.paths, f.pavings, scene.grid); });
        std::vector<save::model::PathTile> with = f.paths;
        for (const rct3::park::ground::PavedTile& t : laid)
            with.push_back(t.path);
        // Tunnels of paths and of track.
        std::vector<rct3::park::GroundOpening> hollows = profiling::section("Hollows", [&] { return rct3::park::path::tunnel_hollows(f.paths, scene.grid); });
        std::ranges::copy(profiling::section("BuriedHollows", [&] { return rct3::park::track::buried_hollows(scene.objects, scene.models, scene.grid); }), std::back_inserter(hollows));
        rct3::park::ground::GroundPathMap paths = profiling::section("PathMap", [&] { return rct3::park::ground::ground_path_map(with, scene.grid); });
        paths.walls = f.walls;
        rct3::park::ground::seal_pavings(paths, laid, f.pavings);
        // shortcut: reads the path styles again while the panel has not, each time they are needed; keep them once read when that shows.
        std::vector<rct3::park::path::PathStyle> read;
        const auto style_named = [&](std::string_view name) -> const rct3::park::path::PathStyle* {
            for (const std::vector<rct3::park::path::PathStyle>* styles : { &f.styles, &read })
            {
                if (const auto found = std::ranges::find(*styles, name, &rct3::park::path::PathStyle::name); found != styles->end())
                    return &*found;
            }
            if (!read.empty() || !assets.install)
                return nullptr;
            for (const std::string_view folder : { rct3::park::path::kPathStyleFolder, rct3::park::path::kQueueStyleFolder })
            {
                if (const auto found = assets.install->find(folder))
                {
                    std::vector<rct3::park::path::PathStyle> more = rct3::park::path::folder_path_styles(*found);
                    read.insert(read.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
                }
            }
            const auto found = std::ranges::find(read, name, &rct3::park::path::PathStyle::name);
            return found != read.end() ? &*found : nullptr;
        };
        // Once placed, a ride, stall or door stands on the supports of the style it is paved in, as the path it
        // opens onto does; only its ghost shows the set its own item names.
        std::unordered_map<std::uint64_t, std::string> sets;
        for (const rct3::park::ground::PavedTile& t : laid)
        {
            const std::uint64_t owner = f.pavings[t.paving].owner;
            if (const rct3::park::path::PathStyle* style = owner != 0 ? style_named(t.path.surface_name) : nullptr)
                sets.try_emplace(owner, style->support_group);
        }
        for (save::model::PlacedObject& o : scene.objects)
        {
            if (const auto set = sets.find(o.id); set != sets.end())
                o.support_set = set->second;
        }
        std::uint64_t id = 0;
        for (const save::model::PathTile& p : f.paths)
            id = std::max(id, p.id);
        for (const save::model::PlacedObject& o : scene.objects)
            id = std::max(id, o.id);
        // Paving off the ground is a deck of its style at its height, open towards its own tiles and its links:
        // laid again when the pavings changed, or something on or beside their tiles did.
        const auto near = [&](save::model::Tile t) { return touched->grown(1).contains(t.col, t.row); };
        if (f.pavings_stale || !touched || std::ranges::any_of(f.pavings, [&](const rct3::park::ground::Paving& p) { return std::ranges::any_of(p.tiles, near) || std::ranges::any_of(p.links, near); }))
        {
            const profiling::Scope decks_zone("Decks");
            bool changed = false;
            std::vector<save::model::PlacedObject> decks;
            for (const rct3::park::ground::PavedTile& t : laid)
            {
                if (!t.path.elevation || rct3::park::ground::is_ground_queue(t.path))
                    continue;
                const rct3::park::path::PathStyle* style = style_named(t.path.surface_name);
                if (!style)
                    continue;
                const rct3::park::ground::Paving& paving = f.pavings[t.paving];
                std::vector<save::model::PathTile> around { t.path };
                for (const auto& tiles : { paving.tiles, paving.links })
                {
                    for (const save::model::Tile other : tiles)
                    {
                        if (std::abs(other.col - t.path.col) + std::abs(other.row - t.path.row) != 1 || !scene.grid.contains(other.col, other.row))
                            continue;
                        save::model::PathTile& o = around.emplace_back(t.path);
                        o.col = static_cast<std::uint8_t>(other.col);
                        o.row = static_cast<std::uint8_t>(other.row);
                        // A raised path of the park it links to opens towards it in turn.
                        const auto joined = std::ranges::find_if(f.paths, [&](const save::model::PathTile& p) { return p.col == other.col && p.row == other.row && p.elevation && !rct3::park::ground::is_ground_queue(p) && p.elevation->slope == 0 && p.elevation->quantised_height == paving.height; });
                        const rct3::park::path::PathStyle* own = joined != f.paths.end() && std::ranges::contains(paving.links, other) ? style_named(joined->surface_name) : nullptr;
                        if (!own)
                            continue;
                        std::vector<save::model::PathTile> next = f.paths;
                        next.push_back(t.path);
                        const auto [symbol, turn] = rct3::park::path::deck_for(*own, *joined, next, scene.grid);
                        const auto shown = std::ranges::find(scene.objects, joined->elevation->scenery_ref, &save::model::PlacedObject::id);
                        if (!symbol.empty() && shown != scene.objects.end() && (shown->symbol != symbol || shown->rotation != turn.value))
                        {
                            save::model::PlacedObject turned = *shown;
                            turned.symbol = symbol;
                            turned.rotation = turn.value;
                            if (load_object_model(scene, f.models, assets, turned))
                            {
                                *shown = std::move(turned);
                                changed = true;
                            }
                        }
                    }
                }
                save::model::PlacedObject deck = deck_object(*style, t.path, around, scene.grid, ++id);
                deck.fixture = true;
                // Scaffold tops beside it end at the same height; a hair over them keeps the two from flickering.
                deck.height_adjust = 0.02f;
                if (!load_object_model(scene, f.models, assets, deck))
                    continue;
                decks.push_back(std::move(deck));
            }
            if (replace_fixtures(scene, f.paving_decks, std::move(decks)) || changed)
                objects_changed(scene);
        }
        f.pavings_stale = false;
        // The fences round the park and the enclosures lean with the ground as it is now.
        profiling::section("Leans", [&] {
            const std::set<std::uint64_t> panels(scene.fence_panels.begin(), scene.fence_panels.end());
            const std::set<std::uint64_t> pieces(scene.enclosure_panels.begin(), scene.enclosure_panels.end());
            std::vector<std::pair<std::uint64_t, glm::vec2>> leaning;
            for (const save::model::PlacedObject& o : scene.objects)
            {
                if (!panels.contains(o.id) && !pieces.contains(o.id))
                    continue;
                const glm::vec2 lean = pieces.contains(o.id) ? rct3::park::fence_piece_lean(scene.grid, o) : rct3::park::panel_lean(scene.grid, o);
                const auto was = scene.leans.find(o.id);
                if (lean != (was != scene.leans.end() ? was->second : glm::vec2(0.0f)))
                    leaning.emplace_back(o.id, lean);
            }
            if (!leaning.empty())
            {
                wait_for_build(scene); // The build beside the frame reads the leans
                for (const auto& [panel, lean] : leaning)
                {
                    if (lean == glm::vec2(0.0f))
                        scene.leans.erase(panel);
                    else
                        scene.leans[panel] = lean;
                }
            }
        });
        // The ground is built in chunks, and only those the change reaches are built again: with the tile
        // round it, as the ground's shading, the walls between tiles and the paths' edges look at their neighbours.
        const int across = (scene.grid.width + kGroundChunk - 1) / kGroundChunk;
        const int down = (scene.grid.height + kGroundChunk - 1) / kGroundChunk;
        const std::size_t count = static_cast<std::size_t>(across) * static_cast<std::size_t>(down) + 1;
        if (scene.ground_chunks.size() != count)
        {
            touched.reset();
            for (const GroundChunk& chunk : scene.ground_chunks)
            {
                for (const render::BufferHandle b : { chunk.vertices, chunk.indices })
                {
                    if (b)
                        device.destroy_buffer(b);
                }
            }
            scene.ground_chunks.assign(count, { });
        }
        const std::optional<rct3::park::TileRect> reach = touched ? std::optional(touched->grown(1)) : std::nullopt;
        const auto show = [&](GroundChunk& chunk, rct3::park::TerrainMesh&& mesh) {
            const profiling::Scope upload_zone("Upload");
            for (const render::BufferHandle b : { chunk.vertices, chunk.indices })
            {
                if (b)
                    device.destroy_buffer(b);
            }
            chunk = { .batches = std::move(mesh.batches), .water = std::move(mesh.water) };
            if (mesh.indices.empty())
                return;
            chunk.low = mesh.vertices.front().position;
            chunk.high = chunk.low;
            for (const rct3::park::GroundVertex& v : mesh.vertices)
            {
                chunk.low = glm::min(chunk.low, v.position);
                chunk.high = glm::max(chunk.high, v.position);
            }
            auto vertices = device.create_buffer({ render::BufferUsage::Vertex, std::as_bytes(std::span(mesh.vertices)), "ground vertices" });
            auto indices = device.create_buffer({ render::BufferUsage::Index, std::as_bytes(std::span(mesh.indices)), "ground indices" });
            if (!vertices || !indices)
            {
                logging::logger("park").warning("ground: {}", !vertices ? vertices.error() : indices.error());
                if (vertices)
                    device.destroy_buffer(*vertices);
                if (indices)
                    device.destroy_buffer(*indices);
                chunk.batches.clear();
                return;
            }
            chunk.vertices = *vertices;
            chunk.indices = *indices;
        };
        std::optional<std::vector<rct3::park::ground::QueuePainting>> paintings;
        for (std::size_t i = 0; i + 1 < count; ++i)
        {
            const int col = static_cast<int>(i % static_cast<std::size_t>(across)) * kGroundChunk;
            const int row = static_cast<int>(i / static_cast<std::size_t>(across)) * kGroundChunk;
            const rct3::park::TileRect area { col, row, col + kGroundChunk, row + kGroundChunk };
            if (reach && (area.end_col <= reach->col || reach->end_col <= area.col || area.end_row <= reach->row || reach->end_row <= area.row))
                continue;
            rct3::park::TerrainMesh mesh = profiling::section("Terrain", [&] { return rct3::park::terrain_mesh(scene.grid, f.water, hollows, area); });
            std::vector<rct3::park::GroundOpening> inside;
            std::ranges::copy_if(hollows, std::back_inserter(inside), [&](const rct3::park::GroundOpening& h) { return area.contains(h.col, h.row); });
            rct3::park::add_ground(mesh, rct3::park::path::tunnel_lining(inside, scene.grid));
            profiling::section("PathMesh", [&] { rct3::park::add_ground(mesh, rct3::park::ground::ground_path_mesh(paths, scene.grid, area)); });
            rct3::park::ground::GroundQueues queues = profiling::section("Queues", [&] { return rct3::park::ground::ground_queues(with, scene.grid, area); });
            rct3::park::add_ground(mesh, std::move(queues.mesh));
            paintings = std::move(queues.paintings);
            show(scene.ground_chunks[i], std::move(mesh));
        }
        // The land round the park meets only the tiles along its edge.
        if (!reach || reach->col <= 0 || reach->row <= 0 || reach->end_col >= scene.grid.width || reach->end_row >= scene.grid.height)
            show(scene.ground_chunks.back(), profiling::section("Surroundings", [&] { return rct3::park::surrounding_land(scene.grid, f.water); }));
        profiling::section("Water", [&] {
            // The lakes, the pools, then the water round the park.
            std::vector<rct3::park::WaterVertex> water;
            for (const GroundChunk& chunk : std::span(scene.ground_chunks).first(count - 1))
                water.insert(water.end(), chunk.water.begin(), chunk.water.end());
            std::ranges::copy(rct3::park::pool_water(scene.grid, f.pools), std::back_inserter(water));
            water.insert(water.end(), scene.ground_chunks.back().water.begin(), scene.ground_chunks.back().water.end());
            if (auto replaced = replace_park_water(device, scene.water, water); !replaced)
                logging::logger("park").warning("water: {}", replaced.error());
            // The waterfalls take their lakes' colours; every change to the falls ends with the ground built again.
            refresh_park_waterfalls(device, scene.waterfalls, f.waterfalls, scene.water.surface);
        });
        picture_styles(device, scene, assets, paths.styles);
        if (paintings)
            picture_queues(device, scene, assets, *paintings);
        scene.grid_lines.stale = true;
        scene.contour_lines.stale = true;
    }

} // namespace rct3::scene::park
