// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_setup.hpp"

#include "logging/log.hpp"
#include "park/asset_index.hpp"
#include "park/boundary_fence.hpp"
#include "park/build_catalogue.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/ground_textures.hpp"
#include "park/ground_variation.hpp"
#include "park/item_category.hpp"
#include "park/object_models.hpp"
#include "park/off_map_trees.hpp"
#include "park/overhead.hpp"
#include "park/path_supports.hpp"
#include "park/path_tunnels.hpp"
#include "park/person_motions.hpp"
#include "park/queue_fences.hpp"
#include "park/relief_maps.hpp"
#include "park/ride_cars.hpp"
#include "park/ride_fences.hpp"
#include "park/sky_models.hpp"
#include "park/surrounding_land.hpp"
#include "park/track_supports.hpp"
#include "park/water_flow.hpp"
#include "parkfile/saved_park.hpp"
#include "profiling/profiler.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/tutorial.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_glow.hpp"
#include "scene/park_map_view.hpp"
#include "scene/park_occlusion.hpp"
#include "scene/park_patrol.hpp"
#include "scene/park_refresh.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_view.hpp"
#include "scene/park_water.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/import_people.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/management_pages.hpp"
#include "ui/screens/message_drawer.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/screens/tutorial_guide.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::array<render::VertexAttribute, 4> kGroundAttributes = { {
            { 0, render::VertexFormat::Float3, offsetof(park::GroundVertex, position) },
            { 1, render::VertexFormat::Float4, offsetof(park::GroundVertex, colour) },
            { 2, render::VertexFormat::Float2, offsetof(park::GroundVertex, uv) },
            { 3, render::VertexFormat::Float3, offsetof(park::GroundVertex, normal) },
        } };

        constexpr std::array<render::VertexAttribute, 6> kCrossingAttributes = { {
            { 0, render::VertexFormat::Float3, offsetof(CrossingVertex, ground) + offsetof(park::GroundVertex, position) },
            { 1, render::VertexFormat::Float4, offsetof(CrossingVertex, ground) + offsetof(park::GroundVertex, colour) },
            { 2, render::VertexFormat::Float2, offsetof(CrossingVertex, ground) + offsetof(park::GroundVertex, uv) },
            { 3, render::VertexFormat::Float3, offsetof(CrossingVertex, ground) + offsetof(park::GroundVertex, normal) },
            { 4, render::VertexFormat::Float4, offsetof(CrossingVertex, band) },
            { 5, render::VertexFormat::Float2, offsetof(CrossingVertex, side) },
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
        /// Adds the models of `objects` that `models` does not have yet, and their pictures.
        /// With `cars`, the objects are ride cars and these are their designs, by object.
        void load_models(const config::GameInstall& install, std::span<const save::PlacedObject> objects, bool modelled_plants, park::ObjectModels& models, park::ModelPictures& pictures, std::span<const park::CarDesign> cars = { })
        {
            const profiling::Scope zone("Models");
            const std::vector<std::string> folders = object_folders(install, objects);
            const std::vector<std::string_view> views(folders.begin(), folders.end());
            park::AssetIndex index = park::AssetIndex::scan(install.root(), views);
            // Overlays the scanned ones take assets from, and those they take from in turn.
            std::set<std::string, std::less<>> tried_files;
            for (auto missing = index.missing_references(); !missing.empty(); missing = index.missing_references())
            {
                bool added = false;
                for (const std::string& reference : missing)
                {
                    if (!tried_files.insert(reference).second)
                        continue;
                    if (const auto file = install.find(reference + ".common.ovl"))
                        added = index.add_file(*file) || added;
                }
                if (!added)
                    break;
            }
            park::ModelLoader loader(index);
            std::map<std::string, bool, std::less<>> tried;
            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                const save::PlacedObject& o = objects[i];
                std::string key = park::model_key(o);
                if (models.contains(key) || !tried.emplace(key, true).second)
                    continue;
                if (auto model = i < cars.size() ? loader.load_car(cars[i].train, o.overlay, cars[i].slot) : loader.load(o.symbol, o.overlay, modelled_plants))
                    models.emplace(std::move(key), std::move(*model));
            }
            for (const auto& [key, picture] : loader.pictures())
                pictures.try_emplace(key, picture);
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

        /// The ground's grass and dirt get half the relief of walls and rock.
        constexpr float kGroundRelief = 0.35f;
        /// How strongly the ground shows while building under it.
        constexpr float kFadedGround = 0.35f;

        constexpr std::uint8_t kPickColour = 255;
        constexpr std::string_view kCharacterFolder = "Characters";
        /// Where the staff's uniform pictures are.
        /// shortcut: always the one set the game ships, though a save names its set; upgrade when a save
        /// names another.
        constexpr std::string_view kUniformFolder = "StaffUniforms";
        constexpr std::uint8_t kColourCells = 16;
        constexpr std::string_view kCarriedFolder = "CarriedItems";
        constexpr std::string_view kCarriedOverlay = "CarriedItems/MainCarriedItems";
        /// The umbrella shapes, in the order of ParkScene::umbrellas.
        constexpr std::array<std::string_view, 2> kUmbrellaShapes { "UmbrellaOpen", "KidsUmbrellaOpen" };

        /// Reads the umbrellas guests carry and uploads their pictures with the people's.
        void load_umbrellas(render::Device& device, ParkScene& scene, const config::GameInstall& install)
        {
            const std::string folder = install.find(kCarriedFolder).value_or(std::filesystem::path(kCarriedFolder)).filename().string();
            const std::array<std::string_view, 1> folders { folder };
            const park::AssetIndex index = park::AssetIndex::scan(install.root(), folders);
            park::ModelLoader loader(index);
            std::vector<park::ObjectBatch> batches;
            for (std::size_t i = 0; i < kUmbrellaShapes.size(); ++i)
            {
                scene.umbrellas[i] = loader.load_shape(kUmbrellaShapes[i], kCarriedOverlay);
                if (scene.umbrellas[i])
                    for (const park::ModelPart& part : scene.umbrellas[i]->levels.front().parts)
                        batches.push_back({ park::painted_name(part.texture, part.slots, { }), part.texture, { }, part.blend, 0, 0 });
            }
            if (auto uploaded = ui::upload_textures(device, park::paint_batches(batches, loader.pictures())))
                scene.people_textures.merge(*uploaded);
            else
                logging::logger("park").warning("umbrella pictures: {}", uploaded.error());
        }

        /// Reads the models of the park's guests and staff and uploads their pictures.
        void load_people_models(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::uint64_t seed)
        {
            const profiling::Scope zone("PeopleModels");
            const auto folder = install.find(kCharacterFolder);
            if (!folder)
                return;
            const std::string characters = folder->filename().string();
            const std::string uniforms = install.find(kUniformFolder).value_or(std::filesystem::path(kUniformFolder)).filename().string();
            const std::array<std::string_view, 2> folders { characters, uniforms };
            park::PersonLibrary library(install.root(), folders);
            std::map<save::PeopleObjectId, std::string_view> groups;
            for (const save::PartGroup& g : scene.people.part_groups)
                groups.emplace(g.id, g.symbol);
            const auto set = [&](const save::PeopleRef& ref) -> const park::PartSet* {
                const auto it = groups.find(ref.id);
                return it != groups.end() ? as_pointer(library.part_set(it->second)) : nullptr;
            };
            // Colours left for the game to pick are picked here, the same each time for the same park and seed.
            random::RandomState colours = random::stream(seed, random::RandomStream::Effects);
            const auto add = [&](const save::Person& person, bool staff, std::size_t saved) {
                const save::Appearance& a = person.appearance;
                if (!a.visible)
                    return;
                ShownPerson shown;
                park::PersonLook& look = shown.pose.look;
                look.swimwear = a.wearing_swimsuit;
                look.head = set(a.head);
                look.body = set(a.wearing_swimsuit && !a.swim_body.is_none() ? a.swim_body : a.body);
                look.legs = set(a.wearing_swimsuit && !a.swim_legs.is_none() ? a.swim_legs : a.legs);
                if (look.head == nullptr && look.body == nullptr && look.legs == nullptr)
                    return;
                for (std::size_t i = 0; i < look.colours.size(); ++i)
                {
                    const std::uint8_t saved = a.palettes[i];
                    look.colours[i] = saved == kPickColour ? static_cast<std::uint8_t>(random::below(colours, kColourCells)) : static_cast<std::uint8_t>(saved % kColourCells);
                }
                // All bits set means none were picked yet.
                if (a.optionals[0] != ~std::uint32_t { 0 })
                    look.optionals = a.optionals;
                // Stored a row per axis, which is a column each here.
                for (glm::length_t c = 0; c < 4; ++c)
                {
                    for (glm::length_t r = 0; r < 4; ++r)
                        shown.pose.world[c][r] = person.world[static_cast<std::size_t>(c * 4 + r)];
                }
                shown.pose.motion = person.animation.anim_type >= 0 ? person.animation.anim_type : park::motion::kIdle;
                shown.saved_time = person.animation.time;
                shown.speed = person.animation.speed > 0 ? person.animation.speed : 1.0f;
                shown.walking = person.animation.anim_type == person.animation.walk || person.animation.anim_type == person.animation.alternate_walk;
                shown.staff = staff;
                shown.saved = saved;
                scene.shown_people.push_back(shown);
            };
            for (std::size_t i = 0; i < scene.people.guests.size(); ++i)
            {
                if (scene.people.guests[i].in_park)
                    add(scene.people.guests[i].person, false, i);
            }
            for (std::size_t i = 0; i < scene.people.staff.size(); ++i)
                add(scene.people.staff[i].person, true, i);
            library.close_files();
            if (auto uploaded = ui::upload_textures(device, library.pictures()))
                scene.people_textures = std::move(*uploaded);
            else
                logging::logger("park").warning("people pictures: {}", uploaded.error());
            logging::logger("park").info("{} people shown, {} pictures", scene.shown_people.size(), library.pictures().size());
            scene.person_models.emplace(std::move(library));
            load_umbrellas(device, scene, install);
        }

        /// The sky from the install, ready to draw; a sky that cannot be read is left out.
        std::expected<void, std::string> load_sky(render::Device& device, const config::GameInstall& install, const config::GameOptions& options, std::string_view theme, ParkScene& scene)
        {
            const profiling::Scope zone("Sky");
            const auto log = logging::logger("park");
            const auto found = install.find(park::kSkyFile);
            if (!found)
            {
                log.warning("no sky: {} is missing", park::kSkyFile);
                return { };
            }
            auto sky = park::load_sky(*found, theme);
            if (!sky)
            {
                log.warning("sky: {}", sky.error());
                return { };
            }
            auto textures = ui::upload_textures(device, sky->images);
            if (!textures)
                return std::unexpected(textures.error());
            scene.sky_textures = std::move(*textures);
            scene.horizon_band = park::horizon_band(*sky);
            scene.sky = std::move(sky->models);
            // Without the sky, the clear colour stands in for it; without clouds, the sky is clear.
            if (!options.flag(config::GameOption::Sky))
                scene.sky.dome = scene.sky.stars = scene.sky.milky_way = { };
            if (!options.flag(config::GameOption::Clouds))
                scene.sky.clouds = { };
            if (options.flag(config::GameOption::Mountains))
                scene.mountains = park::mountain_mesh(sky->mountains, park::mountain_place(scene.grid, theme));
            const render::VertexLayout layout { sizeof(park::GroundVertex), kGroundAttributes };
            auto pipeline = device.create_pipeline({ .shader = render::Shader::Textured, .layout = layout, .blend = render::BlendMode::Alpha, .depth_test = false, .depth_write = false, .label = "sky" });
            if (!pipeline)
                return std::unexpected(pipeline.error());
            scene.sky_pipeline = *pipeline;
            return { };
        }

        /// The park's time of day as its file left it, in the window its mode keeps. A park without opening
        /// hours keeps the whole day.
        simulation::DayClock park_day(const save::SaveFile& file, const save::ObjectIndex& index, const save::WorldModel& world, const config::GameOptions& options)
        {
            simulation::DayWindow hours;
            if (const auto park = save::load_park(file, index); park && park->has_park)
                hours = simulation::opening_hours(park->park.opening_minutes, park->park.closing_minutes);
            const std::optional<std::int32_t> mode = world.time ? world.time->day_night_mode : std::nullopt;
            const std::optional<float> time = world.time ? std::optional(world.time->day_fraction) : std::nullopt;
            return simulation::start_day(time, simulation::day_window(simulation::day_mode(mode), hours), simulation::day_transition(options.choice(config::GameOption::DayNight)));
        }

    } // namespace

    /// The relief map uploaded beside picture `name` (park/relief_maps.hpp), or none.
    render::TextureHandle relief_of(const ui::UiTextures& textures, std::string_view name)
    {
        const auto found = textures.find(std::string(name) + std::string(park::kReliefSuffix));
        return found != textures.end() ? found->second.handle : render::TextureHandle { };
    }

    /// Paints and uploads the pictures of `batches` the scene does not have yet.
    void add_textures(render::Device& device, ParkScene& scene, std::span<const park::ObjectBatch> batches)
    {
        std::vector<park::ObjectBatch> missing;
        for (const park::ObjectBatch& batch : batches)
        {
            // Ground-painted parts draw with the ground's own pictures.
            if (!scene.object_textures.contains(batch.texture) && !scene.ground_textures.contains(batch.texture))
                missing.push_back(batch);
        }
        if (missing.empty())
            return;
        auto pictures = park::paint_batches(missing, scene.pictures);
        if (scene.relief_maps)
            park::add_relief_maps(pictures);
        auto uploaded = ui::upload_textures(device, pictures);
        if (!uploaded)
        {
            logging::logger("park").warning("object pictures: {}", uploaded.error());
            return;
        }
        scene.object_textures.merge(*uploaded);
    }

    void seat_flat_ride(render::Device& device, ParkScene& scene, std::size_t ride_index)
    {
        if (ride_index >= scene.rides.rides.size() || ride_index >= scene.ride_entities.size())
            return;
        const save::Ride& ride = scene.rides.rides[ride_index];
        const entt::entity entity = scene.ride_entities[ride_index];
        if (ride.kind != save::RideKind::Flat || ride.base.placement == 0 || entity == entt::null)
            return;
        for (std::size_t k = 0; k < scene.objects.size(); ++k)
        {
            const auto model = scene.objects[k].owner_ref == ride.base.placement ? scene.models.find(park::model_key(scene.objects[k])) : scene.models.end();
            if (model != scene.models.end() && !model->second.seat_places.empty())
            {
                scene.ride_seats[entity] = k;
                if (model->second.rig)
                {
                    ParkScene::BonedRide& boned = scene.boned_rides.emplace_back();
                    boned.ride = entity;
                    boned.object = k;
                    boned.placed = park::placed_transform(scene.objects[k], &model->second, scene.grid, scene.fitted);
                    std::tie(boned.mesh, boned.bones) = park::boned_meshes(scene.objects[k], model->second, scene.grid);
                    boned.first_seat = scene.seat_carriers.size();
                    scene.seat_carriers.resize(scene.seat_carriers.size() + model->second.seat_bones.size(), boned.placed);
                    add_textures(device, scene, boned.mesh.mesh.batches);
                }
                return;
            }
        }
    }

    void animate_scenery(render::Device& device, ParkScene& scene, std::size_t k)
    {
        const auto model = k < scene.objects.size() ? scene.models.find(park::model_key(scene.objects[k])) : scene.models.end();
        // Attractions' parts move with their attractions (doors as guests pass, chains while the ride runs), so only
        // what the scenery lists hold moves on its own.
        if (model == scene.models.end() || !model->second.rig || scene.objects[k].hidden || !park::scenery_list(model->second.category) || scene.attractions.contains(scene.objects[k].id))
            return;
        ParkScene::BonedRide& boned = scene.boned_rides.emplace_back();
        boned.object = k;
        boned.placed = park::placed_transform(scene.objects[k], &model->second, scene.grid, scene.fitted);
        std::tie(boned.mesh, boned.bones) = park::boned_meshes(scene.objects[k], model->second, scene.grid);
        boned.first_seat = scene.seat_carriers.size();
        add_textures(device, scene, boned.mesh.mesh.batches);
    }

    std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const config::GameOptions& options, const std::filesystem::path& path, std::uint64_t seed, const simulation::RatingModel& ratings, const ParkLoadReport& report)
    {
        const profiling::Scope zone("LoadPark");
        const auto log = logging::logger("park");
        const auto begin = [&](ParkLoadStage stage) {
            if (report)
                report(stage);
        };
        begin(ParkLoadStage::ReadFile);
        auto bytes = profiling::section("ReadFile", [&] { return read_file(path); });
        if (!bytes)
            return std::unexpected(bytes.error());
        // One of ours: the original park it started from, with the player's changes put over it below.
        std::optional<parkfile::SavedPark> saved;
        if (parkfile::is_park_file(*bytes))
        {
            auto read = parkfile::read_saved_park(*bytes);
            if (!read)
                return std::unexpected(read.error());
            saved = std::move(*read);
            *bytes = std::move(saved->base);
        }
        const auto file = profiling::section("ParseSave", [&] { return save::read_save(*bytes, { .verify_checksum = false }); });
        if (!file)
            return std::unexpected(std::string(save::to_string(file.error())));
        const save::ObjectIndex index(*file);
        auto world = save::load_world(*file, index);
        if (!world)
            return std::unexpected(std::string(save::to_string(world.error())));
        if (saved)
        {
            world->terrain = std::move(saved->grid);
            world->paths = std::move(saved->paths);
            world->objects = std::move(saved->objects);
            world->bins = std::move(saved->bins);
            world->signs = std::move(saved->signs);
        }
        if (world->terrain.empty())
            return std::unexpected("the park has no ground");

        ParkScene scene;
        scene.base = *bytes;
        scene.surface_detail = options.choice(config::GameOption::SurfaceDetail);
        scene.relief_maps = scene.surface_detail >= 2;
        scene.grid = std::move(world->terrain);
        scene.seed = seed;
        scene.file = path;
        scene.gameplay = random::stream(seed, random::RandomStream::Gameplay);
        log.info("{}: seed {} (--seed repeats it)", path.string(), random::seed_text(seed));
        scene.camera = park::overview(scene.grid);
        scene.day = park_day(*file, index, *world, options);
        if (saved)
        {
            scene.camera = saved->camera;
            scene.day = simulation::start_day(saved->progress.day_time, scene.day.window, scene.day.transition);
        }
        begin(ParkLoadStage::Ground);
        const std::vector<park::GroundOpening> hollows = park::tunnel_hollows(world->paths, scene.grid);
        park::TerrainMesh mesh = profiling::section("Terrain", [&] { return park::terrain_mesh(scene.grid, world->water, hollows); });
        park::add_ground(mesh, park::tunnel_lining(hollows, scene.grid));
        std::ranges::copy(park::pool_water(scene.grid, world->pool_water), std::back_inserter(mesh.water));
        const park::GroundPathMap paths = park::ground_path_map(world->paths, scene.grid);
        park::add_ground(mesh, park::ground_path_mesh(paths, scene.grid));
        const park::GroundQueues queues = park::ground_queues(world->paths, scene.grid);
        park::add_ground(mesh, queues.mesh);
        const park::TerrainMesh surroundings = park::surrounding_land(scene.grid, world->water);
        park::add_ground(mesh, surroundings);
        mesh.water.insert(mesh.water.end(), surroundings.water.begin(), surroundings.water.end());
        scene.batches = mesh.batches;
        scene.footpaths.paths = world->paths;
        scene.footpaths.water = world->water;
        scene.footpaths.pools = world->pool_water;
        scene.footpaths.pictured = paths.styles;

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
        const render::VertexLayout ground_layout { sizeof(park::GroundVertex), kGroundAttributes };
        if (auto water = create_park_water(device, scene.water, mesh.water, ground_layout); !water)
            return fail(water.error());
        auto ground = device.create_pipeline({ .shader = render::Shader::Lit, .layout = ground_layout, .blend = render::BlendMode::Alpha, .label = "ground" });
        if (!ground)
            return fail(ground.error());
        scene.ground_pipeline = *ground;
        auto ground_depth = device.create_pipeline({ .shader = render::Shader::Lit, .layout = ground_layout, .colour_write = false, .label = "ground depth" });
        if (!ground_depth)
            return fail(ground_depth.error());
        scene.ground_depth_pipeline = *ground_depth;
        auto ground_faded = device.create_pipeline({ .shader = render::Shader::Lit, .layout = ground_layout, .blend = render::BlendMode::Faded, .depth_write = false, .fade = kFadedGround, .label = "faded ground" });
        if (!ground_faded)
            return fail(ground_faded.error());
        scene.ground_faded_pipeline = *ground_faded;
        const std::vector<std::byte> noise = park::variation_texels();
        // Without it the ground is drawn plain.
        if (auto variation = device.create_texture({ .width = park::kVariationSize, .height = park::kVariationSize, .pixels = noise, .label = "ground variation" }))
            scene.variation = *variation;

        park::GroundImages images;
        for (const std::string_view name : park::kGroundFiles)
        {
            const auto found = install.find(name);
            if (!found)
                continue;
            if (const auto added = park::add_ground_images(images, *found); !added)
                log.warning("{}: {}", name, added.error());
        }
        std::vector<park::TunnelPictures> tunnels;
        for (const park::TunnelStyle& style : park::kTunnelStyles)
        {
            const std::string name = park::tunnel_texture_file(style.name);
            const auto found = install.find(name);
            auto loaded = found ? park::load_tunnel_pictures(*found, style) : std::unexpected(std::string("not found"));
            if (loaded && *loaded)
                tunnels.push_back(std::move(**loaded));
            else
                log.warning("{}: {}", name, loaded ? "no picture" : loaded.error());
        }
        park::add_tunnel_images(images, tunnels);
        for (std::size_t i = 0; i < paths.styles.size(); ++i)
        {
            const std::string name = park::path_texture_file(paths.styles[i]);
            const auto found = install.find(name);
            const auto missing = found ? park::add_path_images(images, *found, paths.styles[i], static_cast<std::uint8_t>(i)) : std::expected<std::size_t, std::string>(std::unexpected("not found"));
            if (!missing)
                log.warning("{}: {}", name, missing.error());
            else if (*missing > 0)
                log.warning("{}: {} ground pictures missing", name, *missing);
        }
        for (std::size_t i = 0; i < queues.paintings.size(); ++i)
        {
            const std::string name = park::queue_texture_file(queues.paintings[i].style);
            const auto found = install.find(name);
            const auto added = found ? park::add_queue_image(images, *found, queues.paintings[i], static_cast<std::uint8_t>(i)) : std::expected<bool, std::string>(std::unexpected("not found"));
            if (!added)
                log.warning("{}: {}", name, added.error());
            else if (!*added)
                log.warning("{}: no ground picture", name);
        }
        if (scene.relief_maps)
            park::add_relief_maps(images, kGroundRelief);
        auto textures = ui::upload_textures(device, images);
        if (!textures)
            return fail(textures.error());
        scene.ground_textures = std::move(*textures);
        constexpr std::array<std::byte, 4> kWhite { std::byte { 255 }, std::byte { 255 }, std::byte { 255 }, std::byte { 255 } };
        auto blank = device.create_texture({ 1, 1, render::TextureFormat::Rgba8, kWhite, "blank ground" });
        if (!blank)
            return fail(blank.error());
        scene.blank = *blank;

        const auto park_model = save::load_park(*file, index);
        begin(ParkLoadStage::Objects);
        if (const auto flow_file = install.find(park::kFlowFile))
        {
            // The water running down slides; without it they are drawn dry.
            if (auto flow = park::load_flow_picture(*flow_file))
            {
                if (auto uploaded = ui::upload_textures(device, { { std::string(park::kFlowPicture), *flow } }))
                {
                    scene.object_textures.merge(*uploaded);
                    scene.flow_picture = std::move(*flow);
                }
            }
            else
                logging::logger("park").warning("flowing water: {}", flow.error());
        }
        load_models(install, world->objects, options.flag(config::GameOption::Trees3D), scene.models, scene.pictures);
        scene.objects = std::move(world->objects);
        scene.own_objects = scene.objects.size();
        scene.bins = std::move(world->bins);
        scene.signs = std::move(world->signs);
        if (saved)
        {
            scene.rides = std::move(saved->rides);
            scene.led_signs.hidden.insert(saved->no_queue_signs.begin(), saved->no_queue_signs.end());
        }
        else if (auto rides = save::load_rides(*file, index))
            scene.rides = std::move(*rides);
        else
            log.warning("rides: {}", save::to_string(rides.error()));
        {
            // What a park does not keep but builds when it loads: supports under raised paths and track, railings
            // along queues and fences round rides.
            std::vector<save::PlacedObject> built;
            const auto largest_id = [&] {
                std::uint64_t last = 0;
                for (const auto& list : { std::span<const save::PlacedObject>(scene.objects), std::span<const save::PlacedObject>(built) })
                {
                    for (const save::PlacedObject& o : list)
                        last = std::max(last, o.id);
                }
                return last;
            };
            if (const auto found = install.find(std::string(park::kSupportOverlay) + ".common.ovl"))
            {
                if (const auto sets = park::load_support_sets(*found))
                {
                    built = park::path_supports(world->paths, scene.objects, scene.models, scene.grid, *sets);
                    // Tops, caps and beams are fitted to the points they join, so their shapes are needed first.
                    std::vector<save::PlacedObject> sized;
                    const auto size = [&](const park::SupportPiece& piece) {
                        save::PlacedObject& o = sized.emplace_back();
                        o.symbol = piece.symbol;
                        o.overlay = std::string(park::kSupportOverlay);
                    };
                    for (const auto& [group, list] : *sets)
                    {
                        for (const park::SupportSet& set : list)
                        {
                            for (const auto& piece : { set.top, set.cap })
                            {
                                if (piece)
                                    size(*piece);
                            }
                            std::ranges::for_each(set.beams, size);
                        }
                    }
                    load_models(install, sized, false, scene.models, scene.pictures);
                    park::BuiltSupports columns = park::track_supports(scene.objects, scene.models, world->paths, scene.grid, *sets, largest_id());
                    built.insert(built.end(), std::make_move_iterator(columns.pieces.begin()), std::make_move_iterator(columns.pieces.end()));
                    scene.fitted = std::move(columns.fitted);
                }
                else
                    log.warning("supports: {}", sets.error());
            }
            const std::size_t supports = built.size();
            park::QueueFences fences = park::queue_fences(world->paths, scene.grid, largest_id());
            log.info("{} supports, {} queue railings", supports, fences.pieces.size());
            built.insert(built.end(), std::make_move_iterator(fences.pieces.begin()), std::make_move_iterator(fences.pieces.end()));
            scene.leans = std::move(fences.leans);
            std::unordered_map<std::uint64_t, const park::ObjectModel*> object_models;
            for (const save::PlacedObject& o : scene.objects)
            {
                if (const auto found = scene.models.find(park::model_key(o)); found != scene.models.end())
                    object_models.emplace(o.id, &found->second);
            }
            std::size_t ride_fences = 0;
            for (const save::Ride& ride : scene.rides.rides)
            {
                const park::ObjectModel* item = nullptr;
                if (ride.base.spot)
                {
                    if (const auto found = object_models.find(ride.base.spot->object); found != object_models.end())
                        item = found->second;
                }
                std::vector<save::PlacedObject> pieces = park::ride_fences(ride, item, largest_id());
                ride_fences += pieces.size();
                built.insert(built.end(), std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
            }
            log.info("{} ride fences", ride_fences);
            // shortcut: built once on loading; rebuild it when land can be bought or sold, or a path laid across
            // the park's edge.
            static const std::string kNoFence;
            park::BoundaryFence boundary = park::boundary_fence(park_model ? park_model->park.fence_symbol : kNoFence, park_model ? park_model->park.fence_overlay : kNoFence, scene.grid, world->paths, largest_id());
            log.info("{} park fence panels", boundary.pieces.size());
            const std::size_t first_panel = built.size();
            const std::size_t panels = boundary.pieces.size();
            built.insert(built.end(), std::make_move_iterator(boundary.pieces.begin()), std::make_move_iterator(boundary.pieces.end()));
            scene.leans.merge(boundary.leans);
            load_models(install, built, false, scene.models, scene.pictures);
            // The park names only the fence, so its panels take the fence's own colours.
            for (std::size_t i = first_panel; i < first_panel + panels; ++i)
            {
                if (const auto found = scene.models.find(park::model_key(built[i])); found != scene.models.end())
                    built[i].colours = found->second.default_colours;
            }
            park::RideCars cars = park::ride_cars(scene.rides.rides, largest_id());
            load_models(install, cars.pieces, false, scene.models, scene.pictures, cars.designs);
            scene.cars = std::move(cars.pieces);
            scene.car_spots = std::move(cars.spots);
            scene.saved_cars = std::move(cars.fitted);
            for (const save::PlacedObject& car : scene.cars)
                scene.car_places.push_back(scene.saved_cars.at(car.id));
            scene.car_meshes = park::moving_meshes(scene.cars, scene.models, scene.grid);
            profiling::section("Mesh", [&] { park::move_meshes(scene.car_meshes, scene.car_places); });
            add_textures(device, scene, scene.car_meshes.mesh.batches);
            log.info("{} ride cars, {} vertices", scene.cars.size(), scene.car_meshes.own.size());
            scene.objects.insert(scene.objects.end(), std::make_move_iterator(built.begin()), std::make_move_iterator(built.end()));
            refresh_ride_floors(scene);
        }
        if (options.flag(config::GameOption::OffMapTrees))
        {
            random::RandomState scenery = random::stream(seed, random::RandomStream::Scenery);
            std::vector<save::PlacedObject> trees = park::off_map_trees(scene.objects, scene.models, scene.grid, park::land_rings(scene.grid, scene.footpaths.water), scenery);
            log.info("{} trees beyond the edge", trees.size());
            scene.objects.insert(scene.objects.end(), std::make_move_iterator(trees.begin()), std::make_move_iterator(trees.end()));
        }
        begin(ParkLoadStage::Scene);
        scene.blend_band = options.flag(config::GameOption::BlendDetailLevels) ? options.get(config::GameOption::DetailBlendBand) : 0.0f;
        scene.anchors = park::object_anchors(scene.objects, scene.models, scene.grid, scene.fitted);
        const render::VertexLayout crossing_layout { sizeof(CrossingVertex), kCrossingAttributes };
        for (auto [pipeline, desc] : { std::pair { &scene.object_pipeline, render::PipelineDesc { .shader = render::Shader::Lit, .layout = ground_layout, .label = "objects" } }, std::pair { &scene.cutout_pipeline, render::PipelineDesc { .shader = render::Shader::LitCutout, .layout = ground_layout, .label = "object cut-outs" } },
                                       std::pair { &scene.see_through_pipeline, render::PipelineDesc { .shader = render::Shader::Lit, .layout = ground_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "see-through objects" } }, std::pair { &scene.crossing_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossing, .layout = crossing_layout, .label = "crossing objects" } },
                                       std::pair { &scene.crossing_cutout_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossingCutout, .layout = crossing_layout, .label = "crossing object cut-outs" } }, std::pair { &scene.crossing_see_through_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossing, .layout = crossing_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "crossing see-through objects" } } })
        {
            auto made = device.create_pipeline(desc);
            if (!made)
                return fail(made.error());
            *pipeline = *made;
            // The models' front faces wind as the device's do (seen in the park: culling the other way turned
            // slides inside out).
            desc.cull = render::CullMode::Back;
            auto front = device.create_pipeline(desc);
            if (!front)
                return fail(front.error());
            scene.front_only[made->id] = *front;
        }
        profiling::section("ObjectBuffers", [&] {
            if (auto built = build_objects(scene, park::eye(scene.camera), { }, scene.blend_band, scene.show_supports))
                show_objects(device, scene, std::move(*built));
        });
        if (auto sky = load_sky(device, install, options, park::background_theme(world->sky_background), scene); !sky)
            return fail(sky.error());

        {
            const auto& model = park_model;
            scene.park = simulation::park_state(model ? &*model : nullptr, *world);
            scene.build = start_build_menus(install, model ? &model->content : nullptr);
            if (model)
                scene.park_model = *model;
            if (saved)
            {
                const parkfile::SavedProgress& p = saved->progress;
                scene.park.cash = p.cash;
                scene.park.loan = p.loan;
                scene.park.rating = p.rating;
                scene.park.rating_parts = p.rating_parts;
                scene.park.time = p.time;
                scene.park.zero_time = p.zero_time;
                scene.park.date = p.date;
                scene.park.weather = p.weather;
                scene.park.forecast = p.forecast;
                scene.park.temperature = p.temperature;
            }
        }
        // Under everything else, as part of the park.
        scene.desktop->root().add<ui::WorldLabels>(std::string(kHeightsName)).stays_behind = true;
        // Behind the bar, which it hangs from.
        // Under the bar, even while it is dragged.
        scene.desktop->root().add<ui::MessageDrawer>(std::string(kMessagesName), skins::message_drawer_style(), strings).stays_behind = true;
        auto& tools = scene.desktop->root().add<ui::ToolRow>(std::string(kToolsName), skins::tool_row_style(), strings);
        // Only these menu items do anything yet; the rest show faded until their tools exist.
        for (std::size_t t = 0; t < ui::kParkToolCount; ++t)
        {
            for (const ui::ToolMenuItem& entry : ui::tool_menu(static_cast<ui::ParkTool>(t)))
                tools.set_available(
                    entry.item,
                    entry.item == ui::ToolItem::ShapeGround || entry.item == ui::ToolItem::PaintGround || entry.item == ui::ToolItem::Water || entry.item == ui::ToolItem::People || entry.item == ui::ToolItem::Scenery || entry.item == ui::ToolItem::SeeThroughGround || entry.item == ui::ToolItem::Grid || entry.item == ui::ToolItem::Contours || entry.item == ui::ToolItem::PathHeights || entry.item == ui::ToolItem::RideHeights || entry.item == ui::ToolItem::Supports || entry.item == ui::ToolItem::Options
                        || entry.item == ui::ToolItem::About || entry.item == ui::ToolItem::Save || entry.item == ui::ToolItem::Load || ui::build_list(entry.item) || ui::management_topic(entry.item));
        }
        scene.desktop->root().add<ui::ParkBar>(std::string(kBarName), skins::park_bar_style(), strings);
        scene.desktop->root().add<ui::InfoPanel>(std::string(kInfoName), skins::info_panel_style());
        ui::InfoPanelStyle placing = skins::info_panel_style();
        placing.left = true;
        placing.raise = skins::land_panel_style().bottom - ui::info_panel::kDiscMargin; // Over the tool row, as the land panel stands
        scene.desktop->root().add<ui::InfoPanel>(std::string(kPlacingName), std::move(placing));
        scene.desktop->root().add<ui::FootpathPanel>(std::string(kFootpathPanelName), skins::footpath_panel_style(), strings);
        scene.attractions = park::attraction_objects(scene.rides, scene.objects);
        for (std::size_t k = 0; k < scene.objects.size(); ++k)
            animate_scenery(device, scene, k);
        auto people = profiling::section("ReadPeople", [&] { return save::load_people(*file, index); });
        // Saves made before people were kept start them again from the original park.
        const parkfile::SavedPeople* kept = saved && saved->people ? &*saved->people : nullptr;
        if (people && kept != nullptr)
            *people = kept->model;
        if (people)
        {
            scene.people = std::move(*people);
            load_people_models(device, scene, install, seed);
            profiling::section("Simulation", [&] {
                scene.sim.seed = seed;
                scene.sim.ratings = ratings;
                simulation::import_paths(scene.sim, *world);
                // The saved ones by their place in scene.people, null for those not drawn.
                const auto by_look = [](std::size_t count, std::span<const entt::entity> placed, const auto& records) {
                    std::vector<entt::entity> out(count, entt::null);
                    for (std::size_t i = 0; i < placed.size() && i < records.size(); ++i)
                    {
                        if (const std::int32_t look = records[i].look; look >= 0 && std::cmp_less(look, count))
                            out[static_cast<std::size_t>(look)] = placed[i];
                    }
                    return out;
                };
                const auto placed = kept != nullptr ? parkfile::place_guests(scene.sim, *kept) : std::vector<entt::entity> { };
                const auto guests = kept != nullptr ? by_look(scene.people.guests.size(), placed, kept->guests) : simulation::import_guests(scene.sim, scene.people);
                simulation::import_stalls(scene.sim, scene.rides);
                const auto timings = simulation::flat_ride_timings(scene.rides, scene.objects, [&](const save::PlacedObject& o) -> const park::ObjectModel* {
                    const auto found = scene.models.find(park::model_key(o));
                    return found != scene.models.end() ? &found->second : nullptr;
                });
                // The courses of the rides whose trains go round, for the simulation to run them on.
                std::map<std::string, std::optional<park::PieceCatalogue>, std::less<>> catalogues;
                const park::PieceShapeOf shape_of = [&](const save::TrackPiece& piece, const save::PlacedObject& object) -> std::optional<const park::PieceShape&> {
                    auto [at, added] = catalogues.try_emplace(object.overlay);
                    if (added)
                    {
                        if (const auto file = install.find(object.overlay + ".common.ovl"))
                        {
                            if (auto loaded = park::load_piece_catalogue(*file, object.overlay))
                                at->second = std::move(*loaded);
                        }
                    }
                    return at->second ? park::shape_named(*at->second, piece.symbol) : std::nullopt;
                };
                std::vector<std::optional<simulation::Coaster>> coasters(scene.rides.rides.size());
                scene.courses.assign(scene.rides.rides.size(), std::nullopt);
                for (std::size_t i = 0; i < scene.rides.rides.size(); ++i)
                {
                    if (auto course = park::track_course(scene.rides.rides[i], scene.objects, scene.models, scene.grid, scene.fitted, shape_of))
                    {
                        coasters[i] = simulation::coaster_of(*course, scene.rides.rides[i]);
                        if (coasters[i])
                            scene.courses[i] = std::move(*course);
                    }
                }
                const auto guests_room = simulation::ride_guests(scene.rides, [&](std::string_view path) { return install.find(path); });
                scene.ride_entities = simulation::import_rides(scene.sim, scene.rides, timings, coasters, guests_room);
                for (std::size_t i = 0; i < scene.car_spots.size(); ++i)
                    if (const std::size_t ride = scene.car_spots[i].ride; ride < scene.ride_entities.size() && scene.ride_entities[ride] != entt::null)
                        scene.ride_cars[scene.ride_entities[ride]].push_back(i);
                for (std::size_t i = 0; i < scene.rides.rides.size(); ++i)
                    seat_flat_ride(device, scene, i);
                simulation::start_guests(scene.sim);
                if (kept != nullptr)
                    parkfile::place_parties(scene.sim, *kept, placed);
                else
                {
                    simulation::board_saved_riders(scene.sim, scene.people, guests, scene.rides, scene.ride_entities);
                    simulation::import_parties(scene.sim, scene.people, guests);
                }
                simulation::import_litter(scene.sim, scene.people, scene.park.time);
                simulation::import_path_items(scene.sim, scene.objects, scene.bins, world->benches, scene.signs);
                simulation::import_cover(scene.sim, [&](std::span<const glm::vec3> points) { return park::overheads(points, scene.objects, scene.models, scene.grid, scene.fitted); });
                simulation::import_facilities(scene.sim, scene.rides, [&](const save::Ride& ride) -> std::optional<std::uint32_t> {
                    const auto file = install.find(ride.base.overlay_path + ".common.ovl");
                    return file ? park::special_attraction_kind(*file, std::string_view(ride.base.symbol).substr(0, ride.base.symbol.find(':'))) : std::nullopt;
                });
                simulation::import_sights(scene.sim, scene.objects, [&](const save::PlacedObject& o) -> std::optional<simulation::PathItemKind> {
                    const auto model = scene.models.find(park::model_key(o));
                    if (model == scene.models.end() || !park::is_sight(model->second.category))
                        return std::nullopt;
                    return model->second.photo_spot ? simulation::PathItemKind::PhotoSpot : simulation::PathItemKind::Sight;
                });
                simulation::import_gate(scene.sim, scene.objects, [&](const save::PlacedObject& o) {
                    const auto model = scene.models.find(park::model_key(o));
                    return model != scene.models.end() && model->second.category == std::to_underlying(park::ItemCategory::ParkEntrance);
                });
                const auto staff = kept != nullptr ? by_look(scene.people.staff.size(), parkfile::place_staff(scene.sim, *kept), kept->staff) : simulation::import_staff(scene.sim, scene.people);
                for (const auto [e, upkeep] : scene.sim.registry.view<const simulation::Upkeep>().each())
                    if (upkeep.broken)
                        scene.broken_rides.push_back(e);
                for (ShownPerson& p : scene.shown_people)
                {
                    if (!p.staff && p.saved < guests.size() && guests[p.saved] != entt::null && scene.sim.registry.all_of<simulation::Stride>(guests[p.saved]))
                        p.guest = guests[p.saved];
                    else if (p.staff && p.saved < staff.size())
                        p.guest = staff[p.saved];
                }
            });
        }
        else
            log.warning("people: {}", save::to_string(people.error()));

        auto tutorial = save::load_tutorial(*file);
        if (!tutorial)
            log.warning("tutorial: {}", save::to_string(tutorial.error()));
        else if (*tutorial)
            scene.desktop->root().add<ui::TutorialGuide>(std::string(kGuideName), skins::tutorial_guide_style(), strings, std::move(**tutorial));
        scene.desktop->tooltip_style = skins::tooltip_style();
        scene.map = park_map(scene.grid, world->paths, world->water, scene.objects, scene.models, scene.fitted, scene.attractions, scene.rides, images, install);
        log.info("{}: {} by {} tiles, {} ground batches", path.filename().string(), scene.grid.width, scene.grid.height, scene.batches.size());
        const auto drawn = std::ranges::count_if(scene.levels.steady, [](std::int8_t l) { return l != park::kNotDrawn; }) + std::ranges::count_if(scene.levels.finer, [](std::int8_t l) { return l != park::kNotDrawn; });
        const auto modelled = std::ranges::count_if(scene.objects, [&](const save::PlacedObject& o) { return !o.hidden && scene.models.contains(park::model_key(o)); });
        const auto shown = std::ranges::count_if(scene.objects, [](const save::PlacedObject& o) { return !o.hidden; });
        log.info("{} objects in view, {} without a model, {} models, {} pictures", drawn, shown - modelled, scene.models.size(), scene.object_textures.size());
        return scene;
    }

    void destroy_park_scene(render::Device& device, ParkScene& scene)
    {
        finish_builds(scene);
        destroy_park_water(device, scene.water);
        destroy_led_signs(device, scene.led_signs);
        destroy_park_glow(device, scene.glow);
        destroy_park_occlusion(device, scene.occlusion);
        destroy_save_browser(device, scene.saves);
        for (const render::BufferHandle b : { scene.ground_vertices, scene.ground_indices, scene.steady_objects.vertices, scene.steady_objects.indices, scene.finer_objects.vertices, scene.finer_objects.indices, scene.coarser_objects.vertices, scene.coarser_objects.indices })
        {
            if (b)
                device.destroy_buffer(b);
        }
        for (const render::PipelineHandle p : { scene.ground_pipeline, scene.ground_depth_pipeline, scene.ground_faded_pipeline, scene.object_pipeline, scene.cutout_pipeline, scene.see_through_pipeline, scene.crossing_pipeline, scene.crossing_cutout_pipeline, scene.crossing_see_through_pipeline, scene.sky_pipeline })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const auto& [id, p] : scene.front_only)
            device.destroy_pipeline(p);
        scene.front_only.clear();
        destroy_park_map(device, scene.map);
        drop_view_lines(device, scene);
        ui::destroy_textures(device, scene.ground_textures);
        ui::destroy_textures(device, scene.object_textures);
        ui::destroy_textures(device, scene.people_textures);
        ui::destroy_textures(device, scene.sky_textures);
        for (const render::TextureHandle t : { scene.blank, scene.variation })
        {
            if (t)
                device.destroy_texture(t);
        }
        if (scene.view_target)
            device.destroy_texture(scene.view_target);
        if (scene.placing.preview_target)
            device.destroy_texture(scene.placing.preview_target);
        if (scene.photo_target)
            device.destroy_texture(scene.photo_target);
        for (const render::TextureHandle t : { scene.shadow_drawings[0].target, scene.shadow_drawings[0].view, scene.shadow_drawings[1].target, scene.shadow_drawings[1].view })
        {
            if (t)
                device.destroy_texture(t);
        }
        scene = { };
    }

} // namespace rct3::scene
