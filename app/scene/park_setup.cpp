// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_setup.hpp"

#include "core/maybe.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_pixels.hpp"
#include "park/asset_index.hpp"
#include "park/boundary_fence.hpp"
#include "park/build_catalogue.hpp"
#include "park/chain_lifts.hpp"
#include "park/enclosures.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/ground_textures.hpp"
#include "park/ground_variation.hpp"
#include "park/item_category.hpp"
#include "park/object_models.hpp"
#include "park/off_map_trees.hpp"
#include "park/overhead.hpp"
#include "park/path_decks.hpp"
#include "park/path_tunnels.hpp"
#include "park/person_motions.hpp"
#include "park/queue_fences.hpp"
#include "park/relief_maps.hpp"
#include "park/ride_cars.hpp"
#include "park/ride_fences.hpp"
#include "park/ride_walks.hpp"
#include "park/sky_models.hpp"
#include "park/support_columns.hpp"
#include "park/surrounding_land.hpp"
#include "park/water_flow.hpp"
#include "park/waterfall_mesh.hpp"
#include "parkfile/saved_park.hpp"
#include "profiling/profiler.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/tutorial.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "scene/enclosure_tools.hpp"
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
#include "strings/ascii.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/management_pages.hpp"
#include "ui/screens/message_drawer.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/screens/tutorial_guide.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
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
                if (!std::ranges::contains(folders, name))
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
            const auto views = folders | std::ranges::to<std::vector<std::string_view>>();
            park::AssetIndex index = park::AssetIndex::scan(install.root(), views);
            add_referenced(index, install);
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
                return std::unexpected(std::format("cannot open {}", path.string()));
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
            auto pictures = park::paint_batches(batches, loader.pictures());
            fit_texture_detail(scene, pictures);
            if (auto uploaded = ui::upload_textures(device, pictures))
                scene.people_textures.merge(*uploaded);
            else
                logging::logger("park").warning("umbrella pictures: {}", uploaded.error());
        }

        /// Reads the models of the park's guests and staff and uploads their pictures.
        void load_people_models(render::Device& device, ParkScene& scene, const config::GameInstall& install, random::Seed seed)
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
            scene.looks = random::stream(seed, random::RandomStream::Effects);
            random::RandomState& colours = scene.looks;
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
            // What guests coming in at the gate wear: every head, body and legs in the guests' folders, one
            // folder for each age and sex (such as "AF" for adult women), files such as "AF01_Head_Stub". Each
            // becomes a part group of the park's people, so the new guests are kept with the park.
            // New groups take ids past those the game saves (32 bits) and those already in the park.
            save::PeopleObjectId next_id = save::PeopleObjectId { 1 } << 32;
            for (const save::PartGroup& g : scene.people.part_groups)
                next_id = std::max(next_id, g.id + 1);
            std::error_code error;
            for (const auto& age : std::filesystem::directory_iterator(*folder, error))
            {
                const std::string name = age.path().filename().string();
                // The celebrities VIP goals send each have a folder of their own, by the goals' numbers from 1.
                constexpr std::array<std::string_view, 6> kCelebrities { "Politician", "PopStar", "MovieStar", "JoeSlugger", "BobWaterman", "Smythe" };
                const auto celebrity = std::ranges::find(kCelebrities, name);
                const auto vip = celebrity != kCelebrities.end() ? static_cast<std::uint8_t>(celebrity - kCelebrities.begin() + 1) : std::uint8_t { 0 };
                auto type = name.size() == 2 ? std::string_view("CTA").find(name[0]) : std::string_view::npos;
                auto sex = name.size() == 2 ? std::string_view("MF").find(name[1]) : std::string_view::npos;
                if (vip != 0)
                {
                    // Saved as the game saves them: of the special persons, their number in place of a sex.
                    type = std::to_underlying(save::PersonType::Special);
                    sex = vip;
                }
                // Animal keepers, hired with each animal house, wear their own.
                const bool keeper = name == "AnimalKeeper";
                if (keeper)
                {
                    type = std::to_underlying(save::PersonType::AnimalKeeper);
                    sex = std::to_underlying(save::Sex::Male);
                }
                // Vendors, hired with stalls and ride entrances, wear their own; men's and women's share a folder.
                const bool vendor = name == "VM";
                if (vendor)
                    type = std::to_underlying(save::PersonType::Vendor);
                if (type == std::string_view::npos || sex == std::string_view::npos)
                    continue;
                for (const auto& file : std::filesystem::directory_iterator(age.path(), error))
                {
                    constexpr std::string_view kStub = "_Stub.common.ovl";
                    constexpr std::array<std::string_view, 3> kSections { "_Head", "_Body", "_Legs" };
                    const std::string file_name = file.path().filename().string();
                    if ((vip == 0 && !keeper && !vendor && !file_name.starts_with(name)) || !file_name.ends_with(kStub))
                        continue;
                    const auto worn_by = vendor && file_name.size() > 1 ? std::string_view("MF").find(file_name[1]) : sex;
                    if (worn_by == std::string_view::npos)
                        continue;
                    const std::string_view symbol = std::string_view(file_name).substr(0, file_name.size() - kStub.size());
                    const auto section = std::ranges::find_if(kSections, [&](std::string_view s) { return symbol.ends_with(s); });
                    const park::PartSet* part = section != kSections.end() ? as_pointer(library.part_set(symbol)) : nullptr;
                    if (part == nullptr)
                        continue;
                    // A group the park already has keeps its id; symbols are matched in any case.
                    const auto same = [&](const save::PartGroup& g) { return std::ranges::equal(g.symbol, symbol, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }); };
                    const auto known = std::ranges::find_if(scene.people.part_groups, same);
                    const save::PeopleObjectId id = known != scene.people.part_groups.end() ? known->id : next_id++;
                    if (known == scene.people.part_groups.end())
                        scene.people.part_groups.push_back({ .id = id, .symbol = std::string(symbol), .section = static_cast<std::uint8_t>(section - kSections.begin()), .sex = static_cast<std::uint8_t>(worn_by), .type = static_cast<std::uint8_t>(type) });
                    scene.wardrobe.push_back({ id, part, static_cast<save::PersonType>(type), vip != 0 ? save::Sex::Male : static_cast<save::Sex>(worn_by), static_cast<std::uint8_t>(section - kSections.begin()), vip });
                }
            }
            library.close_files();
            park::GroundImages shrunk; // Copied only when they are shrunk; the library keeps its own
            if (scene.texture_detail < 1)
            {
                shrunk = library.pictures();
                fit_texture_detail(scene, shrunk);
            }
            if (auto uploaded = ui::upload_textures(device, scene.texture_detail < 1 ? shrunk : library.pictures()))
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
            const simulation::DayMode day_mode = simulation::day_mode(mode);
            simulation::DayClock clock = simulation::start_day(time, simulation::day_window(day_mode, hours), simulation::day_transition(options.choice(config::GameOption::DayNight)));
            clock.mode = day_mode;
            clock.park_hours = hours;
            return clock;
        }

        constexpr float kTicketSpot = 1; ///< Along an entrance without a spot of its own, towards the ride

        /// Gives simulated ride `entity` its riders' ways on and off, as `ways_of` lays them from where `ride`'s
        /// first station has its entrance and exit, with the ticket spot the entrance's model has (its first
        /// seat). Nothing for a ride without both.
        template<typename WaysOf> void walk_ride(render::Device& device, ParkScene& scene, entt::entity entity, const save::Ride& ride, WaysOf&& ways_of)
        {
            if (ride.stations.empty() || !ride.stations.front().entrance_spot || !ride.stations.front().exit_spot)
                return;
            const auto entrance = std::ranges::find(scene.objects, ride.stations.front().entrance_spot->object, &save::PlacedObject::id);
            const auto exit = std::ranges::find(scene.objects, ride.stations.front().exit_spot->object, &save::PlacedObject::id);
            if (entrance == scene.objects.end() || exit == scene.objects.end())
                return;
            const auto model_of = [&](const save::PlacedObject& o) -> const park::ObjectModel* {
                const auto model = scene.models.find(park::model_ref(o));
                return model != scene.models.end() ? &model->second : nullptr;
            };
            const glm::mat4 in = park::placed_transform(*entrance, model_of(*entrance), scene.grid, scene.fitted);
            const glm::mat4 out = park::placed_transform(*exit, model_of(*exit), scene.grid, scene.fitted);
            park::RideWays ways = ways_of(in, out);
            ways.ticket = glm::vec3(in * glm::vec4(kTicketSpot, 0, 0, 1));
            ways.ticket_heading = std::atan2(in[0].x, in[0].z);
            if (model_of(*entrance) != nullptr && !model_of(*entrance)->seat_places.empty())
                park::set_ticket_spot(ways, in, model_of(*entrance)->seat_places.front());
            scene.sim.registry.emplace_or_replace<simulation::RideWalks>(entity, simulation::RideWalks { .ticket = ways.ticket, .ticket_heading = ways.ticket_heading, .on = std::move(ways.on), .off = std::move(ways.off) });
            // The entrance's and exit's gates, by the motions the buildings name ...Close, ...Open, and those ending
            // Idle that hold them there.
            for (const auto& [door, is_exit] : { std::pair { entrance, false }, std::pair { exit, true } })
            {
                const park::ObjectModel* model = model_of(*door);
                const auto k = static_cast<std::size_t>(door - scene.objects.begin());
                if (model == nullptr || !model->rig || std::ranges::contains(scene.boned_rides, k, &ParkScene::BonedRide::object))
                    continue;
                const auto motion = [&](std::string_view end) -> std::optional<std::size_t> {
                    const auto found = std::ranges::find_if(model->animation_names, [&](const std::string& name) { return name.ends_with(end); });
                    return found != model->animation_names.end() ? std::optional(static_cast<std::size_t>(found - model->animation_names.begin())) : std::nullopt;
                };
                const auto shut = motion("CloseIdle"), opening = motion("Open"), open = motion("OpenIdle"), shutting = motion("Close");
                if (!shut || !opening || !open || !shutting)
                    continue;
                ParkScene::BonedRide& boned = scene.boned_rides.emplace_back();
                boned.ride = entity;
                boned.object = k;
                boned.placed = is_exit ? out : in;
                std::tie(boned.mesh, boned.bones) = park::boned_meshes(scene.objects[k], *model, scene.grid);
                boned.first_seat = scene.seat_carriers.size();
                boned.reach = scene.grid.tile_size_x;
                boned.gate = { *shut, *opening, *open, *shutting };
                boned.exit = is_exit;
                add_textures(device, scene, boned.mesh.mesh.batches);
            }
        }

    } // namespace

    /// The relief map uploaded beside picture `name` (park/relief_maps.hpp), or none.
    render::TextureHandle relief_of(const ui::UiTextures& textures, std::string_view name)
    {
        // Kept per thread, so building the key reuses its memory instead of allocating on every draw.
        thread_local std::string key;
        key.assign(name).append(park::kReliefSuffix);
        const auto found = textures.find(key);
        return found != textures.end() ? found->second.handle : render::TextureHandle { };
    }

    void fit_texture_detail(const ParkScene& scene, park::GroundImages& images)
    {
        for (auto& [name, image] : images)
            image = ovl::halved(std::move(image), ovl::texture_halvings(scene.texture_detail, image.width, image.height));
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
        fit_texture_detail(scene, pictures);
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
            const auto model = scene.objects[k].owner_ref == ride.base.placement ? scene.models.find(park::model_ref(scene.objects[k])) : scene.models.end();
            if (model != scene.models.end() && !model->second.seat_places.empty())
            {
                scene.ride_seats[entity] = k;
                const glm::mat4 placed = park::placed_transform(scene.objects[k], &model->second, scene.grid, scene.fitted);
                walk_ride(device, scene, entity, ride, [&](const glm::mat4& entrance, const glm::mat4& exit) {
                    std::vector<glm::vec3> seats;
                    for (const glm::mat4& seat : model->second.seat_places)
                        seats.emplace_back((placed * seat)[3]);
                    return park::flat_ride_ways(park::placed_walkways(model->second.walkways, placed), seats, entrance, exit);
                });
                if (model->second.rig)
                {
                    ParkScene::BonedRide& boned = scene.boned_rides.emplace_back();
                    boned.ride = entity;
                    boned.object = k;
                    boned.placed = placed;
                    std::tie(boned.mesh, boned.bones) = park::boned_meshes(scene.objects[k], model->second, scene.grid);
                    boned.first_seat = scene.seat_carriers.size();
                    boned.seats = model->second.seat_bones.size();
                    scene.seat_carriers.resize(scene.seat_carriers.size() + boned.seats, boned.placed);
                    boned.views.assign(model->second.view_bones.size(), glm::mat4(1.0f));
                    add_textures(device, scene, boned.mesh.mesh.batches);
                }
                return;
            }
        }
    }

    void add_referenced(park::AssetIndex& index, const config::GameInstall& install)
    {
        std::set<std::string, std::less<>> tried_files;
        for (auto missing = index.missing_references(); !missing.empty(); missing = index.missing_references())
        {
            bool added = false;
            for (const std::string& reference : missing)
            {
                if (!tried_files.insert(reference).second)
                    continue;
                if (const auto file = install.find(std::format("{}.common.ovl", reference)))
                    added = index.add_file(*file) || added;
            }
            if (!added)
                break;
        }
    }

    void add_ride_cars(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::size_t from, std::size_t count)
    {
        std::uint64_t last = 0;
        for (const auto& list : { std::span<const save::PlacedObject>(scene.objects), std::span<const save::PlacedObject>(scene.cars) })
            for (const save::PlacedObject& o : list)
                last = std::max(last, o.id);
        park::RideCars cars = park::ride_cars(std::span(scene.rides.rides).subspan(from, count), last);
        for (park::CarSpot& spot : cars.spots)
            spot.ride += from;
        load_models(install, cars.pieces, false, scene.models, scene.pictures, cars.designs);
        const std::size_t first = scene.cars.size();
        scene.cars.insert(scene.cars.end(), cars.pieces.begin(), cars.pieces.end());
        scene.car_spots.insert(scene.car_spots.end(), cars.spots.begin(), cars.spots.end());
        scene.saved_cars.merge(cars.fitted);
        scene.car_places.clear();
        for (const save::PlacedObject& car : scene.cars)
            scene.car_places.push_back(scene.saved_cars.at(car.id));
        scene.car_meshes = park::moving_meshes(scene.cars, scene.models, scene.grid);
        scene.car_lamps = std::ranges::any_of(scene.cars, [&](const save::PlacedObject& car) {
            const auto model = scene.models.find(park::model_ref(car));
            return model != scene.models.end() && !model->second.lights.empty();
        });
        for (std::size_t i = first; i < scene.cars.size(); ++i)
            if (const auto model = scene.models.find(park::model_ref(scene.cars[i])); model != scene.models.end() && !model->second.arm_joints.empty())
                scene.arm_cars.push_back({ .car = i, .joints = model->second.arm_joints });
        // Arms' joints follow the cars, each where its car stands until the cars are next placed.
        scene.car_mesh_places = scene.car_places;
        for (const ParkScene::ArmCar& arm : scene.arm_cars)
            scene.car_mesh_places.insert(scene.car_mesh_places.end(), arm.joints.size(), scene.car_places[arm.car]);
        profiling::section("Mesh", [&] { park::move_meshes(scene.car_meshes, scene.car_mesh_places); });
        add_textures(device, scene, scene.car_meshes.mesh.batches);
    }

    void replace_ride_cars(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::size_t ride_index)
    {
        // Each car's place once the ride's own are gone, by its place now.
        std::vector<std::optional<std::size_t>> kept(scene.cars.size());
        std::size_t next = 0;
        for (std::size_t i = 0; i < scene.cars.size(); ++i)
            if (scene.car_spots[i].ride != ride_index)
                kept[i] = next++;
            else
                scene.saved_cars.erase(scene.cars[i].id);
        for (std::size_t i = scene.cars.size(); i-- > 0;)
            if (!kept[i])
            {
                scene.cars.erase(scene.cars.begin() + static_cast<std::ptrdiff_t>(i));
                scene.car_spots.erase(scene.car_spots.begin() + static_cast<std::ptrdiff_t>(i));
            }
        std::erase_if(scene.arm_cars, [&](const ParkScene::ArmCar& arm) { return !kept[arm.car]; });
        for (ParkScene::ArmCar& arm : scene.arm_cars)
            arm.car = *kept[arm.car];
        for (auto& [_, cars] : scene.ride_cars)
        {
            std::erase_if(cars, [&](std::size_t car) { return !kept[car]; });
            for (std::size_t& car : cars)
                car = *kept[car];
        }
        if (scene.riding)
            scene.riding = kept[*scene.riding];
        add_ride_cars(device, scene, install, ride_index, 1);
        board_tracked_ride(device, scene, install, ride_index);
    }

    void load_car_models(ParkScene& scene, const config::GameInstall& install, const park::RideCars& cars)
    {
        load_models(install, cars.pieces, false, scene.models, scene.pictures, cars.designs);
    }

    void board_tracked_ride(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::size_t ride_index)
    {
        const entt::entity entity = ride_index < scene.ride_entities.size() ? scene.ride_entities[ride_index] : entt::null;
        if (entity == entt::null)
            return;
        std::vector<std::size_t>& cars = scene.ride_cars[entity];
        cars.clear();
        for (std::size_t i = 0; i < scene.car_spots.size(); ++i)
            if (scene.car_spots[i].ride == ride_index)
                cars.push_back(i);
        const auto* fleet = scene.sim.registry.try_get<simulation::Trains>(entity);
        const save::Ride& ride = scene.rides.rides[ride_index];
        if (cars.empty())
            scene.ride_cars.erase(entity);
        if (cars.empty() || fleet == nullptr || fleet->trains.empty() || !scene.courses[ride_index] || ride.trains.empty())
            return;
        // Riders of tracked rides board the first train as it waits at the station, seats front to back.
        // shortcut: rides built in the running park keep boarding straight in, upgrade with the station's
        // platform there.
        const auto file = install.find(std::format("tracks/Platforms/{}/{}.common.ovl", ride.platform_style, ride.platform_type));
        const auto platform = file ? park::load_platform(*file) : std::unexpected(std::string("no platform file"));
        walk_ride(device, scene, entity, ride, [&](const glm::mat4& entrance, const glm::mat4& exit) {
            std::vector<glm::vec3> seats;
            // A car without its model (from a file the game lacks) seats its share of the train at its middle.
            const auto first_train = std::ranges::count(cars, std::size_t { 0 }, [&](std::size_t car) { return scene.car_spots[car].train; });
            const std::size_t share = (scene.sim.registry.get<simulation::CycleRide>(entity).capacity + first_train - 1) / std::max<std::size_t>(first_train, 1);
            for (const std::size_t car : cars)
            {
                const park::CarSpot& spot = scene.car_spots[car];
                if (spot.train != 0)
                    continue;
                const auto model = scene.models.find(park::model_ref(scene.cars[car]));
                const glm::mat4 place = park::moved_along(*scene.courses[ride_index], scene.saved_cars.at(scene.cars[car].id), { spot.front, spot.rear }, fleet->trains.front().start - ride.trains.front().distance);
                if (model == scene.models.end())
                    seats.insert(seats.end(), share, glm::vec3(place[3]));
                else
                    for (const glm::mat4& seat : model->second.seat_places)
                        seats.emplace_back((place * seat)[3]);
            }
            return park::track_ride_ways(platform.value_or(park::Platform { }), seats, entrance, exit);
        });
    }

    void animate_scenery(render::Device& device, ParkScene& scene, std::size_t k)
    {
        const auto model = k < scene.objects.size() ? scene.models.find(park::model_ref(scene.objects[k])) : scene.models.end();
        // Attractions' parts move with their attractions (doors as guests pass, chains while the ride runs), so only
        // what the scenery lists hold moves on its own.
        if (model == scene.models.end() || !model->second.rig || scene.objects[k].hidden || !park::scenery_list(model->second.category) || scene.attractions.contains(scene.objects[k].id))
            return;
        ParkScene::BonedRide& boned = scene.boned_rides.emplace_back();
        boned.object = k;
        boned.placed = park::placed_transform(scene.objects[k], &model->second, scene.grid, scene.fitted);
        std::tie(boned.mesh, boned.bones) = park::boned_meshes(scene.objects[k], model->second, scene.grid);
        boned.first_seat = scene.seat_carriers.size();
        // Half its footprint's diagonal and a tile over, for what stands taller than wide.
        boned.reach = scene.grid.tile_size_x * (std::hypot(static_cast<float>(model->second.squares[0]), static_cast<float>(model->second.squares[1])) / 2 + 1);
        add_textures(device, scene, boned.mesh.mesh.batches);
    }

    std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const ui::Palette& palette, const config::GameOptions& options, const std::filesystem::path& path, random::Seed seed, const simulation::RatingModel& ratings, const ParkLoadReport& report)
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
        scene.texture_detail = options.get(config::GameOption::TextureDetail);
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
            const simulation::DayClock from_file = scene.day;
            const simulation::DayMode mode = saved->day_mode.value_or(from_file.mode);
            scene.day = simulation::start_day(saved->progress.day_time, simulation::day_window(mode, from_file.park_hours), from_file.transition);
            scene.day.mode = mode;
            scene.day.park_hours = from_file.park_hours;
        }
        begin(ParkLoadStage::Ground);
        const std::vector<park::GroundOpening> hollows = park::tunnel_hollows(world->paths, scene.grid);
        park::TerrainMesh mesh = profiling::section("Terrain", [&] { return park::terrain_mesh(scene.grid, world->water, hollows); });
        park::add_ground(mesh, park::tunnel_lining(hollows, scene.grid));
        // The park's own pools, then those the player laid, which the park keeps as squares.
        scene.pools = saved && saved->pools ? *saved->pools : park::PoolMap { };
        scene.saved_pool_water = world->pool_water.size();
        std::ranges::copy(park::pool_water(scene.pools), std::back_inserter(world->pool_water));
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
        scene.footpaths.waterfalls = saved && saved->waterfalls ? *saved->waterfalls : world->waterfalls;
        scene.enclosures = saved && saved->animals ? saved->animals->enclosures : world->enclosures;
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
        // The arrows the path tool and stall placing lay on the ground, by their own names.
        for (const std::string_view arrow : { park::kPathArrowPicture, park::kStallArrowPicture })
        {
            const auto tool = install.find(park::kPathToolFile);
            const auto added = tool ? park::add_named_image(images, *tool, arrow, arrow) : std::expected<bool, std::string>(false);
            if (!added || !*added)
                log.warning("{}: no {}", park::kPathToolFile, arrow);
        }
        fit_texture_detail(scene, images);
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
        if (const auto falls_file = install.find(park::kWaterfallFile))
        {
            // The waterfalls' pictures; without them the falls are drawn as plain see-through water.
            std::map<std::string, ovl::RgbaImage, std::less<>> pictures;
            for (const park::WaterfallPicture& picture : park::kWaterfallPictures)
                for (const auto& [texture, key] : { std::pair { picture.texture, picture.key }, std::pair { picture.layer_texture, picture.layer_key } })
                {
                    if (auto image = park::load_flow_picture(*falls_file, texture))
                        pictures.emplace(key, std::move(*image));
                    else
                        logging::logger("park").warning("waterfalls: {}", image.error());
                }
            if (auto uploaded = ui::upload_textures(device, pictures))
                scene.object_textures.merge(*uploaded);
        }
        scene.modelled_plants = options.flag(config::GameOption::Trees3D);
        load_models(install, world->objects, scene.modelled_plants, scene.models, scene.pictures);
        scene.objects = std::move(world->objects);
        scene.own_objects = scene.objects.size();
        scene.bins = std::move(world->bins);
        scene.signs = std::move(world->signs);
        if (saved)
        {
            scene.rides = std::move(saved->rides);
            scene.led_signs.hidden.insert(saved->no_queue_signs.begin(), saved->no_queue_signs.end());
            scene.broken_objects.insert(saved->broken_items.begin(), saved->broken_items.end());
        }
        else if (auto rides = save::load_rides(*file, index))
            scene.rides = std::move(*rides);
        else
            log.warning("rides: {}", save::to_string(rides.error()));
        if (saved && saved->shows)
            scene.shows = std::move(*saved->shows);
        else if (auto shows = save::load_shows(*file, index))
            scene.shows = std::move(*shows);
        else
            log.warning("shows: {}", save::to_string(shows.error()));
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
            if (const auto found = install.find(std::format("{}.common.ovl", park::kSupportOverlay)))
            {
                if (const auto sets = park::load_support_sets(*found))
                {
                    // Tops, caps and beams are fitted to the points they join, so their shapes are needed first.
                    std::vector<save::PlacedObject> sized;
                    const auto size = [&](const park::SupportPiece& piece) {
                        save::PlacedObject& o = sized.emplace_back();
                        o.symbol = piece.symbol;
                        o.overlay = std::string(park::kSupportOverlay);
                    };
                    for (const auto& [_, list] : *sets)
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
                    const auto started = std::chrono::steady_clock::now();
                    park::BuiltSupports columns = park::support_columns(scene.objects, scene.models, world->paths, scene.grid, *sets, largest_id(), supports_passed(scene));
                    log.info("supports built in {} us", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
                    for (std::size_t i = 0; i < columns.pieces.size(); ++i)
                        scene.columns.emplace(columns.pieces[i].id, columns.of[i].tile);
                    built = std::move(columns.pieces);
                    scene.fitted = std::move(columns.fitted);
                    scene.footpaths.support_sets = *sets;
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
                if (const auto found = scene.models.find(park::model_ref(o)); found != scene.models.end())
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
            // shortcut: built again when land is bought, not when a path is laid across the park's edge; follow
            // paths too when they can be laid there.
            static const std::string kNoFence;
            park::BoundaryFence boundary = park::boundary_fence(park_model ? park_model->park.fence_symbol : kNoFence, park_model ? park_model->park.fence_overlay : kNoFence, scene.grid, world->paths, largest_id());
            log.info("{} park fence panels", boundary.pieces.size());
            for (const save::PlacedObject& panel : boundary.pieces)
                scene.fence_panels.push_back(panel.id);
            const std::size_t first_panel = built.size();
            built.insert(built.end(), std::make_move_iterator(boundary.pieces.begin()), std::make_move_iterator(boundary.pieces.end()));
            scene.leans.merge(boundary.leans);
            // The animal enclosures' fences: the park keeps only which tiles are in which.
            park::BoundaryFence enclosed = park::enclosure_fences(scene.enclosures, scene.grid, largest_id());
            log.info("{} enclosure fence panels", enclosed.pieces.size());
            for (const save::PlacedObject& panel : enclosed.pieces)
                scene.enclosure_panels.push_back(panel.id);
            built.insert(built.end(), std::make_move_iterator(enclosed.pieces.begin()), std::make_move_iterator(enclosed.pieces.end()));
            scene.leans.merge(enclosed.leans);
            for (save::PlacedObject& piece : park::pool_objects(scene.pools, park::kPoolOverlay, largest_id()))
            {
                scene.pool_pieces.push_back(piece.id);
                built.push_back(std::move(piece));
            }
            load_models(install, built, false, scene.models, scene.pictures);
            // The park names only the fences, so their panels take the fences' own colours.
            for (std::size_t i = first_panel; i < built.size(); ++i)
            {
                if (const auto found = scene.models.find(park::model_ref(built[i])); found != scene.models.end())
                    built[i].colours = found->second.default_colours;
            }
            scene.objects.insert(scene.objects.end(), std::make_move_iterator(built.begin()), std::make_move_iterator(built.end()));
            add_ride_cars(device, scene, install, 0, scene.rides.rides.size());
            log.info("{} ride cars, {} on arms, {} vertices{}", scene.cars.size(), scene.arm_cars.size(), scene.car_meshes.own.size(), scene.car_lamps ? ", some with lamps" : "");
            refresh_pavings(scene);
        }
        if (options.flag(config::GameOption::OffMapTrees))
        {
            scene.off_map_stream = random::stream(seed, random::RandomStream::Scenery);
            std::vector<save::PlacedObject> trees = park::off_map_trees(scene.objects, scene.models, scene.grid, park::land_rings(scene.grid, scene.footpaths.water), *scene.off_map_stream);
            scene.off_map_groves = park::off_map_groves(scene.objects, scene.models, scene.grid);
            log.info("{} trees beyond the edge", trees.size());
            scene.objects.insert(scene.objects.end(), std::make_move_iterator(trees.begin()), std::make_move_iterator(trees.end()));
        }
        begin(ParkLoadStage::Scene);
        // The ground was drawn before the fences and walls were known; drawn again if one parts two paths.
        scene.footpaths.walls = park::edge_walls(scene.objects, scene.models, scene.grid);
        scene.footpaths.pavings_stale = scene.footpaths.pavings_stale || std::ranges::any_of(scene.footpaths.paths, [&](const save::PathTile& p) {
                                            return !p.elevation && std::ranges::any_of(std::array<std::uint8_t, 4> { 0, 1, 2, 3 }, [&](std::uint8_t side) {
                                                const auto edge = park::edge_height(p, scene.grid, side);
                                                return edge && park::walled(scene.footpaths.walls, { p.col, p.row }, side, *edge);
                                            });
                                        });
        scene.blend_band = options.get(config::GameOption::DetailBlendBand);
        scene.level_scales = level_scales(options);
        scene.anchors = park::object_anchors(scene.objects, scene.models, scene.grid, scene.fitted);
        scene.lamps = park::object_lamps(scene.objects, scene.models, scene.grid, scene.fitted);
        log.info("{} lamps", scene.lamps.size());
        load_park_particles(device, scene.particles, install.root(), random::stream(seed, random::RandomStream::Particles));
        // The tunes are the music files the install has, by name.
        std::error_code missing;
        for (const auto& file : std::filesystem::directory_iterator(install.root() / "Music", missing))
            if (strings::equals_ignoring_case(file.path().extension().string(), ".wma"))
                scene.tunes.push_back(file.path().stem().u16string());
        std::ranges::sort(scene.tunes);
        show_park_particles(scene.particles, park::object_effects(scene.objects, scene.models, scene.grid, scene.fitted));
        show_park_particles(scene.particles, park::waterfall_spray(scene.footpaths.waterfalls), &ParkParticles::sprays);
        const render::VertexLayout crossing_layout { sizeof(CrossingVertex), kCrossingAttributes };
        for (auto [pipeline, desc] : { std::pair { &scene.object_pipeline, render::PipelineDesc { .shader = render::Shader::Lit, .layout = ground_layout, .label = "objects" } }, std::pair { &scene.cutout_pipeline, render::PipelineDesc { .shader = render::Shader::LitCutout, .layout = ground_layout, .label = "object cut-outs" } },
                                       std::pair { &scene.see_through_pipeline, render::PipelineDesc { .shader = render::Shader::Lit, .layout = ground_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "see-through objects" } }, std::pair { &scene.crossing_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossing, .layout = crossing_layout, .label = "crossing objects" } },
                                       std::pair { &scene.crossing_cutout_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossingCutout, .layout = crossing_layout, .label = "crossing object cut-outs" } }, std::pair { &scene.crossing_see_through_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossing, .layout = crossing_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "crossing see-through objects" } },
                                       std::pair { &scene.depth_only_pipeline, render::PipelineDesc { .shader = render::Shader::Lit, .layout = ground_layout, .colour_write = false, .label = "depth-only objects" } }, std::pair { &scene.crossing_depth_only_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossing, .layout = crossing_layout, .colour_write = false, .label = "crossing depth-only objects" } },
                                       std::pair { &scene.chrome_pipeline, render::PipelineDesc { .shader = render::Shader::LitChrome, .layout = ground_layout, .label = "chrome objects" } }, std::pair { &scene.crossing_chrome_pipeline, render::PipelineDesc { .shader = render::Shader::LitCrossingChrome, .layout = crossing_layout, .label = "crossing chrome objects" } },
                                       std::pair { &scene.waterfall_added_pipeline, render::PipelineDesc { .shader = render::Shader::LitLayerAdded, .layout = ground_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "waterfalls" } }, std::pair { &scene.waterfall_multiplied_pipeline, render::PipelineDesc { .shader = render::Shader::LitLayerMultiplied, .layout = ground_layout, .blend = render::BlendMode::Alpha, .depth_write = false, .label = "waterfall foam" } } })
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
        // The surroundings chrome parts mirror, from the install's picture for chrome; without it they draw white.
        if (const auto main = install.find(ui::game_skins::kMainOverlay))
        {
            if (auto overlay = ovl::Overlay::load(*main))
            {
                const ovl::TexturePixels pixels = ovl::TexturePixels::index(*overlay);
                std::map<std::string, ovl::RgbaImage, std::less<>> chrome;
                for (const auto part : { maybe(&overlay->common()), overlay->unique() })
                {
                    if (!part)
                        continue;
                    for (const ovl::TextureItem& item : ovl::read_textures(*overlay, *part))
                        if (auto image = item.texture && item.name == "EnvMapChrome" ? pixels.first_frame(*item.texture) : std::nullopt)
                            chrome.insert_or_assign(std::string(park::kChromePicture), std::move(*image));
                }
                if (auto uploaded = ui::upload_textures(device, chrome))
                    scene.object_textures.merge(*uploaded);
            }
        }
        profiling::section("ObjectBuffers", [&] {
            if (auto built = build_objects(scene, park::eye(scene.camera), { }, scene.blend_band, scene.level_scales, scene.show_supports, { }, park::chain_runs(scene.rides)))
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
            if (saved && saved->goals)
            {
                scene.park_model.objectives = std::move(saved->goals->objectives);
                scene.park_model.finance.max_loan = saved->goals->max_loan;
                scene.park_model.finance.interest_rate_percent = saved->goals->interest_rate_percent;
            }
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
        scene.desktop->root().add<ui::MoneyFloaters>(std::string(kMoneyName)).stays_behind = true;
        // Behind the bar, which it hangs from.
        // Under the bar, even while it is dragged.
        scene.desktop->root().add<ui::MessageDrawer>(std::string(kMessagesName), skins::message_drawer_style(palette), strings).stays_behind = true;
        auto& tools = scene.desktop->root().add<ui::ToolRow>(std::string(kToolsName), skins::tool_row_style(palette), strings);
        // Only these menu items do anything yet; the rest show faded until their tools exist.
        for (std::size_t t = 0; t < ui::kParkToolCount; ++t)
        {
            for (const ui::ToolMenuItem& entry : ui::tool_menu(static_cast<ui::ParkTool>(t)))
                tools.set_available(
                    entry.item,
                    entry.item == ui::ToolItem::DeleteOne || entry.item == ui::ToolItem::DeleteArea || entry.item == ui::ToolItem::ShapeGround || entry.item == ui::ToolItem::PaintGround || entry.item == ui::ToolItem::Water || entry.item == ui::ToolItem::Waterfalls || entry.item == ui::ToolItem::People || entry.item == ui::ToolItem::Scenery || entry.item == ui::ToolItem::SeeThroughGround || entry.item == ui::ToolItem::Grid || entry.item == ui::ToolItem::Contours || entry.item == ui::ToolItem::PathHeights
                        || entry.item == ui::ToolItem::RideHeights || entry.item == ui::ToolItem::Supports || entry.item == ui::ToolItem::Options || entry.item == ui::ToolItem::About || entry.item == ui::ToolItem::Save || entry.item == ui::ToolItem::Load || entry.item == ui::ToolItem::SavedStructures || ui::build_list(entry.item) || ui::management_topic(entry.item));
        }
        scene.desktop->root().add<ui::ParkBar>(std::string(kBarName), skins::park_bar_style(palette), strings);
        // Both round panels stand over the tool row, as the path and land panels do.
        ui::InfoPanelStyle info = skins::info_panel_style(palette);
        info.raise = skins::kOverToolRow - ui::info_panel::kDiscMargin;
        scene.desktop->root().add<ui::InfoPanel>(std::string(kInfoName), info);
        ui::InfoPanelStyle placing = std::move(info);
        placing.left = true;
        scene.desktop->root().add<ui::InfoPanel>(std::string(kPlacingName), std::move(placing));
        scene.desktop->root().add<ui::FootpathPanel>(std::string(kFootpathPanelName), skins::footpath_panel_style(palette), strings);
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
            load_zoo_animals(scene, install);
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
                    const auto found = scene.models.find(park::model_ref(o));
                    return found != scene.models.end() ? &found->second : nullptr;
                });
                // The courses of the rides whose trains go round, for the simulation to run them on.
                std::map<std::string, std::optional<park::PieceCatalogue>, std::less<>> catalogues;
                const park::PieceShapeOf shape_of = [&](const save::TrackPiece& piece, const save::PlacedObject& object) -> std::optional<const park::PieceShape&> {
                    auto [at, added] = catalogues.try_emplace(object.overlay);
                    if (added)
                    {
                        if (const auto file = install.find(std::format("{}.common.ovl", object.overlay)))
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
                        scene.courses[i] = std::move(*course);
                    }
                }
                const auto guests_room = simulation::ride_guests(scene.rides, [&](std::string_view path) { return install.find(path); });
                scene.ride_entities = simulation::import_rides(scene.sim, scene.rides, timings, coasters, guests_room);
                for (const auto kind : simulation::ride_kinds(scene.rides, [&](std::string_view path) { return install.find(path); }))
                    scene.ride_groups.push_back(kind ? simulation::ride_group(*kind) : simulation::RideGroup::Other);
                simulation::import_boats(scene.sim, scene.rides, scene.ride_entities, scene.courses, scene.grid, world->water.bodies);
                simulation::link_stations(scene.sim, scene.ride_entities, scene.courses, scene.grid);
                for (const save::RideShow& show : scene.shows)
                    stage_show(scene, show);
                for (std::size_t i = 0; i < scene.rides.rides.size(); ++i)
                {
                    seat_flat_ride(device, scene, i);
                    board_tracked_ride(device, scene, install, i);
                }
                simulation::start_guests(scene.sim);
                if (kept != nullptr)
                    parkfile::place_parties(scene.sim, *kept, placed);
                else
                {
                    simulation::board_saved_riders(scene.sim, scene.people, guests, scene.rides, scene.ride_entities);
                    simulation::import_parties(scene.sim, scene.people, guests);
                }
                simulation::import_litter(scene.sim, scene.people, scene.park.time);
                // The benches the game's park lists, and those placed in ours, which it does not.
                std::vector<std::uint64_t> benches = world->benches;
                for (const save::PlacedObject& o : scene.objects)
                {
                    const auto model = scene.models.find(park::model_ref(o));
                    if (!o.hidden && model != scene.models.end() && model->second.category == std::to_underlying(park::ItemCategory::Bench) && !std::ranges::contains(benches, o.id))
                        benches.push_back(o.id);
                }
                simulation::import_path_items(scene.sim, scene.objects, scene.bins, benches, scene.signs);
                // What vandals broke stays broken until mended.
                for (auto [e, item] : scene.sim.registry.view<simulation::PathItem>().each())
                    item.broken = scene.broken_objects.contains(item.object);
                for (const save::PlacedObject& o : scene.objects)
                    if (const auto wall = park::edge_wall(o, scene.models, scene.grid))
                        simulation::set_wall(scene.sim, *wall, true);
                simulation::import_cover(scene.sim, [&](std::span<const glm::vec3> points) { return park::overheads(points, scene.objects, scene.models, scene.grid, scene.fitted); });
                // Each kind's rules read once, however many of its rides the park has.
                std::map<std::pair<std::string, std::string>, park::TrackRules> kinds;
                for (const save::Ride& ride : scene.rides.rides)
                {
                    const auto key = std::pair { ride.base.overlay_path, ride.base.symbol };
                    if (ride.kind == save::RideKind::Tracked && !kinds.contains(key))
                    {
                        const auto file = install.find(std::format("{}.common.ovl", ride.base.overlay_path));
                        kinds[key] = file ? park::load_track_rules(*file, ride.base.symbol) : park::TrackRules { };
                    }
                    scene.track_rules.push_back(kinds.contains(key) ? kinds[key] : park::TrackRules { });
                }
                simulation::import_facilities(scene.sim, scene.rides, [&](const save::Ride& ride) -> std::optional<std::uint32_t> {
                    const auto file = install.find(std::format("{}.common.ovl", ride.base.overlay_path));
                    return file ? park::special_attraction_kind(*file, std::string_view(ride.base.symbol).substr(0, ride.base.symbol.find(':'))) : std::nullopt;
                });
                simulation::import_sights(scene.sim, scene.objects, [&](const save::PlacedObject& o) -> std::optional<simulation::PathItemKind> {
                    const auto model = scene.models.find(park::model_ref(o));
                    if (model == scene.models.end() || !park::is_sight(model->second.category))
                        return std::nullopt;
                    return model->second.photo_spot ? simulation::PathItemKind::PhotoSpot : simulation::PathItemKind::Sight;
                });
                simulation::import_gate(scene.sim, scene.objects, [&](const save::PlacedObject& o) -> std::optional<std::array<std::int32_t, 2>> {
                    const auto model = scene.models.find(park::model_ref(o));
                    if (model == scene.models.end() || model->second.category != std::to_underlying(park::ItemCategory::ParkEntrance))
                        return std::nullopt;
                    return model->second.squares;
                });
                simulation::keep_staff_in(scene.sim, scene.grid);
                for (auto [e, gate] : scene.sim.registry.view<simulation::ParkGate>().each())
                    gate.closed = park_model && park_model->has_park && !park_model->park.flags.has(save::ParkFlag::Open);
                if (park_model && park_model->group_mix)
                    simulation::import_party_mix(scene.sim, *park_model->group_mix);
                const auto placed_staff = kept != nullptr ? parkfile::place_staff(scene.sim, *kept) : std::vector<entt::entity> { };
                const auto staff = kept != nullptr ? by_look(scene.people.staff.size(), placed_staff, kept->staff) : simulation::import_staff(scene.sim, scene.people);
                // The animals as our save kept them, else as the original park did.
                if (kept != nullptr && saved->animals)
                    parkfile::place_animals(scene.sim, *saved->animals, placed_staff);
                else
                {
                    const save::TerrainGrid& g = scene.grid;
                    const auto enclosure_at = [&](glm::vec2 p) {
                        const auto col = g.tile_size_x > 0 ? static_cast<std::int32_t>(std::floor((p.x - g.origin_x) / g.tile_size_x)) : -1;
                        const auto row = g.tile_size_z > 0 ? static_cast<std::int32_t>(std::floor((p.y - g.origin_z) / g.tile_size_z)) : -1;
                        return scene.enclosures.at(col, row);
                    };
                    // The park's own animal houses, beside the keepers' huts the park placed with them.
                    // shortcut: a house's ground is taken as its model's tiles round the tile it is placed on, with
                    // its troughs there too; read the saved troughs' places if animals end up eating off the house.
                    constexpr std::string_view kShelters = "Enclosures\\Shelters\\Shelters"; // As the park names it
                    constexpr std::string_view kHut = "WardensShed";
                    std::vector<simulation::AnimalHouse> houses;
                    for (const save::PlacedObject& o : scene.objects)
                    {
                        // The park keeps two records of each house, alike but for their ids.
                        if (o.overlay != kShelters || o.symbol == kHut || std::ranges::any_of(houses, [&](const simulation::AnimalHouse& h) { return h.troughs == tile_point(g, { o.col, o.row }); }))
                            continue;
                        const auto model = scene.models.find(park::model_key(o));
                        std::array<std::int32_t, 2> span = model != scene.models.end() ? model->second.squares : std::array<std::int32_t, 2> { 1, 1 };
                        if (o.rotation % 2 != 0)
                            std::swap(span[0], span[1]);
                        const glm::vec3 middle = tile_point(g, { o.col, o.row });
                        const glm::vec2 half { static_cast<float>(span[0]) * g.tile_size_x / 2, static_cast<float>(span[1]) * g.tile_size_z / 2 };
                        simulation::AnimalHouse house { .object = o.id, .enclosure = enclosure_at({ middle.x, middle.z }), .troughs = middle, .low = glm::vec2 { middle.x, middle.z } - half, .high = glm::vec2 { middle.x, middle.z } + half };
                        const auto hut = std::ranges::min_element(scene.objects, { }, [&](const save::PlacedObject& h) { return h.symbol == kHut ? std::abs(h.col - o.col) + std::abs(h.row - o.row) : std::numeric_limits<std::int32_t>::max(); });
                        if (hut != scene.objects.end() && hut->symbol == kHut && std::abs(hut->col - o.col) + std::abs(hut->row - o.row) <= span[0] + span[1])
                            house.hut = hut->id;
                        houses.push_back(house);
                    }
                    // Each keeper keeps the enclosure the park gave them.
                    std::vector<std::pair<entt::entity, std::int32_t>> keepers;
                    for (std::size_t i = 0; kept == nullptr && i < scene.people.staff.size() && i < staff.size(); ++i)
                        if (const auto* work = std::get_if<save::KeeperWork>(&scene.people.staff[i].work); work != nullptr && staff[i] != entt::null)
                            keepers.emplace_back(staff[i], work->enclosure_id);
                    simulation::import_animal_houses(scene.sim, houses, keepers);
                    simulation::import_animals(scene.sim, scene.people.zoo_animals, enclosure_at);
                }
                if (!scene.sim.registry.view<const simulation::Animal>().empty())
                {
                    const save::PlacedObject crate = animal_crate();
                    load_models(install, std::span(&crate, 1), false, scene.models, scene.pictures);
                    animals_changed(device, scene);
                }
                for (const auto [e, upkeep] : scene.sim.registry.view<const simulation::Upkeep>().each())
                {
                    if (upkeep.broken)
                        scene.broken_rides.push_back({ e });
                    if (upkeep.reliability.value < kLowReliability)
                        scene.unreliable_rides.push_back(e);
                }
                scene.known_guests.insert(guests.begin(), guests.end());
                for (ShownPerson& p : scene.shown_people)
                {
                    if (!p.staff && p.saved < guests.size() && guests[p.saved] != entt::null && scene.sim.registry.all_of<simulation::Stride>(guests[p.saved]))
                        p.guest = guests[p.saved];
                    // A vendor stands in their booth as saved, serving the riders handing over their tickets.
                    else if (p.staff && p.saved < staff.size() && staff[p.saved] != entt::null && (scene.sim.registry.all_of<simulation::Stride>(staff[p.saved]) || scene.sim.registry.get<simulation::Staff>(staff[p.saved]).role == simulation::StaffRole::Vendor || scene.sim.registry.all_of<simulation::KeeperDuty>(staff[p.saved])))
                    {
                        p.guest = staff[p.saved];
                        scene.known_guests.insert(p.guest); // A keeper is not hired again
                    }
                }
            });
        }
        else
            log.warning("people: {}", save::to_string(people.error()));

        auto tutorial = save::load_tutorial(*file);
        if (!tutorial)
            log.warning("tutorial: {}", save::to_string(tutorial.error()));
        else if (*tutorial)
            scene.desktop->root().add<ui::TutorialGuide>(std::string(kGuideName), skins::tutorial_guide_style(palette), strings, std::move(**tutorial));
        scene.desktop->tooltip_style = skins::tooltip_style(palette);
        scene.map = park_map(scene.grid, world->paths, world->water, scene.objects, scene.models, scene.fitted, scene.attractions, scene.rides, images, install);
        log.info("{}: {} by {} tiles, {} ground batches", path.filename().string(), scene.grid.width, scene.grid.height, scene.batches.size());
        const auto drawn = std::ranges::count_if(scene.levels.steady, [](std::int8_t l) { return l != park::kNotDrawn; }) + std::ranges::count_if(scene.levels.finer, [](std::int8_t l) { return l != park::kNotDrawn; });
        const auto modelled = std::ranges::count_if(scene.objects, [&](const save::PlacedObject& o) { return !o.hidden && scene.models.contains(park::model_key(o)); });
        const auto shown = std::ranges::count_if(scene.objects, [](const save::PlacedObject& o) { return !o.hidden; });
        log.info("{} objects in view, {} without a model, {} models, {} pictures", drawn, shown - modelled, scene.models.size(), scene.object_textures.size());
        watch_supports(scene);
        return scene;
    }

    void destroy_park_scene(render::Device& device, ParkScene& scene)
    {
        finish_builds(scene);
        destroy_park_water(device, scene.water);
        destroy_led_signs(device, scene.led_signs);
        destroy_park_glow(device, scene.glow);
        destroy_park_lens(device, scene.lens);
        destroy_park_particles(device, scene.particles);
        destroy_park_occlusion(device, scene.occlusion);
        destroy_save_browser(device, scene.saves);
        destroy_save_browser(device, scene.structures.browser);
        for (const render::BufferHandle b : { scene.ground_vertices, scene.ground_indices, scene.steady_objects.vertices, scene.steady_objects.indices, scene.finer_objects.vertices, scene.finer_objects.indices, scene.coarser_objects.vertices, scene.coarser_objects.indices })
        {
            if (b)
                device.destroy_buffer(b);
        }
        for (const render::PipelineHandle p : { scene.ground_pipeline, scene.ground_depth_pipeline, scene.ground_faded_pipeline, scene.object_pipeline, scene.cutout_pipeline, scene.see_through_pipeline, scene.crossing_pipeline, scene.crossing_cutout_pipeline, scene.crossing_see_through_pipeline, scene.depth_only_pipeline, scene.crossing_depth_only_pipeline, scene.chrome_pipeline, scene.crossing_chrome_pipeline, scene.waterfall_added_pipeline, scene.waterfall_multiplied_pipeline, scene.sky_pipeline })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const auto& [_, p] : scene.front_only)
            device.destroy_pipeline(p);
        scene.front_only.clear();
        destroy_park_map(device, scene.map);
        drop_view_lines(device, scene);
        ui::destroy_textures(device, scene.ground_textures);
        ui::destroy_textures(device, scene.object_textures);
        ui::destroy_textures(device, scene.people_textures);
        ui::destroy_textures(device, scene.drawn_animals.textures);
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
        for (const render::TextureHandle t : { scene.shadow_drawings[0].target, scene.shadow_drawings[1].target, scene.shadow_views, scene.lamp_texture })
        {
            if (t)
                device.destroy_texture(t);
        }
        scene = { };
    }

} // namespace rct3::scene
