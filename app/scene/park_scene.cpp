// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_scene.hpp"

#include "logging/log.hpp"
#include "park/asset_index.hpp"
#include "park/ground_textures.hpp"
#include "park/object_models.hpp"
#include "park/sky_models.hpp"
#include "save/model/tutorial.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "ui/game_style.hpp"
#include "ui/scale.hpp"
#include "ui/screens/tutorial_guide.hpp"
#include "ui/widgets/question_box.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        /// The game's ground pictures; the later file adds the expansion's.
        constexpr std::array<std::string_view, 2> kGroundFiles = {
            "terrain/RCT3/Terrain_RCT3.common.ovl",
            "terrain/CT/Terrain_CT.common.ovl",
        };

        constexpr std::string_view kGuideName = "tutorial";
        constexpr std::string_view kQuestionName = "leave_tutorial";
        constexpr std::string_view kLeaveText = "TutorialWindow_ConfirmQuit";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";

        // How far the controls move the camera.
        constexpr float kDragPan = 0.0025f; ///< Per pixel, times the distance
        constexpr float kDragTurn = 0.006f; ///< Radians per pixel
        constexpr float kKeyPan = 0.8f;     ///< Per second, times the distance
        constexpr float kWheelZoom = 0.85f; ///< Distance times this per notch towards the screen
        constexpr float kKeyZoom = 2.5f;    ///< Times per second

        constexpr std::array<render::VertexAttribute, 3> kGroundAttributes = { {
            { 0, render::VertexFormat::Float3, offsetof(park::GroundVertex, position) },
            { 1, render::VertexFormat::Float4, offsetof(park::GroundVertex, colour) },
            { 2, render::VertexFormat::Float2, offsetof(park::GroundVertex, uv) },
        } };
        constexpr std::array<render::VertexAttribute, 2> kWaterAttributes = { {
            { 0, render::VertexFormat::Float3, offsetof(park::WaterVertex, position) },
            { 1, render::VertexFormat::Float4, offsetof(park::WaterVertex, colour) },
        } };

        /// The top folders of the install that the park's objects come from, as they are spelled on disk.
        std::vector<std::string> object_folders(const config::GameInstall& install, std::span<const save::PlacedObject> objects)
        {
            std::vector<std::string> folders;
            for (const save::PlacedObject& o : objects)
            {
                const std::string_view overlay = o.overlay;
                const std::string_view top = overlay.substr(0, overlay.find_first_of("/\\"));
                if (top.empty() || top.size() == overlay.size())
                    continue;
                const auto found = install.find(top);
                if (!found)
                    continue;
                std::string name = found->filename().string();
                if (std::ranges::find(folders, name) == folders.end())
                    folders.push_back(std::move(name));
            }
            return folders;
        }

        /// The models of the park's objects, by model key, and the pictures they use.
        park::ObjectModels load_models(const config::GameInstall& install, std::span<const save::PlacedObject> objects, bool modelled_plants, park::ModelPictures& pictures)
        {
            const std::vector<std::string> folders = object_folders(install, objects);
            const std::vector<std::string_view> views(folders.begin(), folders.end());
            const park::AssetIndex index = park::AssetIndex::scan(install.root(), views);
            park::ModelLoader loader(index);
            park::ObjectModels models;
            std::map<std::string, bool, std::less<>> tried;
            for (const save::PlacedObject& o : objects)
            {
                std::string key = park::model_key(o);
                if (!tried.emplace(key, true).second)
                    continue;
                if (auto model = loader.load(o.symbol, o.overlay, modelled_plants))
                    models.emplace(std::move(key), std::move(*model));
            }
            pictures = loader.pictures();
            return models;
        }

        std::expected<std::vector<std::byte>, std::string> read_file(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::unexpected("cannot open " + path.string());
            const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto bytes = std::as_bytes(std::span(raw));
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        }

        template<typename T> std::expected<render::BufferHandle, std::string> upload(render::Device& device, render::BufferUsage usage, const std::vector<T>& items, std::string_view label)
        {
            return device.create_buffer({ usage, std::as_bytes(std::span(items)), label });
        }

        /// Paints and uploads the pictures of `batches` the scene does not have yet.
        void add_textures(render::Device& device, ParkScene& scene, std::span<const park::ObjectBatch> batches)
        {
            std::vector<park::ObjectBatch> missing;
            for (const park::ObjectBatch& batch : batches)
            {
                if (!scene.object_textures.contains(batch.texture))
                    missing.push_back(batch);
            }
            if (missing.empty())
                return;
            auto uploaded = ui::upload_textures(device, park::paint_batches(missing, scene.pictures));
            if (!uploaded)
            {
                logging::logger("park").warning("object pictures: {}", uploaded.error());
                return;
            }
            scene.object_textures.merge(*uploaded);
        }

        /// Builds the object buffers again when an object's level differs from the one they hold.
        void refresh_objects(render::Device& device, ParkScene& scene, const glm::vec3& eye)
        {
            park::ObjectLevels levels = park::object_levels(scene.objects, scene.models, scene.anchors, eye);
            if (levels == scene.levels && scene.object_indices)
                return;
            for (render::BufferHandle& b : { std::ref(scene.object_vertices), std::ref(scene.object_indices) })
            {
                if (b)
                    device.destroy_buffer(b);
                b = { };
            }
            scene.levels = std::move(levels);
            scene.since_rebuild = 0;
            const park::ObjectMesh mesh = park::object_mesh(scene.objects, scene.models, scene.grid, scene.levels);
            scene.object_batches = mesh.batches;
            add_textures(device, scene, mesh.batches);
            if (mesh.indices.empty())
                return;
            auto vertices = upload(device, render::BufferUsage::Vertex, mesh.vertices, "object vertices");
            auto indices = upload(device, render::BufferUsage::Index, mesh.indices, "object indices");
            if (!vertices || !indices)
            {
                logging::logger("park").warning("object buffers: {}", !vertices ? vertices.error() : indices.error());
                if (vertices)
                    device.destroy_buffer(*vertices);
                if (indices)
                    device.destroy_buffer(*indices);
                scene.object_batches.clear();
                return;
            }
            scene.object_vertices = *vertices;
            scene.object_indices = *indices;
        }

        float scale_for(const render::Device& device, const ParkScene& scene)
        {
            return ui::interface_scale(static_cast<float>(device.extent().height), scene.scale);
        }

        float aspect(const render::Device& device)
        {
            const render::Extent e = device.extent();
            return e.height > 0 ? static_cast<float>(e.width) / static_cast<float>(e.height) : 1.0f;
        }

        /// Draws `mesh` from buffers made for this frame, each batch with `pipeline_of(batch)`.
        void draw_transient(render::Device& device, const ParkScene& scene, const park::ObjectMesh& mesh, const ui::UiTextures& textures, const glm::mat4& transform, auto&& pipeline_of, auto&& address_of)
        {
            if (mesh.indices.empty())
                return;
            const auto vertices = device.allocate_transient(std::as_bytes(std::span(mesh.vertices)));
            const auto indices = device.allocate_transient(std::as_bytes(std::span(mesh.indices)));
            if (!vertices || !indices)
                return;
            for (const park::ObjectBatch& batch : mesh.batches)
            {
                const auto texture = textures.find(batch.texture);
                render::DrawCommand draw;
                draw.pipeline = pipeline_of(batch);
                draw.vertices = *vertices;
                draw.indices = *indices;
                draw.index_type = render::IndexType::U32;
                draw.first = batch.first;
                draw.count = batch.count;
                draw.transform = transform;
                draw.texture = texture != textures.end() ? texture->second.handle : scene.blank;
                draw.filter = render::Filter::Linear;
                draw.address = address_of(batch);
                device.draw(draw);
            }
        }

        /// The sky from the install, ready to draw; a sky that cannot be read is left out.
        std::expected<void, std::string> load_sky(render::Device& device, const config::GameInstall& install, ParkScene& scene)
        {
            const auto log = logging::logger("park");
            const auto found = install.find(park::kSkyFile);
            if (!found)
            {
                log.warning("no sky: {} is missing", park::kSkyFile);
                return { };
            }
            auto sky = park::load_sky(*found);
            if (!sky)
            {
                log.warning("sky: {}", sky.error());
                return { };
            }
            auto textures = ui::upload_textures(device, sky->images);
            if (!textures)
                return std::unexpected(textures.error());
            scene.sky_textures = std::move(*textures);
            if (const auto horizon = park::horizon_colour(*sky, park::kMiddayColumn))
                scene.horizon = { horizon->x, horizon->y, horizon->z, horizon->w };
            scene.sky = std::move(sky->models);
            const render::VertexLayout layout { sizeof(park::GroundVertex), kGroundAttributes };
            auto pipeline = device.create_pipeline({ .shader = render::Shader::Textured, .layout = layout, .blend = render::BlendMode::Alpha, .depth_test = false, .depth_write = false, .label = "sky" });
            if (!pipeline)
                return std::unexpected(pipeline.error());
            scene.sky_pipeline = *pipeline;
            return { };
        }

        void ask_leave(ParkScene& scene, const ui::StringTable& strings)
        {
            if (scene.desktop->root().find(kQuestionName) != nullptr)
                return;
            const ui::QuestionBoxStyle style = skins::question_box_style();
            scene.desktop->root().add<ui::QuestionBox>(std::string(kQuestionName), style, std::u16string(strings.get(kLeaveText)), std::vector<ui::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        }

        void move_camera(ParkScene& scene, const ParkInput& input, bool over_interface, float elapsed)
        {
            const ui::Point p = input.pointer.position;
            const float dx = p.x - scene.last_pointer.x;
            const float dy = p.y - scene.last_pointer.y;
            scene.last_pointer = p;

            // A drag starts away from the windows and goes on wherever the pointer then goes.
            const bool turn = input.middle || (input.right && input.pointer.down);
            const bool drag = input.right && !turn;
            scene.turning = turn && (scene.turning || !over_interface);
            scene.dragging = drag && (scene.dragging || !over_interface);

            park::ParkCamera c = scene.camera;
            if (scene.turning)
                c = park::turned(c, dx * kDragTurn, dy * kDragTurn);
            else if (scene.dragging)
                c = park::panned(c, -dx * kDragPan * c.distance, dy * kDragPan * c.distance);
            if (!over_interface && input.pointer.wheel.y != 0)
                c = park::zoomed(c, std::pow(kWheelZoom, input.pointer.wheel.y));

            const float step = kKeyPan * c.distance * elapsed;
            const float right = (input.right_key ? step : 0) - (input.left_key ? step : 0);
            const float forward = (input.up_key ? step : 0) - (input.down_key ? step : 0);
            if (right != 0 || forward != 0)
                c = park::panned(c, right, forward);
            if (input.zoom_in_key != input.zoom_out_key)
                c = park::zoomed(c, std::pow(kKeyZoom, (input.zoom_in_key ? -1.0f : 1.0f) * elapsed));
            scene.camera = park::on_ground(c, scene.grid);
        }

    } // namespace

    std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const config::GameOptions& options, const std::filesystem::path& path)
    {
        const auto log = logging::logger("park");
        const auto bytes = read_file(path);
        if (!bytes)
            return std::unexpected(bytes.error());
        const auto file = save::read_save(*bytes, { .verify_checksum = false });
        if (!file)
            return std::unexpected(std::string(save::to_string(file.error())));
        const save::ObjectIndex index(*file);
        auto world = save::load_world(*file, index);
        if (!world)
            return std::unexpected(std::string(save::to_string(world.error())));
        if (world->terrain.empty())
            return std::unexpected("the park has no ground");

        ParkScene scene;
        scene.grid = std::move(world->terrain);
        scene.camera = park::overview(scene.grid);
        const park::TerrainMesh mesh = park::terrain_mesh(scene.grid, world->water);
        scene.batches = mesh.batches;

        // Built one by one; whatever fails leaves the rest for destroy_park_scene.
        const auto fail = [&](std::string message) -> std::expected<ParkScene, std::string> {
            destroy_park_scene(device, scene);
            return std::unexpected(std::move(message));
        };
        auto vertices = upload(device, render::BufferUsage::Vertex, mesh.vertices, "ground vertices");
        if (!vertices)
            return fail(vertices.error());
        scene.ground_vertices = *vertices;
        auto indices = upload(device, render::BufferUsage::Index, mesh.indices, "ground indices");
        if (!indices)
            return fail(indices.error());
        scene.ground_indices = *indices;
        if (!mesh.water.empty())
        {
            auto water = upload(device, render::BufferUsage::Vertex, mesh.water, "water");
            if (!water)
                return fail(water.error());
            scene.water = *water;
            scene.water_count = static_cast<std::uint32_t>(mesh.water.size());
        }

        auto ground = device.create_pipeline({ .shader = render::Shader::Textured, .layout = { sizeof(park::GroundVertex), kGroundAttributes }, .blend = render::BlendMode::Alpha, .label = "ground" });
        if (!ground)
            return fail(ground.error());
        scene.ground_pipeline = *ground;
        auto water = device.create_pipeline({ .shader = render::Shader::VertexColor, .layout = { sizeof(park::WaterVertex), kWaterAttributes }, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "water" });
        if (!water)
            return fail(water.error());
        scene.water_pipeline = *water;

        park::GroundImages images;
        for (const std::string_view name : kGroundFiles)
        {
            const auto found = install.find(name);
            if (!found)
                continue;
            if (const auto added = park::add_ground_images(images, *found); !added)
                log.warning("{}: {}", name, added.error());
        }
        auto textures = ui::upload_textures(device, images);
        if (!textures)
            return fail(textures.error());
        scene.ground_textures = std::move(*textures);
        constexpr std::array<std::byte, 4> kWhite { std::byte { 255 }, std::byte { 255 }, std::byte { 255 }, std::byte { 255 } };
        auto blank = device.create_texture({ 1, 1, render::TextureFormat::Rgba8, kWhite, "blank ground" });
        if (!blank)
            return fail(blank.error());
        scene.blank = *blank;

        scene.models = load_models(install, world->objects, options.flag(config::GameOption::Trees3D), scene.pictures);
        scene.objects = std::move(world->objects);
        scene.anchors = park::object_anchors(scene.objects, scene.models, scene.grid);
        const render::VertexLayout object_layout { sizeof(park::GroundVertex), kGroundAttributes };
        auto solid = device.create_pipeline({ .shader = render::Shader::Textured, .layout = object_layout, .label = "objects" });
        if (!solid)
            return fail(solid.error());
        scene.object_pipeline = *solid;
        auto cutout = device.create_pipeline({ .shader = render::Shader::TexturedCutout, .layout = object_layout, .label = "object cut-outs" });
        if (!cutout)
            return fail(cutout.error());
        scene.cutout_pipeline = *cutout;
        auto see_through = device.create_pipeline({ .shader = render::Shader::Textured, .layout = object_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "see-through objects" });
        if (!see_through)
            return fail(see_through.error());
        scene.see_through_pipeline = *see_through;
        refresh_objects(device, scene, park::eye(scene.camera));
        if (auto sky = load_sky(device, install, scene); !sky)
            return fail(sky.error());

        auto tutorial = save::load_tutorial(*file);
        if (!tutorial)
            log.warning("tutorial: {}", save::to_string(tutorial.error()));
        else if (*tutorial)
            scene.desktop->root().add<ui::TutorialGuide>(std::string(kGuideName), skins::question_box_style(), strings, std::move(**tutorial));
        scene.desktop->tooltip_style = skins::tooltip_style();
        log.info("{}: {} by {} tiles, {} ground batches", path.filename().string(), scene.grid.width, scene.grid.height, scene.batches.size());
        const auto drawn = std::ranges::count_if(scene.levels, [](std::int8_t l) { return l != park::kNotDrawn; });
        const auto modelled = std::ranges::count_if(scene.objects, [&](const save::PlacedObject& o) { return !o.hidden && scene.models.contains(park::model_key(o)); });
        const auto shown = std::ranges::count_if(scene.objects, [](const save::PlacedObject& o) { return !o.hidden; });
        log.info("{} objects in view, {} without a model, {} models, {} pictures", drawn, shown - modelled, scene.models.size(), scene.object_textures.size());
        return scene;
    }

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed)
    {
        const float scale = scale_for(device, scene);
        const render::Extent extent = device.extent();
        ui::Desktop& desktop = *scene.desktop;
        desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Escape asks to leave, as the guide's quit does, unless the question is already open: then it
        // answers it.
        const bool asking = desktop.root().find(kQuestionName) != nullptr;
        ui::KeyboardState keys = keyboard;
        if (!asking)
        {
            const auto escape = std::ranges::remove_if(keys.presses, [](const ui::KeyPress& k) { return k.key == ui::Key::Escape; });
            if (!escape.empty())
                ask_leave(scene, *assets.strings);
            keys.presses.erase(escape.begin(), escape.end());
        }

        ui::PointerState pointer = input.pointer;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        scene.current_fonts = &ui::fonts_for(device, assets, desktop.root().rect().width());
        const bool over = desktop.update(pointer, keys, { elapsed, scene.current_fonts });
        move_camera(scene, input, over, elapsed);
        const glm::vec3 viewer = park::eye(scene.camera);
        scene.since_rebuild += elapsed;
        if (scene.since_rebuild >= kObjectRebuildInterval)
            refresh_objects(device, scene, viewer);
        scene.billboards = park::billboard_mesh(scene.objects, scene.models, scene.anchors, scene.levels, viewer);
        add_textures(device, scene, scene.billboards.batches);
        scene.clock += elapsed;
        scene.sky_mesh = park::sky_mesh(scene.sky, { .eye = viewer, .radius = kSkyRadius, .time = scene.clock });

        if (auto* guide = static_cast<ui::TutorialGuide*>(desktop.root().find(kGuideName)); guide != nullptr && guide->take_quit())
            ask_leave(scene, *assets.strings);
        if (auto* question = static_cast<ui::QuestionBox*>(desktop.root().find(kQuestionName)))
        {
            if (const auto answer = question->take_answer())
                scene.leave = scene.leave || *answer == 0;
        }
    }

    bool take_park_leave(ParkScene& scene)
    {
        return std::exchange(scene.leave, false);
    }

    render::Color park_background(const ParkScene& scene)
    {
        return scene.horizon;
    }

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        const glm::mat4 transform = park::view_projection(scene.camera, aspect(device));
        if (scene.sky_pipeline)
        {
            // The dome reads one column of its picture, so it must not wrap into the next.
            draw_transient(device, scene, scene.sky_mesh, scene.sky_textures, transform, [&](const park::ObjectBatch&) { return scene.sky_pipeline; }, [](const park::ObjectBatch& b) { return b.blend == park::PartBlend::Opaque ? render::AddressMode::Clamp : render::AddressMode::Repeat; });
        }
        const auto draw_objects = [&](bool see_through) {
            for (const park::ObjectBatch& batch : scene.object_batches)
            {
                if ((batch.blend == park::PartBlend::Blended) != see_through)
                    continue;
                const auto texture = scene.object_textures.find(batch.texture);
                render::DrawCommand draw;
                draw.pipeline = batch.blend == park::PartBlend::Opaque ? scene.object_pipeline : batch.blend == park::PartBlend::Cutout ? scene.cutout_pipeline : scene.see_through_pipeline;
                draw.vertices = { scene.object_vertices };
                draw.indices = { scene.object_indices };
                draw.index_type = render::IndexType::U32;
                draw.first = batch.first;
                draw.count = batch.count;
                draw.transform = transform;
                draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
                draw.filter = render::Filter::Linear;
                draw.address = render::AddressMode::Repeat;
                device.draw(draw);
            }
        };
        for (const park::GroundBatch& batch : scene.batches)
        {
            const auto texture = scene.ground_textures.find(park::ground_key(batch.paint, batch.index));
            render::DrawCommand draw;
            draw.pipeline = scene.ground_pipeline;
            draw.vertices = { scene.ground_vertices };
            draw.indices = { scene.ground_indices };
            draw.index_type = render::IndexType::U32;
            draw.first = batch.first;
            draw.count = batch.count;
            draw.transform = transform;
            draw.texture = texture != scene.ground_textures.end() ? texture->second.handle : scene.blank;
            draw.filter = render::Filter::Linear;
            draw.address = render::AddressMode::Repeat;
            device.draw(draw);
        }
        if (scene.object_indices)
            draw_objects(false);
        draw_transient(device, scene, scene.billboards, scene.object_textures, transform, [&](const park::ObjectBatch&) { return scene.cutout_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
        if (scene.water_count > 0)
        {
            render::DrawCommand draw;
            draw.pipeline = scene.water_pipeline;
            draw.vertices = { scene.water };
            draw.count = scene.water_count;
            draw.transform = transform;
            device.draw(draw);
        }
        if (scene.object_indices)
            draw_objects(true);

        scene.list.clear();
        scene.list.set_scale(scale_for(device, scene));
        static const ui::FontSet kNoFonts;
        ui::DrawContext context { scene.list, assets.skins, scene.current_fonts != nullptr ? *scene.current_fonts : kNoFonts };
        scene.desktop->draw(context);
        ui::render_draw_list(device, assets.renderer, scene.list);
    }

    void destroy_park_scene(render::Device& device, ParkScene& scene)
    {
        for (const render::BufferHandle b : { scene.ground_vertices, scene.ground_indices, scene.water, scene.object_vertices, scene.object_indices })
        {
            if (b)
                device.destroy_buffer(b);
        }
        for (const render::PipelineHandle p : { scene.ground_pipeline, scene.water_pipeline, scene.object_pipeline, scene.cutout_pipeline, scene.see_through_pipeline, scene.sky_pipeline })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        ui::destroy_textures(device, scene.ground_textures);
        ui::destroy_textures(device, scene.object_textures);
        ui::destroy_textures(device, scene.sky_textures);
        if (scene.blank)
            device.destroy_texture(scene.blank);
        scene = { };
    }

} // namespace rct3::scene
