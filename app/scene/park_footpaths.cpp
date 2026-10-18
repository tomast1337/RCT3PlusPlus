// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_footpaths.hpp"

#include "config/game_options.hpp"
#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/ground_textures.hpp"
#include "park/object_pick.hpp"
#include "park/path_decks.hpp"
#include "park/path_supports.hpp"
#include "park/queue_fences.hpp"
#include "park/relief_maps.hpp"
#include "park/surrounding_land.hpp"
#include "scene/park_scene.hpp"
#include "ui/extra_skins.hpp"
#include "ui/money_text.hpp"
#include "ui/ui_textures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace rct3::scene
{

    namespace
    {

        constexpr std::array<std::string_view, 2> kIconFiles { park::kPathIconFile, park::kQueueIconFile };
        constexpr glm::vec4 kGhostTint { 1.0f, 1.0f, 1.0f, 0.6f };
        constexpr glm::vec4 kRefusedTint { 1.0f, 0.3f, 0.3f, 0.6f };
        /// Of the next piece over the paths already laid, so the two never fight for the same depth.
        constexpr float kGhostLift = 0.02f;
        constexpr std::string_view kNoMoney = "PlaceObject_NotEnoughMoney";
        constexpr std::string_view kPickStart = "Rct3pp_Paths_PickStart";
        constexpr std::string_view kNotOwned = "Rct3pp_Paths_NotOwned";
        constexpr std::string_view kOffMap = "Rct3pp_Paths_OffMap";
        constexpr std::string_view kUnderground = "Rct3pp_Paths_Underground";
        constexpr std::string_view kNoQueues = "Rct3pp_Paths_NoQueues";

        ui::FootpathPanel* panel_of(const ParkScene& scene)
        {
            return static_cast<ui::FootpathPanel*>(scene.desktop->root().find(kFootpathPanelName));
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
            return park::piece_cost(style.piece_cost, !piece.ground, park::height_above_ground(piece, grid));
        }

        /// The object drawing raised path `path` of `style`, with the piece the paths around it call for.
        save::PlacedObject deck_object(const park::PathStyle& style, const save::PathTile& path, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, std::uint64_t id)
        {
            const park::Deck deck = park::choose_deck(path, paths, grid);
            const save::PathElevation& e = *path.elevation;
            save::PlacedObject o;
            o.id = id;
            o.owner_ref = path.id;
            o.symbol = park::deck_symbol(style, deck.piece);
            o.overlay = style.overlay;
            o.col = path.col;
            o.row = path.row;
            // Stands on its low end: a piece going down is entered at its top.
            o.base_height = e.quantised_height - (park::slope_falls(e.slope) ? static_cast<std::int32_t>(park::path_rise(e.slope)) : 0);
            o.rotation = deck.turns;
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
            if (images.empty())
                return;
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

        /// Builds the ground again with the paths as they are now.
        void rebuild_ground(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            park::TerrainMesh mesh = park::terrain_mesh(scene.grid, f.water);
            const park::GroundPathMap paths = park::ground_path_map(f.paths, scene.grid);
            park::add_ground(mesh, park::ground_path_mesh(paths, scene.grid));
            park::add_ground(mesh, park::ground_queues(f.paths, scene.grid).mesh);
            park::add_ground(mesh, park::surrounding_land(scene.grid, f.water));
            picture_styles(device, scene, assets, paths.styles);
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
        }

        /// The next piece alone, faint over its tile, tinted when it cannot be laid.
        void build_ghost(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, bool allowed)
        {
            ParkFootpaths& f = scene.footpaths;
            f.ghost = { };
            f.raised_ghost = { };
            const auto piece = park::next_piece(f.builder, scene.grid);
            if (!piece || !f.style || *f.style >= f.styles.size() || f.styles[*f.style].queue)
                return;
            const glm::vec4 tint = allowed ? kGhostTint : kRefusedTint;
            if (!piece->ground)
            {
                std::vector<save::PathTile> with = f.paths;
                park::FootpathBuilder trial = f.builder;
                (void)park::lay_path(trial, with, *piece, f.styles[*f.style].name, 0, 0);
                if (with.size() == f.paths.size())
                    return; // The same piece is already there
                const std::array<save::PlacedObject, 1> one { deck_object(f.styles[*f.style], with.back(), with, scene.grid, 0) };
                if (!load_object_model(scene, f.models, assets, one[0]))
                    return;
                f.raised_ghost = park::object_mesh(one, scene.models, scene.grid);
                for (park::GroundVertex& v : f.raised_ghost.vertices)
                    v.colour *= tint;
                ghost_textures(device, scene, f.raised_ghost);
                return;
            }
            const auto spot = std::optional(piece->spot);
            // Numbered as the ground would number it once laid, so its pictures are those the ground uses.
            std::vector<save::PathTile> with = f.paths;
            with.push_back(park::ground_path(*spot, f.styles[*f.style].name, 0));
            park::GroundPathMap map = park::ground_path_map(with, scene.grid);
            picture_styles(device, scene, assets, map.styles);
            const auto index = std::ranges::find(map.styles, f.styles[*f.style].name) - map.styles.begin();
            std::ranges::fill(map.tiles, park::kNoPath);
            map.raised.clear();
            map.queues.clear();
            map.tiles[static_cast<std::size_t>(spot->row) * static_cast<std::size_t>(map.width) + static_cast<std::size_t>(spot->col)] = static_cast<std::int16_t>(index);
            f.ghost = park::ground_path_mesh(map, scene.grid);
            for (park::GroundVertex& v : f.ghost.vertices)
            {
                v.colour = tint;
                v.position.y += kGhostLift;
            }
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

        /// Gives the raised paths around `spot` the pieces their neighbours call for, loading new models.
        void refresh_around(ParkScene& scene, const ui::InterfaceAssets& assets, park::PathSpot spot)
        {
            ParkFootpaths& f = scene.footpaths;
            if (park::refresh_decks(f.paths, scene.objects, scene.grid, f.styles, spot.col - 1, spot.row - 1, spot.col + 1, spot.row + 1) == 0)
                return;
            for (const save::PlacedObject& o : scene.objects)
            {
                if (std::abs(o.col - spot.col) <= 1 && std::abs(o.row - spot.row) <= 1 && !o.hidden && !o.overlay.empty())
                    (void)load_object_model(scene, f.models, assets, o);
            }
        }

        /// Adds the supports under raised path `path`, which stands alone on its tile.
        void hold_up(ParkScene& scene, const ui::InterfaceAssets& assets, const save::PathTile& path)
        {
            ParkFootpaths& f = scene.footpaths;
            if (!f.support_sets && assets.install)
            {
                if (const auto found = assets.install->find(std::string(park::kSupportOverlay) + ".common.ovl"))
                {
                    if (auto sets = park::load_support_sets(*found))
                        f.support_sets = std::move(*sets);
                    else
                        logging::logger("park").warning("supports: {}", sets.error());
                }
            }
            if (!f.support_sets)
                return;
            // shortcut: only the new piece and paths on the ground of its tile are looked at, so a raised piece
            // laid over a lower one gets its own supports through it; look at the whole tile when that shows.
            std::vector<save::PathTile> here { path };
            std::ranges::copy_if(f.paths, std::back_inserter(here), [&](const save::PathTile& p) { return p.col == path.col && p.row == path.row && !p.elevation; });
            std::vector<save::PlacedObject> built = park::path_supports(here, scene.objects, scene.models, scene.grid, *f.support_sets);
            std::vector<std::uint64_t>& held = f.supports[path.id];
            for (save::PlacedObject& o : built)
            {
                if (!load_object_model(scene, f.models, assets, o))
                    continue;
                held.push_back(o.id);
                scene.objects.push_back(std::move(o));
            }
        }

        void lay(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            if (park::lay_problem(f.builder, scene.grid, chosen_queue(f)) != park::LayProblem::None)
                return;
            const park::PathStyle& style = f.styles[*f.style];
            const park::NextPiece piece = *park::next_piece(f.builder, scene.grid);
            const bool there = park::same_piece_at(f.paths, piece, f.builder.heading, f.builder.slope).has_value();
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
            if (scene.park.cash)
                *scene.park.cash -= cost;
            if (!piece.ground)
            {
                const save::PathTile& laid = f.paths.back();
                save::PlacedObject deck = deck_object(style, laid, f.paths, scene.grid, id + 1);
                if (load_object_model(scene, f.models, assets, deck))
                {
                    scene.objects.push_back(std::move(deck));
                    hold_up(scene, assets, laid);
                }
                else
                    logging::logger("park").warning("{}: no model for {}", style.overlay, deck.symbol);
            }
            refresh_around(scene, assets, piece.spot);
            objects_changed(scene);
            if (piece.ground || piece.cut)
                rebuild_ground(device, scene, assets);
        }

        /// Takes the last piece of the trail away, with what draws and holds it up.
        void remove(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            const auto removed = park::remove_last(f.builder, f.paths);
            if (!removed)
                return;
            if (removed->elevation)
            {
                wait_for_build(scene);
                std::vector<std::uint64_t> gone = std::move(f.supports[removed->id]);
                f.supports.erase(removed->id);
                gone.push_back(removed->elevation->scenery_ref);
                for (save::PlacedObject& o : scene.objects)
                {
                    if (std::ranges::contains(gone, o.id))
                        o.hidden = true;
                }
                refresh_around(scene, assets, { removed->col, removed->row });
                objects_changed(scene);
                return;
            }
            rebuild_ground(device, scene, assets);
            // A raised piece beside may lose a joined side.
            wait_for_build(scene);
            refresh_around(scene, assets, { removed->col, removed->row });
            objects_changed(scene);
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
                case park::LayProblem::Queue:
                    return std::u16string(assets.strings->get(kNoQueues));
                case park::LayProblem::None:
                    break;
            }
            return { };
        }

    } // namespace

    void toggle_footpath_panel(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options)
    {
        ui::FootpathPanel* panel = panel_of(scene);
        if (panel == nullptr)
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
            const auto first = std::ranges::find_if(f.styles, [](const park::PathStyle& s) { return !s.queue; });
            if (first != f.styles.end())
                f.style = static_cast<std::size_t>(first - f.styles.begin());
        }
        auto [paths, queues] = panel_styles(f, assets, options);
        panel->show(std::move(paths), std::move(queues));
        panel->set_chosen(pick_of(f));
        f.dirty = true;
    }

    void close_footpath_panel(ParkScene& scene)
    {
        if (ui::FootpathPanel* panel = panel_of(scene); panel != nullptr && panel->showing())
            panel->hide();
        scene.footpaths.ghost = { };
        scene.footpaths.raised_ghost = { };
    }

    void update_footpaths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const config::GameOptions& options)
    {
        ui::FootpathPanel* panel = panel_of(scene);
        ParkFootpaths& f = scene.footpaths;
        if (panel == nullptr || !panel->showing())
        {
            if (panel != nullptr && panel->take_closed())
            {
                f.ghost = { };
                f.raised_ghost = { };
            }
            return;
        }
        if (const auto pick = panel->take_style())
        {
            f.style = style_of(f, *pick);
            f.notice.clear();
            f.dirty = true;
        }
        if (const auto control = panel->take_control(); control && *control < park::kPathControlCount)
        {
            f.notice.clear();
            const park::PathStep step = park::press(f.builder, static_cast<park::PathControl>(*control));
            if (step == park::PathStep::Lay)
                lay(device, scene, assets);
            else if (step == park::PathStep::Remove)
                remove(device, scene, assets);
            f.dirty = true;
        }

        const park::LayProblem problem = park::lay_problem(f.builder, scene.grid, chosen_queue(f));
        std::array<bool, ui::kFootpathControls> usable { };
        usable.fill(true);
        const bool turn = park::can_turn(f.builder);
        usable.at(static_cast<std::size_t>(park::PathControl::TurnLeft)) = turn;
        usable.at(static_cast<std::size_t>(park::PathControl::TurnRight)) = turn;
        usable.at(static_cast<std::size_t>(park::PathControl::Straight)) = problem == park::LayProblem::None;
        usable.at(static_cast<std::size_t>(park::PathControl::Remove)) = !f.builder.trail.empty();
        panel->set_controls(usable, static_cast<std::size_t>(park::slope_control(f.builder.slope)), f.builder.cut_into_ground);

        std::u16string status = f.notice;
        if (status.empty())
            status = problem_text(assets, problem);
        if (status.empty() && f.style && *f.style < f.styles.size())
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

    bool footpath_click(ParkScene& scene, ui::Point at, ui::Point size)
    {
        const ui::FootpathPanel* panel = panel_of(scene);
        if (panel == nullptr || !panel->showing() || panel->page() != ui::FootpathPage::Controls || size.x <= 0 || size.y <= 0)
            return false;
        const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
        const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, ndc);
        const auto distance = park::ground_hit(ray, scene.grid);
        if (!distance)
            return false;
        const glm::vec3 hit = ray.origin + ray.direction * *distance;
        const auto col = static_cast<int>(std::floor((hit.x - scene.grid.origin_x) / scene.grid.tile_size_x));
        const auto row = static_cast<int>(std::floor((hit.z - scene.grid.origin_z) / scene.grid.tile_size_z));
        if (!scene.grid.contains(col, row))
            return false;
        park::start_at(scene.footpaths.builder, { col, row });
        scene.footpaths.notice.clear();
        scene.footpaths.dirty = true;
        return true;
    }

    void draw_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        const park::TerrainMesh& ghost = scene.footpaths.ghost;
        if (ghost.indices.empty())
            return;
        const auto vertices = device.allocate_transient(std::as_bytes(std::span(ghost.vertices)));
        const auto indices = device.allocate_transient(std::as_bytes(std::span(ghost.indices)));
        if (!vertices || !indices)
            return;
        for (const park::GroundBatch& batch : ghost.batches)
        {
            const auto texture = scene.ground_textures.find(park::ground_key(batch.paint, batch.index));
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
            draw.light = light;
            device.draw(draw);
        }
    }

    void draw_raised_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light)
    {
        draw_ghost_mesh(device, scene, scene.footpaths.raised_ghost, transform, light);
    }

} // namespace rct3::scene
