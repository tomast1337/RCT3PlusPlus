// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/footpaths.hpp"

#include "config/game_options.hpp"
#include "logging/log.hpp"
#include "park/boundary_fence.hpp"
#include "park/camera.hpp"
#include "park/enclosures.hpp"
#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/ground/textures.hpp"
#include "park/object/pick.hpp"
#include "park/object/placement.hpp"
#include "park/path/decks.hpp"
#include "park/path/tunnels.hpp"
#include "park/queue_fences.hpp"
#include "park/queue_lines.hpp"
#include "park/relief_maps.hpp"
#include "park/support/columns.hpp"
#include "park/surrounding_land.hpp"
#include "park/track/ground.hpp"
#include "profiling/profiler.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
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
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rct3::scene::park
{

    const rct3::park::support::SupportSets* support_sets(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        ParkFootpaths& f = scene.footpaths;
        if (!f.support_sets && assets.install)
        {
            if (const auto found = assets.install->find(std::format("{}.common.ovl", rct3::park::support::kSupportOverlay)))
            {
                if (auto sets = rct3::park::support::load_support_sets(*found))
                    f.support_sets = std::move(*sets);
                else
                    logging::logger("park").warning("supports: {}", sets.error());
            }
        }
        return f.support_sets ? &*f.support_sets : nullptr;
    }

    std::vector<save::model::PlacedObject> supports_under(const ParkScene& scene, std::span<const save::model::PlacedObject> ghosts, const rct3::park::support::SupportSets& sets)
    {
        std::vector<save::model::PlacedObject> objects = scene.objects;
        std::uint64_t last = 0;
        for (const save::model::PlacedObject& o : objects)
            last = std::max(last, o.id);
        std::set<rct3::park::support::SupportTile> tiles;
        std::unordered_set<std::uint64_t> ids;
        for (const save::model::PlacedObject& ghost : ghosts)
        {
            save::model::PlacedObject& o = objects.emplace_back(ghost);
            o.id = ++last;
            const auto under = rct3::park::support::support_tiles(o, scene.models);
            tiles.insert(under.begin(), under.end());
            ids.insert(o.id);
        }
        rct3::park::support::BuiltSupports built = rct3::park::support::support_columns(objects, scene.models, scene.footpaths.paths, scene.grid, sets, last, supports_passed(scene), tiles);
        std::vector<save::model::PlacedObject> out;
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
        for (const save::model::Ride& ride : scene.rides.rides)
        {
            for (const save::model::TrackPiece& piece : ride.track ? ride.track->pieces : std::vector<save::model::TrackPiece> { })
            {
                if (piece.platform)
                    platforms.insert(piece.platform->id);
            }
        }
        // A queue's railings stand on its deck: they hold up nothing and are not in the way of its columns.
        std::set<std::pair<std::string_view, std::string_view>> railings;
        for (const save::model::PathTile& p : scene.footpaths.paths)
        {
            if (p.queue)
                railings.emplace(p.queue->fence_symbol, p.queue->fence_overlay);
        }
        std::unordered_set<std::uint64_t> out(scene.footpaths.paving_decks.begin(), scene.footpaths.paving_decks.end());
        // A ride's fence stands on its floor's rim: it holds up nothing and is not in the way of the ride's columns.
        for (const auto& [_, pieces] : scene.ride_fence_pieces)
            out.insert(pieces.begin(), pieces.end());
        for (const save::model::PlacedObject& o : scene.objects)
        {
            if ((o.owner_ref != o.id && platforms.contains(o.owner_ref)) || (o.fixture && railings.contains({ o.symbol, o.overlay })))
                out.insert(o.id);
        }
        return out;
    }

    namespace
    {

        constexpr std::array<std::string_view, 2> kIconFiles { rct3::park::path::kPathIconFile, rct3::park::path::kQueueIconFile };
        /// Of the next piece over the paths already laid, so the two never fight for the same depth.
        constexpr float kGhostLift = 0.02f;
        /// Of the arrow over the next piece, so it shows over the piece's own picture.
        constexpr float kArrowLift = 0.04f;
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

        std::optional<ui::screens::FootpathPanel&> panel_of(const ParkScene& scene)
        {
            return scene.desktop->root().find_as<ui::screens::FootpathPanel>(kFootpathPanelName);
        }

        bool chosen_queue(const ParkFootpaths& f)
        {
            return f.style && *f.style < f.styles.size() && f.styles[*f.style].queue;
        }

        ui::Currency currency_of(const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            return ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
        }

        std::u16string name_of(const ui::InterfaceAssets& assets, const rct3::park::path::PathStyle& style)
        {
            const std::u16string_view text = assets.strings->get(style.name_text);
            return text.empty() ? std::u16string(style.name.begin(), style.name.end()) : std::u16string(text);
        }

        /// The price of a piece of `style` on the ground.
        std::int64_t next_cost(const rct3::park::path::PathStyle& style)
        {
            return rct3::park::piece_cost(style.piece_cost, false, 0);
        }

        /// The price of `piece` in `style`, dearer the higher it is raised.
        std::int64_t piece_price(const rct3::park::path::PathStyle& style, const rct3::park::NextPiece& piece, const save::model::TerrainGrid& grid)
        {
            return rct3::park::piece_price(piece, grid, style.piece_cost);
        }

        /// The object drawing raised path `path` of `style`, with the piece the paths around it call for.
        save::model::PlacedObject deck_object(const rct3::park::path::PathStyle& style, const save::model::PathTile& path, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, std::uint64_t id)
        {
            const auto [symbol, turn] = rct3::park::path::deck_for(style, path, paths, grid);
            const save::model::PathElevation& e = *path.elevation;
            save::model::PlacedObject o;
            o.id = id;
            o.owner_ref = path.id;
            o.symbol = symbol;
            o.overlay = style.overlay;
            o.col = path.col;
            o.row = path.row;
            // Stands on its low end: a piece going down is entered at its top.
            o.base_height = e.quantised_height - (rct3::park::slope_falls(e.slope) ? static_cast<std::int32_t>(rct3::park::ground::path_rise(e.slope)) : 0);
            o.rotation = turn.value;
            // Held at its own height: an object with no offset from the ground would be put on the ground.
            o.force_absolute_height = true;
            return o;
        }

        bool offered(const rct3::park::BuildOffer& offer, const rct3::park::path::PathStyle& style)
        {
            return offer.offers_path(style.name, style.names_category);
        }

        /// The panel's choices: the path styles the park offers, then its queue styles.
        std::pair<std::vector<ui::screens::FootpathStyle>, std::vector<ui::screens::FootpathStyle>> panel_styles(const ParkFootpaths& f, const rct3::park::BuildOffer& offer, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            const ui::Currency money = currency_of(assets, options);
            std::pair<std::vector<ui::screens::FootpathStyle>, std::vector<ui::screens::FootpathStyle>> out;
            for (const rct3::park::path::PathStyle& s : f.styles)
            {
                if (offered(offer, s))
                    (s.queue ? out.second : out.first).push_back({ s.icon, name_of(assets, s), ui::money_text(money, next_cost(s)) });
            }
            return out;
        }

        std::optional<ui::screens::FootpathPick> pick_of(const ParkFootpaths& f, const rct3::park::BuildOffer& offer)
        {
            if (!f.style || *f.style >= f.styles.size())
                return std::nullopt;
            const bool queue = f.styles[*f.style].queue;
            const auto before = std::ranges::count_if(std::span(f.styles).first(*f.style), [&](const rct3::park::path::PathStyle& s) { return s.queue == queue && offered(offer, s); });
            return ui::screens::FootpathPick { queue, static_cast<std::size_t>(before) };
        }

        std::optional<std::size_t> style_of(const ParkFootpaths& f, const rct3::park::BuildOffer& offer, ui::screens::FootpathPick pick)
        {
            std::size_t seen = 0;
            for (std::size_t i = 0; i < f.styles.size(); ++i)
            {
                if (f.styles[i].queue != pick.queue || !offered(offer, f.styles[i]))
                    continue;
                if (seen++ == pick.index)
                    return i;
            }
            return std::nullopt;
        }

        void upload_ground(render::Device& device, ParkScene& scene, rct3::park::ground::GroundImages& images);

        /// Has the scene's ground pictures match `styles`, the path styles in the order the ground's batches
        /// number them, loading those it lacks.
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

        /// The same for the ground queues' `paintings`.
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
        });
        picture_styles(device, scene, assets, paths.styles);
        if (paintings)
            picture_queues(device, scene, assets, *paintings);
        scene.grid_lines.stale = true;
        scene.contour_lines.stale = true;
    }

    namespace
    {
        /// The stroke as it would be laid now, or the path a click on the tile pointed at would lay; nothing
        /// when there is neither or a queue is chosen.
        std::optional<rct3::park::path::DragPlan> stroke_plan(const ParkScene& scene)
        {
            const ParkFootpaths& f = scene.footpaths;
            // shortcut: strokes lay paths on the ground only, no queues or raised runs; upgrade with the A-to-B route.
            if (!f.style || *f.style >= f.styles.size() || f.styles[*f.style].queue)
                return std::nullopt;
            if (f.brush.empty() && !f.hover_tile)
                return std::nullopt;
            // Each tile of the stroke paints the brush's square round it.
            std::vector<save::model::Tile> area;
            for (const save::model::Tile spot : f.brush.empty() ? std::span(&*f.hover_tile, 1) : std::span(f.brush))
            {
                for (const save::model::Tile t : rct3::park::path::brush_square(spot, f.brush_size))
                {
                    if (!std::ranges::contains(area, t))
                        area.push_back(t);
                }
            }
            rct3::park::path::DragPlan plan = rct3::park::path::plan_drag(area, scene.grid, f.paths, f.solids);
            if (!f.brush.empty())
                return plan;
            return plan.lay.empty() ? std::nullopt : std::optional(std::move(plan));
        }

        std::int64_t stroke_cost(const ParkFootpaths& f, const rct3::park::path::DragPlan& plan)
        {
            return static_cast<std::int64_t>(plan.lay.size()) * next_cost(f.styles[*f.style]);
        }

        /// The route to the tile pointed at, with routing on, and whether it only goes through what is in the
        /// way: then it is the route that ignores that, shown refused so the player sees what blocks it.
        std::pair<std::optional<std::vector<rct3::park::path::RouteStep>>, bool> route_for(const ParkScene& scene)
        {
            const ParkFootpaths& f = scene.footpaths;
            if (!f.routing || !f.route_goal || !f.builder.start || !f.style || *f.style >= f.styles.size())
                return { };
            const auto cost = static_cast<std::int32_t>(next_cost(f.styles[*f.style]));
            // shortcut: routes are one tile wide whatever the brush size; widen them once raised runs beside each
            // other can be checked lane by lane.
            if (auto route = rct3::park::path::find_route(f.builder, *f.route_goal, scene.grid, f.paths, cost, f.route_goal_height, f.solids, f.straight))
                return { std::move(route), false };
            auto through = rct3::park::path::find_route(f.builder, *f.route_goal, scene.grid, f.paths, cost, f.route_goal_height, std::nullopt, f.straight);
            const bool blocked = through.has_value();
            return { std::move(through), blocked };
        }

        std::int64_t route_cost(const ParkScene& scene, const std::vector<rct3::park::path::RouteStep>& route)
        {
            const ParkFootpaths& f = scene.footpaths;
            return rct3::park::path::route_price(route, f.builder, scene.grid, f.paths, static_cast<std::int32_t>(next_cost(f.styles[*f.style])));
        }

        std::uint64_t unused_id(const ParkScene& scene)
        {
            std::uint64_t last = 0;
            for (const save::model::PathTile& p : scene.footpaths.paths)
                last = std::max(last, p.id);
            for (const save::model::PlacedObject& o : scene.objects)
                last = std::max(last, o.id);
            return last + 1;
        }

        /// The tiles of `spots` and those beside them, as a mask over the grid.
        std::vector<bool> around(const save::model::TerrainGrid& grid, std::span<const save::model::Tile> spots)
        {
            std::vector<bool> keep(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), false);
            for (const save::model::Tile spot : spots)
            {
                for (const save::model::Tile near : { spot, rct3::park::step(spot, rct3::park::Side::kNextRow), rct3::park::step(spot, rct3::park::Side::kNextCol), rct3::park::step(spot, rct3::park::Side::kRowBefore), rct3::park::step(spot, rct3::park::Side::kColBefore) })
                {
                    if (grid.contains(near.col, near.row))
                        keep[static_cast<std::size_t>(near.row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(near.col)] = true;
                }
            }
            return keep;
        }

        /// The ground's paths and queues as they would be with `with`, on the tiles of `spots` and beside them:
        /// the new tiles and the neighbours whose edges they change, as the ground will draw them.
        rct3::park::TerrainMesh ground_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const save::model::PathTile> with, std::span<const save::model::Tile> spots, glm::vec4 tint)
        {
            const std::vector<rct3::park::ground::PavedTile> laid = rct3::park::ground::lay_pavings(with, scene.footpaths.pavings, scene.grid);
            std::vector<save::model::PathTile> paved(with.begin(), with.end());
            for (const rct3::park::ground::PavedTile& t : laid)
                paved.push_back(t.path);
            rct3::park::ground::GroundPathMap map = rct3::park::ground::ground_path_map(paved, scene.grid);
            map.walls = scene.footpaths.walls;
            rct3::park::ground::seal_pavings(map, laid, scene.footpaths.pavings);
            picture_styles(device, scene, assets, map.styles);
            // Built only round the spots: only_tiles keeps them and those beside them.
            std::optional<rct3::park::TileRect> area;
            for (const save::model::Tile spot : spots)
            {
                const rct3::park::TileRect one = rct3::park::TileRect { spot.col, spot.row, spot.col + 1, spot.row + 1 }.grown(1);
                area = !area ? one : rct3::park::TileRect { std::min(area->col, one.col), std::min(area->row, one.row), std::max(area->end_col, one.end_col), std::max(area->end_row, one.end_row) };
            }
            if (!area)
                return { };
            rct3::park::TerrainMesh mesh = rct3::park::ground::ground_path_mesh(map, scene.grid, area);
            rct3::park::ground::GroundQueues queues = rct3::park::ground::ground_queues(paved, scene.grid, area);
            picture_queues(device, scene, assets, queues.paintings);
            rct3::park::add_ground(mesh, queues.mesh);
            rct3::park::TerrainMesh ghost = rct3::park::only_tiles(mesh, scene.grid, around(scene.grid, spots));
            for (rct3::park::GroundVertex& v : ghost.vertices)
            {
                v.colour = tint;
                v.position.y += kGhostLift;
            }
            return ghost;
        }

        /// The style `path` was laid in.
        const rct3::park::path::PathStyle* style_named(const ParkFootpaths& f, const save::model::PathTile& path)
        {
            const bool queue = path.kind == save::model::PathKind::Queue;
            const auto found = std::ranges::find_if(f.styles, [&](const rct3::park::path::PathStyle& s) { return s.queue == queue && s.name == path.surface_name; });
            return found != f.styles.end() ? &*found : nullptr;
        }

        /// Pays back what laying `path` cost, floating it up from there with `also` (hundredths, above 0 for money
        /// in) paid elsewhere for what stood on it, so the two show as one sum.
        void refund(ParkScene& scene, const save::model::PathTile& path, std::int64_t also = 0)
        {
            const rct3::park::path::PathStyle* style = style_named(scene.footpaths, path);
            const std::int64_t back = style != nullptr ? rct3::park::laid_price(path, scene.grid, style->piece_cost) : 0;
            simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, -back, simulation::park::LedgerLine::Landscaping, false);
            glm::vec3 at = tile_point(scene.grid, { path.col, path.row });
            if (path.elevation && !rct3::park::ground::is_ground_queue(path))
                at.y = static_cast<float>(path.elevation->quantised_height);
            float_money(scene, at, back + also);
        }

        /// The paths the remove tab would take away, red over themselves: those a press went over and the one
        /// pointed at.
        void remove_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            std::vector<save::model::PathTile> ground;
            std::vector<save::model::Tile> spots;
            std::vector<save::model::PlacedObject> decks;
            for (const save::model::PathTile& p : f.paths)
            {
                if (!std::ranges::contains(f.remove_marked, p.id) && p.id != f.remove_hover)
                    continue;
                if (!p.elevation || rct3::park::ground::is_ground_queue(p))
                {
                    ground.push_back(p);
                    spots.push_back({ p.col, p.row });
                }
                else if (const auto deck = std::ranges::find(scene.objects, p.elevation->scenery_ref, &save::model::PlacedObject::id); deck != scene.objects.end())
                    decks.push_back(*deck);
            }
            f.ghost = ground_ghost(device, scene, assets, ground, spots, kRefusedTint);
            f.raised_ghost = rct3::park::object::object_mesh(decks, scene.models, scene.grid);
            for (rct3::park::GroundVertex& v : f.raised_ghost.vertices)
                v.colour *= kRefusedTint;
            ghost_textures(device, scene, f.raised_ghost);
        }

        /// The route shown as it would be laid: its ground pieces over the ground, its raised ones as their decks
        /// and supports.
        void route_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            const rct3::park::path::PathStyle& style = f.styles[*f.style];
            const bool affordable = !scene.park.cash || *scene.park.cash >= route_cost(scene, *f.route);
            const glm::vec4 tint = affordable && !f.route_blocked ? kGhostTint : kRefusedTint;
            std::vector<save::model::PathTile> with = f.paths;
            rct3::park::FootpathBuilder trial = f.builder;
            std::uint64_t id = unused_id(scene);
            std::vector<save::model::Tile> ground;
            std::vector<std::size_t> raised;
            for (const rct3::park::path::RouteStep& step : *f.route)
            {
                trial.heading = step.heading;
                trial.slope = step.slope;
                if (!rct3::park::lay_path(trial, with, step.piece, style.name, id, step.piece.ground ? 0 : id + 1))
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
            std::vector<save::model::PlacedObject> shown;
            for (const std::size_t i : raised)
                shown.push_back(deck_object(style, with[i], with, scene.grid, with[i].elevation->scenery_ref));
            std::erase_if(shown, [&](const save::model::PlacedObject& o) { return !load_object_model(scene, f.models, assets, o); });
            if (const rct3::park::support::SupportSets* sets = support_sets(scene, assets); sets != nullptr && !shown.empty())
            {
                for (save::model::PlacedObject& o : supports_under(scene, shown, *sets))
                {
                    if (load_object_model(scene, f.models, assets, o))
                        shown.push_back(std::move(o));
                }
            }
            f.raised_ghost = rct3::park::object::object_mesh(shown, scene.models, scene.grid);
            for (rct3::park::GroundVertex& v : f.raised_ghost.vertices)
                v.colour *= tint;
            ghost_textures(device, scene, f.raised_ghost);
        }

        /// The next piece faint, as it will look once laid: its deck and supports when raised, the ground's
        /// pictures when on the ground, and the pieces beside it that it changes. Tinted when it cannot be laid.
        void build_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, bool allowed)
        {
            ParkFootpaths& f = scene.footpaths;
            // Pointing at a path's edge shows what would go on from there instead of the trail's next piece.
            const rct3::park::FootpathBuilder& b = f.hover ? *f.hover : f.builder;
            if (f.hover)
                allowed = rct3::park::lay_problem(b, scene.grid, f.paths, f.solids) == rct3::park::LayProblem::None;
            f.ghost = { };
            f.raised_ghost = { };
            f.arrow = { };
            f.under_ground = false;
            if (const auto next = rct3::park::next_piece(b, scene.grid); next && f.brush.empty())
            {
                f.arrow = rct3::park::heading_arrow(*next, b.heading, scene.grid);
                for (rct3::park::GroundVertex& v : f.arrow.vertices)
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
                std::vector<save::model::PathTile> with = f.paths;
                for (const save::model::Tile spot : plan->lay)
                    with.push_back(rct3::park::ground_path(spot, f.styles[*f.style].name, 0));
                f.ghost = ground_ghost(device, scene, assets, with, plan->lay, affordable ? kGhostTint : kRefusedTint);
                return;
            }
            if (f.route && !f.route->empty() && f.style && *f.style < f.styles.size())
            {
                f.under_ground = std::ranges::any_of(*f.route, [](const rct3::park::path::RouteStep& s) { return s.piece.tunnel; });
                route_ghost(device, scene, assets);
                return;
            }
            const auto piece = rct3::park::next_piece(b, scene.grid);
            f.under_ground = piece && piece->tunnel;
            if (!piece || !f.style || *f.style >= f.styles.size())
                return;
            const glm::vec4 tint = allowed ? kGhostTint : kRefusedTint;
            const rct3::park::path::PathStyle& style = f.styles[*f.style];
            std::vector<save::model::PathTile> with = f.paths;
            rct3::park::FootpathBuilder trial = b;
            const std::uint64_t id = unused_id(scene);
            (void)rct3::park::lay_path(trial, with, *piece, style.name, id, piece->ground ? 0 : id + 1);
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
            std::vector<save::model::PlacedObject> shown { deck_object(style, with.back(), with, scene.grid, id + 1) };
            for (const save::model::PathTile& p : std::span(with).first(with.size() - 1))
            {
                if (!p.elevation || rct3::park::ground::is_ground_queue(p) || std::abs(p.col - piece->spot.col) > 1 || std::abs(p.row - piece->spot.row) > 1)
                    continue;
                const rct3::park::path::PathStyle* own = style_named(f, p);
                const auto now = std::ranges::find(scene.objects, p.elevation->scenery_ref, &save::model::PlacedObject::id);
                if (own == nullptr || now == scene.objects.end() || now->hidden)
                    continue;
                save::model::PlacedObject next = deck_object(*own, p, with, scene.grid, 0);
                if (next.symbol != now->symbol || next.rotation != now->rotation)
                    shown.push_back(std::move(next));
            }
            std::erase_if(shown, [&](const save::model::PlacedObject& o) { return !load_object_model(scene, f.models, assets, o); });
            if (shown.empty() || shown.front().owner_ref != id)
                return;
            if (const rct3::park::support::SupportSets* sets = support_sets(scene, assets))
            {
                for (save::model::PlacedObject& o : supports_under(scene, std::span(&shown.front(), 1), *sets))
                {
                    if (load_object_model(scene, f.models, assets, o))
                        shown.push_back(std::move(o));
                }
            }
            f.raised_ghost = rct3::park::object::object_mesh(shown, scene.models, scene.grid);
            for (rct3::park::GroundVertex& v : f.raised_ghost.vertices)
                v.colour *= tint;
            ghost_textures(device, scene, f.raised_ghost);
        }

        /// Gives the raised paths around `spot` the pieces their neighbours call for, loading new models.
        /// Gives the raised pieces on the tiles from `low` to `high` and those beside them the pieces they call for.
        void refresh_area(ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Tile low, save::model::Tile high)
        {
            ParkFootpaths& f = scene.footpaths;
            if (rct3::park::path::refresh_decks(f.paths, scene.objects, scene.grid, f.styles, low.col - 1, low.row - 1, high.col + 1, high.row + 1, f.walls) == 0)
                return;
            for (const save::model::PlacedObject& o : scene.objects)
            {
                if (o.col >= low.col - 1 && o.col <= high.col + 1 && o.row >= low.row - 1 && o.row <= high.row + 1 && !o.hidden && !o.overlay.empty())
                    (void)load_object_model(scene, f.models, assets, o);
            }
        }

        void refresh_around(ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Tile spot)
        {
            refresh_area(scene, assets, spot, spot);
        }

        /// Hides `objects` of the scene's objects.
        void hide(ParkScene& scene, std::span<const std::uint64_t> objects)
        {
            for (save::model::PlacedObject& o : scene.objects)
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
                const auto tile = std::ranges::find(f.paths, id, &save::model::PathTile::id);
                if (tile == f.paths.end() || !tile->queue)
                    continue;
                std::vector<std::uint64_t>& railings = f.railings[id];
                hide(scene, railings);
                railings.clear();
                rct3::park::QueueFences fences = rct3::park::queue_fences(std::span(&*tile, 1), scene.grid, unused_id(scene));
                for (save::model::PlacedObject& o : fences.pieces)
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
        void sim_paths(ParkScene& scene, std::span<const save::model::PathTile> laid, std::span<const save::model::PathTile> gone)
        {
            for (const save::model::PathTile& p : gone)
                simulation::remove_path(scene.sim, p, scene.grid);
            for (const save::model::PathTile& p : laid)
                (void)simulation::add_path(scene.sim, p, scene.grid);
            (void)simulation::relink_attractions(scene.sim, scene.rides);
            simulation::import_pools(scene.sim, scene.pools, scene.scenery_pool_extras);
            // Boats keep out from under the paths laid low over their water.
            simulation::boat_water(scene.sim, scene.grid, scene.footpaths.water.bodies, boat_obstacles(scene));
        }

        /// Lays the paths of the stroke just ended.
        void lay_stroke(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            const auto plan = stroke_plan(scene);
            const std::vector<save::model::Tile> brush = std::exchange(f.brush, { });
            if (!plan || plan->lay.empty())
            {
                // A click where no path can lie picks where the trail starts instead, as raised paths may.
                if (brush.size() == 1)
                    rct3::park::start_at(f.builder, brush.front());
                return;
            }
            const std::int64_t cost = stroke_cost(f, *plan);
            if (!simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::Landscaping, false))
            {
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            wait_for_build(scene);
            std::uint64_t id = unused_id(scene);
            f.stroke.clear();
            for (const save::model::Tile spot : plan->lay)
            {
                f.paths.push_back(rct3::park::ground_path(spot, f.styles[*f.style].name, id));
                f.stroke.push_back(id++);
            }
            sim_paths(scene, std::span(f.paths).last(plan->lay.size()), { });
            float_money(scene, tile_point(scene.grid, plan->lay.back()), -cost);
            // The trail goes on from the last tile, the way the stroke last went.
            const save::model::Tile last = brush.back();
            if (brush.size() > 1)
            {
                const save::model::Tile before = brush[brush.size() - 2];
                f.builder.heading = last.row > before.row ? rct3::park::Side::kNextRow : last.col > before.col ? rct3::park::Side::kNextCol : last.row < before.row ? rct3::park::Side::kRowBefore : rct3::park::Side::kColBefore;
            }
            const auto there = rct3::park::ground_path_at(f.paths, last);
            if (!there || !rct3::park::path::start_from_edge(f.builder, f.paths[*there], scene.grid, f.builder.heading))
                rct3::park::start_at(f.builder, last);
            save::model::Tile low = last;
            save::model::Tile high = last;
            for (const save::model::Tile spot : plan->lay)
            {
                low = { std::min(low.col, spot.col), std::min(low.row, spot.row) };
                high = { std::max(high.col, spot.col), std::max(high.row, spot.row) };
            }
            refresh_area(scene, assets, low, high);
            objects_changed(scene);
            rebuild_ground(device, scene, assets, rct3::park::TileRect { low.col, low.row, high.col + 1, high.row + 1 });
        }

        /// Takes the paths of the last stroke away.
        void remove_stroke(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            wait_for_build(scene);
            const std::vector<std::uint64_t> gone = std::exchange(f.stroke, { });
            save::model::Tile low { scene.grid.width, scene.grid.height };
            save::model::Tile high { -1, -1 };
            std::vector<save::model::PathTile> taken;
            std::erase_if(f.paths, [&](const save::model::PathTile& p) {
                if (!std::ranges::contains(gone, p.id))
                    return false;
                taken.push_back(p);
                low = { std::min<int>(low.col, p.col), std::min<int>(low.row, p.row) };
                high = { std::max<int>(high.col, p.col), std::max<int>(high.row, p.row) };
                return true;
            });
            if (high.col < 0)
                return;
            for (const save::model::PathTile& p : taken)
                refund(scene, p);
            sim_paths(scene, { }, taken);
            refresh_area(scene, assets, low, high);
            objects_changed(scene);
            rebuild_ground(device, scene, assets, rct3::park::TileRect { low.col, low.row, high.col + 1, high.row + 1 });
        }

        /// Lays the next piece; `refresh` false leaves building the ground and the objects again to the caller.
        void lay(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, bool refresh = true)
        {
            ParkFootpaths& f = scene.footpaths;
            if (rct3::park::lay_problem(f.builder, scene.grid, f.paths, f.solids) != rct3::park::LayProblem::None)
                return;
            const rct3::park::path::PathStyle& style = f.styles[*f.style];
            const rct3::park::NextPiece piece = *rct3::park::next_piece(f.builder, scene.grid);
            const bool there = rct3::park::same_piece_at(f.paths, piece, f.builder).has_value();
            const std::int64_t cost = piece_price(style, piece, scene.grid);
            // Paid before the ground is cut; walking over the same piece costs nothing.
            if (!there && !simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::Landscaping, false))
            {
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            const std::uint64_t id = unused_id(scene);
            wait_for_build(scene);
            if (!there && piece.cut)
                rct3::park::cut_down(scene.grid, piece, f.builder.heading);
            if (!rct3::park::lay_path(f.builder, f.paths, piece, style.name, id, piece.ground ? 0 : id + 1))
                return;
            f.stroke.clear();
            f.route_laid = 0;
            f.last_height = piece.end;
            const glm::vec3 ground = tile_point(scene.grid, piece.spot);
            float_money(scene, { ground.x, static_cast<float>(std::max(piece.start, piece.end)), ground.z }, -cost);
            // The queue tile laid before may now open towards this one.
            std::vector<std::uint64_t> railed { f.paths.back().id };
            if (f.builder.trail.size() > 1 && f.builder.trail[f.builder.trail.size() - 2].laid != 0)
                railed.push_back(f.builder.trail[f.builder.trail.size() - 2].laid);
            if (!piece.ground)
            {
                const save::model::PathTile& laid = f.paths.back();
                save::model::PlacedObject deck = deck_object(style, laid, f.paths, scene.grid, id + 1);
                if (load_object_model(scene, f.models, assets, deck))
                {
                    scene.objects.push_back(std::move(deck));
                }
                else
                    logging::logger("park").warning("{}: no model for {}", style.overlay, deck.symbol);
            }
            if (f.builder.queue)
            {
                save::model::PathTile& laid = f.paths.back();
                laid.surface_colours = std::array<std::int32_t, 3> { };
                // A line's railing is its ride's fence; a queue of no ride yet has the plain one.
                if (laid.queue->fence_symbol.empty())
                {
                    laid.queue->fence_symbol = kQueueRailing;
                    laid.queue->fence_overlay = kQueueRailingOverlay;
                }
                const std::int32_t height = laid.elevation ? laid.elevation->quantised_height : piece.start;
                std::optional<std::uint64_t> line;
                if (const auto entrance = rct3::park::entrance_beside(scene.rides, piece.spot, height))
                {
                    line = rct3::park::join_entrance(scene.rides, f.paths, scene.grid, f.paths.size() - 1, *entrance, unused_id(scene));
                    logging::logger("park").info("queue joins ride {} on line {}", entrance->ride, *line);
                }
                else
                    line = rct3::park::join_line(scene.rides, f.paths, scene.grid, f.paths.size() - 1);
                for (const save::model::PathTile& p : f.paths)
                {
                    if (line && p.queue && p.queue->queue_line_ref == *line && !std::ranges::contains(railed, p.id))
                        railed.push_back(p.id);
                }
            }
            // A queue's open end bends to a path beside it: the queue just laid, or those beside the path just laid.
            bool turned = false;
            const save::model::PathTile laid = f.paths.back();
            for (std::size_t i = 0; i < f.paths.size(); ++i)
            {
                const bool candidate = f.builder.queue ? i + 1 == f.paths.size() : f.paths[i].queue && std::abs(f.paths[i].col - laid.col) + std::abs(f.paths[i].row - laid.row) == 1;
                if (candidate && rct3::park::turn_to_path(f.paths, scene.grid, i))
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
                if (const auto at = std::ranges::find(f.paths, changed, &save::model::PathTile::id); at != f.paths.end())
                    refresh_around(scene, assets, { at->col, at->row });
            }
            std::vector<save::model::PathTile> changed;
            std::ranges::copy_if(f.paths, std::back_inserter(changed), [&](const save::model::PathTile& p) { return std::ranges::contains(railed, p.id); });
            sim_paths(scene, changed, { });
            if (!refresh)
                return;
            objects_changed(scene);
            // A ground queue's picture follows its open sides, which laying the next tile changes.
            if (piece.ground || piece.cut || piece.tunnel || f.builder.queue)
                rebuild_ground(device, scene, assets, rct3::park::TileRect { piece.spot.col, piece.spot.row, piece.spot.col + 1, piece.spot.row + 1 }.grown(1));
        }

        /// Takes away what draws and holds up `removed`, just taken out of the paths; `also` as refund takes it.
        void take_away(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const save::model::PathTile& removed, std::int64_t also = 0)
        {
            ParkFootpaths& f = scene.footpaths;
            refund(scene, removed, also);
            wait_for_build(scene);
            std::vector<std::uint64_t> gone = std::move(f.railings[removed.id]);
            f.railings.erase(removed.id);
            if (removed.elevation)
                gone.push_back(removed.elevation->scenery_ref);
            hide(scene, gone);
            sim_paths(scene, { }, std::span(&removed, 1));
            if (removed.queue && removed.queue->queue_line_ref != 0)
            {
                for (save::model::Ride& ride : scene.rides.rides)
                {
                    for (save::model::Station& station : ride.stations)
                    {
                        if (station.queue_line == removed.queue->queue_line_ref)
                            station.queue_tiles = std::max(station.queue_tiles - 1, 0);
                    }
                }
            }
            // A raised piece beside may lose a joined side.
            refresh_around(scene, assets, { removed.col, removed.row });
            objects_changed(scene);
            if (!removed.elevation || rct3::park::ground::is_ground_queue(removed) || removed.elevation->underground)
                rebuild_ground(device, scene, assets, rct3::park::TileRect { removed.col, removed.row, removed.col + 1, removed.row + 1 });
        }

        /// Takes the last piece of the trail away, with what draws and holds it up.
        void remove(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            if (const auto removed = rct3::park::remove_last(scene.footpaths.builder, scene.footpaths.paths))
                take_away(device, scene, assets, *removed);
        }

        /// Takes paths `ids` away, the trail backing up over its own (rct3::park::remove_paths).
        void take_paths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::uint64_t> ids, std::int64_t also = 0)
        {
            ParkFootpaths& f = scene.footpaths;
            // Indices into the paths may point at what is gone, and the last stroke or route may be part of it.
            f.hover.reset();
            f.hover_edge.reset();
            f.stroke.clear();
            f.route_laid = 0;
            // shortcut: builds the ground again for each path taken; once per drag if long drags are slow.
            for (const save::model::PathTile& gone : rct3::park::remove_paths(f.builder, f.paths, ids))
                take_away(device, scene, assets, gone, std::exchange(also, 0));
        }

        std::u16string problem_text(const ui::InterfaceAssets& assets, rct3::park::LayProblem problem)
        {
            switch (problem)
            {
                case rct3::park::LayProblem::NoStart:
                    return std::u16string(assets.strings->get(kPickStart));
                case rct3::park::LayProblem::OffMap:
                    return std::u16string(assets.strings->get(kOffMap));
                case rct3::park::LayProblem::NotOwned:
                    return std::u16string(assets.strings->get(kNotOwned));
                case rct3::park::LayProblem::Underground:
                    return std::u16string(assets.strings->get(kUnderground));
                case rct3::park::LayProblem::Occupied:
                    return std::u16string(assets.strings->get(kOccupied));
                case rct3::park::LayProblem::Blocked:
                    return std::u16string(assets.strings->get(kBlocked));
                case rct3::park::LayProblem::None:
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
                simulation::park::cannot_pay(scene.park);
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            const rct3::park::PathSlope slope = f.builder.slope;
            const std::size_t before = f.builder.trail.size();
            for (const rct3::park::path::RouteStep& step : *route)
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

    void wall_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const rct3::park::EdgeWall& wall, bool standing)
    {
        scene.footpaths.walls = rct3::park::edge_walls(scene.objects, scene.models, scene.grid);
        simulation::set_wall(scene.sim, wall, standing);
        refresh_around(scene, assets, wall.tile);
        objects_changed(scene);
        rebuild_ground(device, scene, assets, rct3::park::TileRect { wall.tile.col, wall.tile.row, wall.tile.col + 1, wall.tile.row + 1 });
    }

    void read_path_styles(ParkFootpaths& f, const ui::InterfaceAssets& assets)
    {
        if (!f.styles.empty() || !assets.install)
            return;
        for (const std::string_view folder : { rct3::park::path::kPathStyleFolder, rct3::park::path::kQueueStyleFolder })
        {
            if (const auto found = assets.install->find(folder))
            {
                std::vector<rct3::park::path::PathStyle> more = rct3::park::path::folder_path_styles(*found);
                f.styles.insert(f.styles.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
            }
        }
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
            read_path_styles(f, assets);
            logging::logger("park").info("{} path and queue styles, {} pictures", f.styles.size(), skins);
            const auto first = std::ranges::find_if(f.styles, [&](const rct3::park::path::PathStyle& s) { return !s.queue && offered(scene.build.offer, s); });
            if (first != f.styles.end())
                f.style = static_cast<std::size_t>(first - f.styles.begin());
        }
        // Styles invented while the panel is shut show when it opens next.
        auto [paths, queues] = panel_styles(f, scene.build.offer, assets, options);
        panel->show(std::move(paths), std::move(queues));
        panel->set_chosen(pick_of(f, scene.build.offer));
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
            f.style = style_of(f, scene.build.offer, *pick);
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
        if (panel->brush() != f.brush_size)
        {
            f.brush_size = panel->brush();
            f.dirty = true;
        }
        if (panel->straight() != f.straight)
        {
            f.straight = panel->straight();
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
        if (const auto control = panel->take_control(); control && *control < rct3::park::kPathControlCount)
        {
            f.notice.clear();
            const rct3::park::PathStep step = rct3::park::press(f.builder, static_cast<rct3::park::PathControl>(*control));
            if (step == rct3::park::PathStep::Lay)
                lay(device, scene, assets);
            else if (step == rct3::park::PathStep::Remove && f.route_laid > 0)
            {
                for (std::size_t n = std::exchange(f.route_laid, 0); n > 0; --n)
                    remove(device, scene, assets);
            }
            else if (step == rct3::park::PathStep::Remove && !f.stroke.empty())
                remove_stroke(device, scene, assets);
            else if (step == rct3::park::PathStep::Remove)
                remove(device, scene, assets);
            f.dirty = true;
        }

        // shortcut: worked out again only when objects come or go, not when one is moved or hidden; also on
        // those once objects can move while the path panel is open.
        if (f.solids_of != scene.objects.size())
        {
            f.solids = rct3::park::path::solid_tiles(scene.objects, scene.models, scene.grid);
            f.solids_of = scene.objects.size();
        }
        const rct3::park::LayProblem problem = rct3::park::lay_problem(f.builder, scene.grid, f.paths, f.solids);
        if (const auto next = rct3::park::next_piece(f.builder, scene.grid); panel->following() && next && next->spot != f.followed)
        {
            f.followed = next->spot;
            f.camera_goal = glm::vec3 { scene.grid.world_x(next->spot.col) + scene.grid.tile_size_x / 2, static_cast<float>(next->start), scene.grid.world_z(next->spot.row) + scene.grid.tile_size_z / 2 };
        }
        else if (!panel->following())
            f.camera_goal.reset();
        // Once a trail is started only the controls that lead to a piece that fits; before, any slope may be
        // picked for the first piece.
        std::array<bool, ui::screens::kFootpathControls> usable { };
        if (f.builder.start)
            usable = rct3::park::fitting_controls(f.builder, scene.grid, f.paths, f.solids);
        else
        {
            usable.fill(true);
            usable.at(static_cast<std::size_t>(rct3::park::PathControl::TurnLeft)) = rct3::park::can_turn(f.builder);
            usable.at(static_cast<std::size_t>(rct3::park::PathControl::TurnRight)) = rct3::park::can_turn(f.builder);
            usable.at(static_cast<std::size_t>(rct3::park::PathControl::Straight)) = problem == rct3::park::LayProblem::None;
        }
        usable.at(static_cast<std::size_t>(rct3::park::PathControl::Remove)) = !f.builder.trail.empty() || !f.stroke.empty() || f.route_laid > 0;
        panel->set_controls(usable, static_cast<std::size_t>(rct3::park::slope_control(f.builder.slope)), f.builder.cut_into_ground);

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
            const auto piece = rct3::park::next_piece(f.builder, scene.grid);
            const std::int64_t cost = piece ? piece_price(f.styles[*f.style], *piece, scene.grid) : next_cost(f.styles[*f.style]);
            status = name_of(assets, f.styles[*f.style]) + u"  " + ui::money_text(currency_of(assets, options), cost);
        }
        panel->set_status(std::move(status));

        if (f.dirty)
        {
            build_ghost(device, scene, assets, problem == rct3::park::LayProblem::None);
            f.dirty = false;
        }
    }

    namespace
    {
        /// The view's ray through `at` while the path panel shows its controls.
        std::optional<rct3::park::Ray> controls_ray(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            auto panel = panel_of(scene);
            if (!panel || !panel->showing() || panel->page() != ui::screens::FootpathPage::Controls || size.x <= 0 || size.y <= 0)
                return std::nullopt;
            const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
            return rct3::park::view_ray(scene.camera, size.x / size.y, ndc);
        }

        /// The tile of the ground under `at` while the path panel shows its controls.
        std::optional<save::model::Tile> controls_tile(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            const auto ray_at = controls_ray(scene, at, size);
            if (!ray_at)
                return std::nullopt;
            const rct3::park::Ray& ray = *ray_at;
            const auto distance = rct3::park::object::ground_hit(ray, scene.grid);
            if (!distance)
                return std::nullopt;
            const glm::vec3 hit = ray.origin + ray.direction * *distance;
            const auto col = static_cast<int>(std::floor((hit.x - scene.grid.origin_x) / scene.grid.tile_size_x));
            const auto row = static_cast<int>(std::floor((hit.z - scene.grid.origin_z) / scene.grid.tile_size_z));
            if (!scene.grid.contains(col, row))
                return std::nullopt;
            return save::model::Tile { col, row };
        }

        /// Begins a trail at `tile`: on its ground, or with `control` at the last piece's height.
        void begin_trail(ParkFootpaths& f, const save::model::RideModel& rides, save::model::Tile tile, bool control)
        {
            rct3::park::start_at(f.builder, tile);
            if (control)
                f.builder.start_height = f.last_height;
            // A queue begun beside a ride's entrance goes away from it, so it leads guests there.
            if (chosen_queue(f))
            {
                if (const auto entrance = rct3::park::entrance_beside(rides, { tile.col, tile.row }, std::nullopt))
                    f.builder.heading = entrance->side.opposite();
            }
            f.notice.clear();
            f.dirty = true;
        }

        /// The rides' entrances and exits.
        std::vector<save::model::PlacementSpot> ride_doors(const save::model::RideModel& rides)
        {
            std::vector<save::model::PlacementSpot> doors;
            for (const save::model::Ride& ride : rides.rides)
            {
                for (const save::model::Station& station : ride.stations)
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
                const auto steps = static_cast<int>(std::lround((*f.slope_press - at.y) / rct3::park::kSlopeDragStep));
                if (const rct3::park::PathSlope slope = rct3::park::stepped_slope(f.slope_before, steps); slope != f.builder.slope)
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
            const auto under = ray ? rct3::park::path::path_under(*ray, f.paths, scene.grid, ground_faded(scene)) : std::nullopt;
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
                const auto distance = rct3::park::object::ground_hit(*ray, scene.grid);
                if (tile && distance)
                {
                    // The trail's next piece pointed at is picked up where it is; elsewhere a trail begins.
                    if (const auto next = rct3::park::next_piece(f.builder, scene.grid); next && next->spot == *tile)
                    {
                        rct3::park::start_at(f.builder, *tile);
                        f.builder.start_height = next->start;
                    }
                    else
                        begin_trail(f, scene.rides, *tile, control);
                    f.lift = ParkFootpaths::Lift { ray->origin + ray->direction * *distance, rct3::park::next_piece(f.builder, scene.grid)->start };
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
                if (const auto under = rct3::park::path::path_under(*ray, f.paths, scene.grid, ground_faded(scene)))
                {
                    const save::model::PathTile& p = f.paths[under->path];
                    tile = save::model::Tile { p.col, p.row };
                    if (p.elevation && !rct3::park::ground::is_ground_queue(p))
                        height = p.elevation->quantised_height;
                }
            }
            const bool on_arrow = tile && tile == rct3::park::next_spot(f.builder);
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
            std::optional<std::pair<std::size_t, rct3::park::Side>> edge;
            std::optional<std::array<std::int32_t, 4>> door;
            // While a trail goes on its arrow holds still: nothing else pointed at shows.
            const bool held = f.builder.start.has_value();
            if (const auto ray = over || held ? std::nullopt : controls_ray(scene, at, size))
            {
                const std::vector<save::model::PlacementSpot> doors = ride_doors(scene.rides);
                if (const auto d = rct3::park::path::door_under(*ray, doors, scene.grid, ground_faded(scene)))
                {
                    const save::model::PlacementSpot& spot = doors[d->door];
                    door = std::array { spot.col, spot.row, spot.height, d->side.value };
                }
                else if (const auto under = rct3::park::path::path_under(*ray, f.paths, scene.grid, ground_faded(scene)); under && under->side)
                    edge = std::pair { under->path, *under->side };
            }
            if (edge != f.hover_edge || door != f.hover_door)
            {
                f.hover_edge = edge;
                f.hover_door = door;
                f.hover.reset();
                rct3::park::FootpathBuilder b = f.builder;
                if (edge && rct3::park::path::start_from_edge(b, f.paths[edge->first], scene.grid, edge->second))
                    f.hover = std::move(b);
                else if (door)
                {
                    rct3::park::path::start_from_door(b, save::model::PlacementSpot { 0, (*door)[0], (*door)[1], (*door)[2], 0 }, rct3::park::Side::from((*door)[3]));
                    f.hover = std::move(b);
                }
                f.dirty = true;
            }
            // Bare ground pointed at, away from the trail's next piece, which has its own arrow.
            const std::optional<save::model::Tile> tile = edge || door || over || held ? std::nullopt : controls_tile(scene, at, size);
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
            const bool on_next = f.builder.start && tile && tile == rct3::park::next_spot(f.builder);
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
        for (const save::model::Tile spot : rct3::park::path::brush_line(f.brush.back(), *tile))
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
        const auto under = ray ? rct3::park::path::path_under(*ray, f.paths, scene.grid, ground_faded(scene)) : std::nullopt;
        if (!under)
            return false;
        f.sell = f.paths[under->path].id;
        return true;
    }

    std::optional<std::uint64_t> footpath_at(const ParkScene& scene, ui::Point at, ui::Point size)
    {
        if (size.x <= 0 || size.y <= 0)
            return std::nullopt;
        const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
        const auto under = rct3::park::path::path_under(rct3::park::view_ray(scene.camera, size.x / size.y, ndc), scene.footpaths.paths, scene.grid, ground_faded(scene));
        return under ? std::optional(scene.footpaths.paths[under->path].id) : std::nullopt;
    }

    void remove_footpaths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::uint64_t> ids, std::int64_t also)
    {
        if (ids.empty())
            return;
        read_path_styles(scene.footpaths, assets);
        take_paths(device, scene, assets, ids, also);
        scene.footpaths.dirty = true;
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
        /// Draws `ghost` with the ground's pictures, or all of it with picture `only` when given (none when the scene
        /// has no such picture), as a ghost unless `steady`.
        void draw_ground_ghost(render::Device& device, const ParkScene& scene, const rct3::park::TerrainMesh& ghost, std::optional<std::string_view> only, const glm::mat4& transform, const render::SceneLight& light, bool steady = false)
        {
            if (ghost.indices.empty())
                return;
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(ghost.vertices)));
            const auto indices = device.allocate_transient(std::as_bytes(std::span(ghost.indices)));
            if (!vertices || !indices)
                return;
            for (const rct3::park::GroundBatch& batch : ghost.batches)
            {
                const auto texture = only ? scene.ground_textures.find(*only) : scene.ground_textures.find(rct3::park::ground::ground_key(batch.paint, batch.index));
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
                draw.light = steady ? light : ghost_light(light, scene.clock);
                draw.light.cut_clear = batch.paint == rct3::park::GroundPaint::PathA || batch.paint == rct3::park::GroundPaint::PathB || batch.paint == rct3::park::GroundPaint::Queue;
                device.draw(draw);
            }
        }
    } // namespace

    void draw_build_arrow(render::Device& device, const ParkScene& scene, const rct3::park::TerrainMesh& arrow, std::string_view picture, const glm::mat4& transform)
    {
        // In its own colours whatever the hour: no sun, no shade, no blinking.
        const render::SceneLight flat { .sky = glm::vec3(1), .ground = glm::vec3(1) };
        draw_ground_ghost(device, scene, arrow, picture, transform, flat, true);
    }

    bool ground_faded(const ParkScene& scene)
    {
        return scene.see_through_ground || scene.footpaths.under_ground;
    }

    void draw_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        draw_ground_ghost(device, scene, scene.footpaths.ghost, std::nullopt, transform, light);
        if (scene.ground_textures.contains(rct3::park::ground::kPathArrowPicture))
            draw_ground_ghost(device, scene, scene.footpaths.arrow, rct3::park::ground::kPathArrowPicture, transform, light);
    }

    void draw_raised_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        draw_ghost_mesh(device, scene, scene.footpaths.raised_ghost, transform, light);
    }

} // namespace rct3::scene::park
