// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_footpaths.hpp"

#include "config/game_options.hpp"
#include "logging/log.hpp"
#include "park/camera.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/ground_textures.hpp"
#include "park/object_pick.hpp"
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
        constexpr std::string_view kGroundOnly = "Rct3pp_Paths_GroundOnly";
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

        /// The price of the next piece: every piece laid now lies on the ground.
        std::int64_t next_cost(const park::PathStyle& style)
        {
            return park::piece_cost(style.piece_cost, false, 0);
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
            park::add_ground(mesh, park::surrounding_land(scene.grid));
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
            const auto spot = park::next_spot(f.builder);
            if (!spot || !scene.grid.contains(spot->col, spot->row) || !f.style || *f.style >= f.styles.size() || f.styles[*f.style].queue)
                return;
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
                v.colour = allowed ? kGhostTint : kRefusedTint;
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

        void lay(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            ParkFootpaths& f = scene.footpaths;
            if (park::lay_problem(f.builder, scene.grid, chosen_queue(f)) != park::LayProblem::None)
                return;
            const park::PathStyle& style = f.styles[*f.style];
            const std::int64_t cost = next_cost(style);
            const auto spot = park::next_spot(f.builder);
            const bool there = spot && park::ground_path_at(f.paths, *spot);
            if (!there && scene.park.cash && *scene.park.cash < cost)
            {
                f.notice = std::u16string(assets.strings->get(kNoMoney));
                return;
            }
            if (!park::lay_ground_path(f.builder, f.paths, style.name, unused_id(scene)))
                return;
            if (scene.park.cash)
                *scene.park.cash -= cost;
            rebuild_ground(device, scene, assets);
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
                case park::LayProblem::Raised:
                    return std::u16string(assets.strings->get(kGroundOnly));
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
    }

    void update_footpaths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const config::GameOptions& options)
    {
        ui::FootpathPanel* panel = panel_of(scene);
        ParkFootpaths& f = scene.footpaths;
        if (panel == nullptr || !panel->showing())
        {
            if (panel != nullptr && panel->take_closed())
                f.ghost = { };
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
            else if (step == park::PathStep::Remove && park::remove_last(f.builder, f.paths))
                rebuild_ground(device, scene, assets);
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
            status = name_of(assets, f.styles[*f.style]) + u"  " + ui::money_text(currency_of(assets, options), next_cost(f.styles[*f.style]));
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

} // namespace rct3::scene
