// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_scene.hpp"

#include "logging/log.hpp"
#include "park/asset_index.hpp"
#include "park/build_catalogue.hpp"
#include "park/daylight.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/ground_textures.hpp"
#include "park/ground_variation.hpp"
#include "park/item_category.hpp"
#include "park/object_models.hpp"
#include "park/object_pick.hpp"
#include "park/object_placement.hpp"
#include "park/off_map_trees.hpp"
#include "park/overhead.hpp"
#include "park/path_supports.hpp"
#include "park/path_tunnels.hpp"
#include "park/person_motions.hpp"
#include "park/queue_fences.hpp"
#include "park/relief_maps.hpp"
#include "park/ride_cars.hpp"
#include "park/ride_fences.hpp"
#include "park/sky_look.hpp"
#include "park/sky_models.hpp"
#include "park/sun_view.hpp"
#include "park/surrounding_land.hpp"
#include "park/track_supports.hpp"
#include "park/water_flow.hpp"
#include "profiling/profiler.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/tutorial.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "scene/park_management.hpp"
#include "scene/park_patrol.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/import_people.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/scale.hpp"
#include "ui/screens/message_drawer.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/screens/tutorial_guide.hpp"
#include "ui/widgets/question_box.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <future>
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

        /// The game's ground pictures; the later file adds the expansion's.
        constexpr std::array<std::string_view, 2> kGroundFiles = {
            "terrain/RCT3/Terrain_RCT3.common.ovl",
            "terrain/CT/Terrain_CT.common.ovl",
        };

        constexpr std::string_view kGuideName = "tutorial";
        constexpr std::string_view kBarName = "bar";
        constexpr std::string_view kToolsName = "tools";
        constexpr float kPropViewClosest = 1.5f; ///< Small props fill the round view
        constexpr std::string_view kMessagesName = "messages";
        constexpr std::string_view kInfoName = "info";
        constexpr float kViewPitch = 0.5f;          ///< Radians down, of the info panel's round view
        constexpr float kViewTurn = 0.15f;          ///< Radians a second the round view turns round its attraction
        constexpr float kPersonViewDistance = 4.5f; ///< Of the round view's eye from the person it shows
        constexpr float kPersonViewHeight = 0.9f;   ///< Of the point it looks at, above the person's feet
        /// Of the pointer between pressing and letting go, in interface units, beyond which it is no click.
        constexpr float kClickSlack = 4;
        constexpr int kFahrenheit = 1; ///< The temperature units option's choice for Fahrenheit
        constexpr std::string_view kQuestionName = "leave_tutorial";
        constexpr std::string_view kLeaveText = "TutorialWindow_ConfirmQuit";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";

        // How far the controls move the camera.
        constexpr float kDragPan = 0.0025f;    ///< Per pixel, times the distance
        constexpr float kDragTurn = 0.006f;    ///< Radians per pixel
        constexpr float kKeyPan = 0.8f;        ///< Per second, times the distance
        constexpr float kEdgeBand = 12;        ///< Interface units in from a screen edge where the pointer pushes
        constexpr float kWheelZoom = 0.85f;    ///< Distance times this per notch towards the screen
        constexpr float kWheelSeconds = 0.25f; ///< How long a notch takes to roll in, nearly all of it
        constexpr float kKeyZoom = 2.5f;       ///< Times per second
        constexpr float kKeyTurn = 1.5f;       ///< Radians per second

        constexpr float kHoverPickInterval = 0.1f; ///< Seconds between looks at what is under the pointer, for its look

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

        template<typename T> std::expected<render::BufferHandle, std::string> upload(render::Device& device, render::BufferUsage usage, const std::vector<T>& items, std::string_view label)
        {
            return device.create_buffer({ usage, std::as_bytes(std::span(items)), label });
        }

        /// Paints and uploads the pictures of `batches` the scene does not have yet.
        /// The ground's grass and dirt get half the relief of walls and rock.
        constexpr float kGroundRelief = 0.35f;

        /// The relief map uploaded beside picture `name` (park/relief_maps.hpp), or none.
        render::TextureHandle relief_of(const ui::UiTextures& textures, std::string_view name)
        {
            const auto found = textures.find(std::string(name) + std::string(park::kReliefSuffix));
            return found != textures.end() ? found->second.handle : render::TextureHandle { };
        }

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

        /// How far away people are drawn at each level of detail, finest first; beyond the last they are not.
        constexpr std::array kPersonDistances { 75.0f, 200.0f, 600.0f };
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
                return it != groups.end() ? library.part_set(it->second) : nullptr;
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

        /// Whether a person standing at `at` may be in the view `transform` gives, with room to spare so people
        /// do not pop in at the edges while the view turns between posings.
        bool maybe_in_view(const glm::mat4& transform, const glm::vec3& at)
        {
            constexpr float kSpare = 1.3f;
            constexpr float kReach = 2.0f; // About a person's height, in clip units
            const glm::vec4 clip = transform * glm::vec4(at + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
            const float edge = clip.w * kSpare + kReach;
            return clip.w > -kReach && std::abs(clip.x) <= edge && std::abs(clip.y) <= edge;
        }

        constexpr std::size_t kNoCar = static_cast<std::size_t>(-1);

        /// Where rider carrier `k` (see ParkScene::rider_cars) stands now.
        glm::mat4 carrier_place(const ParkScene& scene, std::size_t k)
        {
            if (k == kNoCar)
                return glm::mat4(1.0f);
            if (k < scene.car_places.size())
                return scene.car_places[k];
            k -= scene.cars.size();
            return k < scene.seat_carriers.size() ? scene.seat_carriers[k] : glm::mat4(1.0f);
        }

        /// Where simulated guest `guest` sits while riding: the car, by its place in `cars`, and the seat in
        /// the car's own space that their place among the riders comes to, counting the train's seats front
        /// to back. On a ride without cars the car is kNoCar and the seat is in the park, on the ride as it is
        /// drawn. Nothing when not riding, or when the ride has no seat for them.
        /// shortcut: flat rides are drawn standing still, so their riders sit still too, upgrade when the
        /// rides' motions are played.
        std::optional<std::pair<std::size_t, glm::mat4>> seat_of(const ParkScene& scene, entt::entity guest)
        {
            const auto& r = scene.sim.registry;
            const auto* walking = r.try_get<simulation::Errand>(guest); // None once gone home
            if (walking == nullptr || walking->kind != simulation::ErrandKind::Riding || !r.all_of<simulation::CycleRide>(walking->target))
                return std::nullopt;
            const simulation::Errand& errand = *walking;
            // On a ride with trains, the train the guest boarded and their place among its riders.
            const auto* fleet = r.try_get<simulation::Trains>(errand.target);
            std::size_t train = 0;
            const std::vector<entt::entity>* riders = &r.get<simulation::CycleRide>(errand.target).riders;
            if (fleet != nullptr)
                for (std::size_t t = 0; t < fleet->riders.size(); ++t)
                    if (std::ranges::contains(fleet->riders[t], guest))
                    {
                        train = t;
                        riders = &fleet->riders[t];
                    }
            auto seat = static_cast<std::size_t>(std::ranges::find(*riders, guest) - riders->begin());
            if (const auto object = scene.ride_seats.find(errand.target); object != scene.ride_seats.end())
            {
                const save::PlacedObject& o = scene.objects[object->second];
                const auto model = scene.models.find(park::model_key(o));
                if (model == scene.models.end() || seat >= model->second.seat_places.size())
                    return std::nullopt;
                // On a ride its bones move, the seat's bone carries the seat.
                if (const auto boned = std::ranges::find(scene.boned_rides, object->second, &ParkScene::BonedRide::object); boned != scene.boned_rides.end())
                    return std::pair { scene.cars.size() + boned->first_seat + seat, model->second.seat_places[seat] };
                return std::pair { kNoCar, park::placed_transform(o, &model->second, scene.grid, scene.fitted) * model->second.seat_places[seat] };
            }
            const auto cars = scene.ride_cars.find(errand.target);
            if (cars == scene.ride_cars.end())
                return std::nullopt;
            for (const std::size_t car : cars->second)
            {
                if (fleet != nullptr && scene.car_spots[car].train != train)
                    continue;
                const auto model = scene.models.find(park::model_key(scene.cars[car]));
                const std::size_t seats = model != scene.models.end() ? model->second.seat_places.size() : 0;
                if (seat < seats)
                    return std::pair { car, model->second.seat_places[seat] };
                seat -= seats;
            }
            return std::nullopt;
        }

        /// Where simulated guest `guest` sits in the park while riding, as seat_of finds it.
        std::optional<glm::mat4> seat_place(const ParkScene& scene, entt::entity guest)
        {
            if (const auto seat = seat_of(scene, guest))
                return seat->first == kNoCar ? seat->second : carrier_place(scene, seat->first) * seat->second;
            return std::nullopt;
        }

        /// Where `shown` stands now: where the simulation has walked it while that runs, else where it was saved.
        /// Reads the simulation, so only on the frame's thread.
        glm::vec3 standing_at(const ParkScene& scene, const ShownPerson& shown)
        {
            if (scene.simulate && shown.guest != entt::null)
                if (const auto seat = seat_place(scene, shown.guest))
                    return glm::vec3((*seat)[3]);
            if (scene.simulate && shown.guest != entt::null)
                return simulation::between_steps(scene.sim.registry.get<simulation::Pose>(shown.guest), scene.sim_time / simulation::kTickSeconds).position;
            return glm::vec3(shown.pose.world[3]);
        }

        /// How guests walk holding an umbrella up, as the game's motions name it.
        const std::int32_t kUmbrellaMotion = park::motion_number("walkumbrella").value_or(park::motion::kWalk);
        /// How riders sit in a car, as the game's motions name it.
        const std::int32_t kRideMotion = park::motion_number("ridecar").value_or(park::motion::kIdle);
        /// What party members standing together in a ring do, in turns: chatting, laughing, nodding, pointing.
        const std::array<std::int32_t, 4> kChatMotions { park::motion::kIdle, park::motion_number("laugh").value_or(park::motion::kIdle), park::motion_number("nodhead").value_or(park::motion::kIdle), park::motion_number("point").value_or(park::motion::kIdle) };
        constexpr float kChatTurnSeconds = 4; ///< How long one of them keeps at one thing

        /// The motion of a simulated guest standing still: something from kChatMotions while their party
        /// stands together, else idling.
        std::int32_t chat_motion(const ParkScene& scene, const ShownPerson& shown)
        {
            const auto& r = scene.sim.registry;
            const auto* member = r.try_get<simulation::Member>(shown.guest);
            if (member == nullptr || r.get<simulation::Party>(member->party).huddle == 0)
                return park::motion::kIdle;
            // Each by turns, offset by who they are so they do not all laugh at once.
            const auto turn = static_cast<std::size_t>(scene.clock / kChatTurnSeconds) + entt::to_integral(shown.guest);
            return kChatMotions[turn % kChatMotions.size()];
        }

        /// Places the people near enough to see that stand still, or those that move and may be in `view`, as
        /// they are at `clock`, at `levels`. With `simulate`, the simulated guests count as moving and stand
        /// where the simulation has them. The still ones read only what is set when the park loads besides the
        /// arguments, so they may be placed beside the frame; the moving ones only on it.
        std::vector<park::PlacedPerson> place_people(const ParkScene& scene, const park::ObjectLevels& levels, float clock, bool moving, const glm::mat4& view, bool simulate)
        {
            std::vector<park::PlacedPerson> placed;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                const bool simulated = simulate && shown.guest != entt::null;
                if (levels[i] == park::kNotDrawn || (simulated || !shown.walking) != moving)
                    continue;
                park::PlacedPerson pose = shown.pose;
                pose.level = static_cast<std::size_t>(levels[i]);
                pose.time = shown.walking ? shown.saved_time : shown.saved_time + clock * shown.speed;
                if (simulated)
                {
                    const auto& r = scene.sim.registry;
                    const simulation::Pose at = simulation::between_steps(r.get<simulation::Pose>(shown.guest), scene.sim_time / simulation::kTickSeconds);
                    const auto& stride = r.get<simulation::Stride>(shown.guest);
                    const auto kind = r.get<simulation::Errand>(shown.guest).kind;
                    pose.time = shown.saved_time + clock * shown.speed;
                    if (kind == simulation::ErrandKind::Riding)
                        continue; // Seated riders go with their cars (pose_riders); the rest wait out of sight
                    const bool walking = stride.from != stride.to;
                    // People are modelled facing +z, which the heading turns towards the way they go.
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = walking ? park::motion::kWalk : chat_motion(scene, shown);
                    // An umbrella held up in the rain.
                    // shortcut: put away, out of sight, when dry; draw it carried closed if guests should be seen
                    // with one then.
                    const auto* weather = r.ctx().find<simulation::Weather>();
                    if (weather != nullptr && weather->rain && r.all_of<simulation::Umbrella>(shown.guest))
                    {
                        const auto* visitor = r.try_get<simulation::Visitor>(shown.guest);
                        const auto& umbrella = scene.umbrellas[visitor != nullptr && visitor->age == simulation::AgeGroup::Child ? 1 : 0];
                        pose.held = umbrella ? &*umbrella : nullptr;
                        // shortcut: standing guests hold it up in the walk's first pose, legs mid-step; upgrade if a
                        // standing motion with an umbrella turns up.
                        pose.motion = kUmbrellaMotion;
                        if (!walking)
                            pose.time = 0;
                    }
                }
                if (moving && !maybe_in_view(view, glm::vec3(pose.world[3])))
                    continue;
                placed.push_back(pose);
            }
            return placed;
        }

        /// Starts posing the riders seated in the ride cars away from the frame, each car's in the car's own
        /// space; place_cars moves them with their cars. Riders drawn at no level of detail are left out.
        std::future<RidersBuild> pose_riders(const ParkScene& scene)
        {
            std::map<std::size_t, std::vector<park::PlacedPerson>> by_car;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < scene.people_levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                if (shown.guest == entt::null || scene.people_levels[i] == park::kNotDrawn)
                    continue;
                const auto seat = seat_of(scene, shown.guest);
                if (!seat)
                    continue;
                park::PlacedPerson pose = shown.pose;
                pose.level = static_cast<std::size_t>(scene.people_levels[i]);
                pose.time = shown.saved_time + scene.clock * shown.speed;
                pose.world = seat->second;
                pose.motion = kRideMotion;
                by_car[seat->first].push_back(pose);
            }
            return std::async(std::launch::async, [&library = *scene.person_models, by_car = std::move(by_car)] {
                std::vector<park::ObjectMesh> alone;
                RidersBuild built;
                for (const auto& [car, riders] : by_car)
                {
                    alone.push_back(park::person_mesh(library, riders));
                    built.cars.push_back(car);
                }
                built.riders = park::gathered_meshes(alone);
                return built;
            });
        }

        /// Turns and moves the bones of the boned flat rides as far into their motions as their rides are, and
        /// the seats they carry.
        void swing_rides(ParkScene& scene)
        {
            for (ParkScene::BonedRide& boned : scene.boned_rides)
            {
                const auto model = scene.models.find(park::model_key(scene.objects[boned.object]));
                if (model == scene.models.end() || !model->second.rig)
                    continue;
                const auto* cycle = scene.simulate ? scene.sim.registry.try_get<simulation::CycleRide>(boned.ride) : nullptr;
                const bool riding = cycle != nullptr && cycle->timer > 0;
                const float into = riding ? static_cast<float>(cycle->cycle_ticks - cycle->timer) * simulation::kTickSeconds + scene.sim_time : 0.0f;
                const float length = cycle != nullptr ? static_cast<float>(cycle->cycle_ticks) * simulation::kTickSeconds : 0.0f;
                const auto [motion, time] = park::flat_ride_motion(model->second.animation_seconds, length, into, riding, scene.clock);
                const std::vector<glm::mat4> moves = park::bone_moves(*model->second.rig, motion, time);
                std::vector<glm::mat4> places(boned.bones.size());
                for (std::size_t p = 0; p < places.size(); ++p)
                    places[p] = boned.placed * moves[static_cast<std::size_t>(boned.bones[p])];
                park::move_meshes(boned.mesh, places);
                const auto& seat_bones = model->second.seat_bones;
                for (std::size_t seat = 0; seat < seat_bones.size(); ++seat)
                    if (seat_bones[seat] >= 0 && static_cast<std::size_t>(seat_bones[seat]) < moves.size())
                        scene.seat_carriers[boned.first_seat + seat] = boned.placed * moves[static_cast<std::size_t>(seat_bones[seat])];
            }
        }

        /// Moves the ride cars to where the simulation has their trains, or back to where the park kept them
        /// while it does not run.
        void place_cars(ParkScene& scene)
        {
            for (std::size_t i = 0; i < scene.cars.size(); ++i)
            {
                const std::uint64_t id = scene.cars[i].id;
                const park::CarSpot spot = scene.car_spots[i];
                glm::mat4 place = scene.saved_cars.at(id);
                const entt::entity ride = scene.simulate && spot.ride < scene.ride_entities.size() ? scene.ride_entities[spot.ride] : entt::null;
                const auto* fleet = ride != entt::null ? scene.sim.registry.try_get<simulation::Trains>(ride) : nullptr;
                if (fleet && spot.train < fleet->trains.size() && scene.courses[spot.ride])
                {
                    const simulation::Coaster& train = fleet->trains[spot.train];
                    // Between ticks, on as far as the train's speed takes it, from where the park kept it.
                    const float at = train.at + (train.running ? train.speed * scene.sim_time : 0.0f);
                    place = park::moved_along(*scene.courses[spot.ride], place, { spot.front, spot.rear }, at - scene.rides.rides[spot.ride].trains[spot.train].distance);
                }
                scene.car_places[i] = place;
            }
            profiling::section("Rides", [&] { swing_rides(scene); });
            profiling::section("Mesh", [&] {
                park::move_meshes(scene.car_meshes, scene.car_places);
                scene.rider_places.resize(scene.rider_cars.size());
                for (std::size_t k = 0; k < scene.rider_cars.size(); ++k)
                    scene.rider_places[k] = carrier_place(scene, scene.rider_cars[k]);
                park::move_meshes(scene.riders, scene.rider_places);
            });
        }

        /// Whether `build` has finished, without waiting for it.
        template<typename T> bool ready(const std::future<T>& build)
        {
            return build.valid() && build.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        /// Builds the people meshes again as often as they need, all beside the frame: the moving ones as soon as
        /// the last are done, the riders a few times a second, the still ones when a person's level of detail
        /// changes. The still ones' levels are only taken on with them, so the moving ones keep to the same levels.
        void refresh_people(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, float elapsed)
        {
            if (!scene.person_models)
                return;
            scene.since_people_levels += elapsed;
            if (ready(scene.people_build))
            {
                PeopleBuild built = scene.people_build.get();
                if (built.simulate == scene.simulate)
                {
                    scene.people_levels = std::move(built.levels);
                    scene.still_people = std::move(built.mesh);
                }
                else
                    scene.people_levels.clear(); // Built before the switch changed; build again
            }
            if (scene.since_people_levels >= kObjectRebuildInterval && !scene.people_build.valid())
            {
                scene.since_people_levels = 0;
                park::ObjectLevels levels(scene.shown_people.size(), park::kNotDrawn);
                for (std::size_t i = 0; i < levels.size(); ++i)
                {
                    // shortcut: guests who come in at the gate are simulated but not shown; add them to the shown
                    // people once new guests can be dressed and posed.
                    if (const ShownPerson& p = scene.shown_people[i]; scene.simulate && p.guest != entt::null && scene.sim.registry.all_of<simulation::Left>(p.guest))
                        continue; // Gone home
                    const float distance = glm::distance(eye, standing_at(scene, scene.shown_people[i]));
                    const auto level = std::ranges::find_if(kPersonDistances, [&](float d) { return distance <= d; });
                    if (level != kPersonDistances.end())
                        levels[i] = static_cast<std::int8_t>(level - kPersonDistances.begin());
                }
                if (levels != scene.people_levels)
                {
                    scene.people_build = std::async(std::launch::async, [&scene, levels = std::move(levels), view, simulate = scene.simulate] {
                        profiling::register_thread("PeopleWorker");
                        PeopleBuild built;
                        built.mesh = profiling::section("Still", [&] { return park::person_mesh(*scene.person_models, place_people(scene, levels, 0, false, view, simulate)); });
                        built.levels = levels;
                        built.simulate = simulate;
                        profiling::unregister_thread();
                        return built;
                    });
                }
            }
            // The people in motion are posed away from the frame, again as soon as the last posing is done.
            if (ready(scene.moving_build))
                scene.moving_people = scene.moving_build.get();
            if (!scene.moving_build.valid())
            {
                const profiling::Scope zone("Moving");
                scene.moving_build = std::async(std::launch::async, [&library = *scene.person_models, placed = place_people(scene, scene.people_levels, scene.clock, true, view, scene.simulate)] { return park::person_mesh(library, placed); });
            }
            if (ready(scene.riders_build))
            {
                RidersBuild built = scene.riders_build.get();
                scene.riders = std::move(built.riders);
                scene.rider_cars = std::move(built.cars);
            }
            scene.since_riders += elapsed;
            if (scene.simulate && scene.since_riders >= kRidersRebuildInterval && !scene.riders_build.valid())
            {
                scene.since_riders = 0;
                scene.riders_build = profiling::section("Riders", [&] { return pose_riders(scene); });
            }
        }

        /// The objects' meshes at the levels seen from `eye`, or nothing when those are `shown`. Reads only
        /// what is set when the park loads besides its arguments, so it may run beside the frame.
        std::optional<ObjectBuild> build_objects(const ParkScene& scene, const glm::vec3& eye, const park::LevelPlan& shown, float band)
        {
            ObjectBuild built;
            built.plan = profiling::section("Levels", [&] { return park::plan_levels(scene.objects, scene.models, scene.anchors, eye, band); });
            for (const ParkScene::BonedRide& boned : scene.boned_rides)
            {
                // Drawn apart, moving
                built.plan.steady[boned.object] = park::kNotDrawn;
                built.plan.finer[boned.object] = park::kNotDrawn;
                built.plan.coarser[boned.object] = park::kNotDrawn;
            }
            if (built.plan == shown)
                return std::nullopt;
            for (auto [shapes, levels] : { std::pair { &built.steady, &built.plan.steady }, std::pair { &built.finer, &built.plan.finer }, std::pair { &built.coarser, &built.plan.coarser } })
            {
                shapes->mesh = profiling::section("Mesh", [&] { return park::object_mesh(scene.objects, scene.models, scene.grid, *levels, { }, scene.leans, scene.fitted); });
                shapes->billboards = profiling::section("Gather", [&] { return park::gather_billboards(scene.objects, scene.models, scene.anchors, *levels); });
            }
            return built;
        }

        /// The band and side of each of `owners`' vertices: `side` 0 for objects drawn whole.
        std::vector<std::pair<glm::vec4, glm::vec2>> crossing_bands(const ParkScene& scene, const park::LevelPlan& plan, std::span<const std::uint32_t> owners, float side)
        {
            std::vector<std::pair<glm::vec4, glm::vec2>> out;
            out.reserve(owners.size());
            for (const std::uint32_t o : owners)
                out.push_back({ glm::vec4(scene.anchors[o], plan.bands[o].x), glm::vec2(plan.bands[o].y, side) });
            return out;
        }

        /// `vertices` with the bands of `bands`, for the crossing shaders.
        std::vector<CrossingVertex> crossing_vertices(std::span<const park::GroundVertex> vertices, std::span<const std::pair<glm::vec4, glm::vec2>> bands)
        {
            std::vector<CrossingVertex> out;
            out.reserve(vertices.size());
            for (std::size_t i = 0; i < vertices.size() && i < bands.size(); ++i)
                out.push_back({ vertices[i], bands[i].first, bands[i].second });
            return out;
        }

        /// Puts `shapes` in `shown`'s buffers, dropping what they held.
        void show_shapes(render::Device& device, ParkScene& scene, ShownObjects& shown, ObjectShapes&& shapes, const park::LevelPlan& plan, float side = 0)
        {
            for (const render::BufferHandle b : { shown.vertices, shown.indices })
            {
                if (b)
                    device.destroy_buffer(b);
            }
            shown = { .batches = shapes.mesh.batches, .billboards = std::move(shapes.billboards) };
            if (side != 0)
                shown.bands = crossing_bands(scene, plan, shown.billboards.mesh.owners, side);
            profiling::section("Textures", [&] {
                add_textures(device, scene, shapes.mesh.batches);
                add_textures(device, scene, shown.billboards.mesh.batches);
            });
            if (shapes.mesh.indices.empty())
                return;
            const profiling::Scope zone("Upload");
            auto vertices = side != 0 ? upload(device, render::BufferUsage::Vertex, crossing_vertices(shapes.mesh.vertices, crossing_bands(scene, plan, shapes.mesh.owners, side)), "crossing object vertices") : upload(device, render::BufferUsage::Vertex, shapes.mesh.vertices, "object vertices");
            auto indices = upload(device, render::BufferUsage::Index, shapes.mesh.indices, "object indices");
            if (!vertices || !indices)
            {
                logging::logger("park").warning("object buffers: {}", !vertices ? vertices.error() : indices.error());
                if (vertices)
                    device.destroy_buffer(*vertices);
                if (indices)
                    device.destroy_buffer(*indices);
                shown.batches.clear();
                return;
            }
            shown.vertices = *vertices;
            shown.indices = *indices;
        }

        /// Puts `built` in the object buffers.
        void show_objects(render::Device& device, ParkScene& scene, ObjectBuild&& built)
        {
            scene.levels = std::move(built.plan);
            show_shapes(device, scene, scene.steady_objects, std::move(built.steady), scene.levels);
            const float side = scene.tint_crossing ? 2.0f : 1.0f; // Twice as far tints them (lit.frag)
            show_shapes(device, scene, scene.finer_objects, std::move(built.finer), scene.levels, -side);
            show_shapes(device, scene, scene.coarser_objects, std::move(built.coarser), scene.levels, side);
            scene.billboards_eye.reset();
        }

        /// Takes on the objects' meshes once built, and looks at their levels again every
        /// kObjectRebuildInterval, building the meshes again beside the frame when one differs.
        void refresh_objects(render::Device& device, ParkScene& scene, const glm::vec3& eye, float elapsed)
        {
            if (ready(scene.object_build))
            {
                if (auto built = scene.object_build.get())
                    show_objects(device, scene, std::move(*built));
            }
            scene.since_rebuild += elapsed;
            if (scene.since_rebuild < kObjectRebuildInterval || scene.object_build.valid())
                return;
            scene.since_rebuild = 0;
            scene.object_build = std::async(std::launch::async, [&scene, eye, shown = scene.levels, band = scene.blend_band] {
                profiling::register_thread("ObjectWorker");
                auto built = profiling::section("Objects", [&] { return build_objects(scene, eye, shown, band); });
                profiling::unregister_thread();
                return built;
            });
        }

        /// Waits for the meshes being built beside the frame, dropping them.
        void finish_builds(ParkScene& scene)
        {
            if (scene.object_build.valid())
                scene.object_build.wait();
            if (scene.people_build.valid())
                scene.people_build.wait();
            if (scene.moving_build.valid())
                scene.moving_build.wait();
            if (scene.riders_build.valid())
                scene.riders_build.wait();
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

        /// Draws `mesh` from buffers made for this frame, each batch with `pipeline_of(batch)`; with its vertices
        /// as `vertex_bytes` when given, for pipelines of another layout.
        void draw_transient(render::Device& device, const ParkScene& scene, const park::ObjectMesh& mesh, const ui::UiTextures& textures, const glm::mat4& transform, const render::SceneLight& light, auto&& pipeline_of, auto&& address_of, std::span<const std::byte> vertex_bytes = { })
        {
            if (mesh.indices.empty())
                return;
            const auto vertices = device.allocate_transient(vertex_bytes.empty() ? std::as_bytes(std::span(mesh.vertices)) : vertex_bytes);
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
                draw.light = light;
                device.draw(draw);
            }
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

        void ask_leave(ParkScene& scene, const ui::StringTable& strings)
        {
            if (scene.desktop->root().find(kQuestionName) != nullptr)
                return;
            const ui::QuestionBoxStyle style = skins::question_box_style();
            scene.desktop->root().add<ui::QuestionBox>(std::string(kQuestionName), style, std::u16string(strings.get(kLeaveText)), std::vector<ui::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        }

        void move_camera(ParkScene& scene, const ParkInput& input, bool over_interface, bool edge_scroll, float elapsed)
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
            const bool free = scene.free_camera;
            if (scene.turning)
                c = park::turned(c, dx * kDragTurn, dy * kDragTurn, free);
            else if (scene.dragging)
                c = park::panned(c, -dx * kDragPan * c.distance, dy * kDragPan * c.distance);
            if (!over_interface)
                scene.zoom_pending += input.pointer.wheel.y;
            if (scene.zoom_pending != 0)
            {
                const float notches = park::zoom_step(scene.zoom_pending, elapsed, kWheelSeconds);
                scene.zoom_pending -= notches;
                const park::ParkCamera before = c;
                c = park::zoomed(c, std::pow(kWheelZoom, notches), free);
                if (c.distance == before.distance)
                    scene.zoom_pending = 0; // At a limit: what is left would roll in late once turned back
            }

            const float step = kKeyPan * c.distance * elapsed;
            float right = (input.right_key ? step : 0) - (input.left_key ? step : 0);
            float forward = (input.up_key ? step : 0) - (input.down_key ? step : 0);
            if (edge_scroll && input.in_window && !over_interface && !scene.turning && !scene.dragging)
            {
                const float scale = scene.scale.value_or(1);
                const ui::Rect screen = scene.desktop->root().rect();
                const glm::vec2 push = park::edge_push({ p.x, p.y }, glm::vec2 { screen.width(), screen.height() } * scale, kEdgeBand * scale);
                right += push.x * step;
                forward += push.y * step;
            }
            if (right != 0 || forward != 0)
                c = park::panned(c, right, forward);
            if (input.zoom_in_key != input.zoom_out_key)
                c = park::zoomed(c, std::pow(kKeyZoom, (input.zoom_in_key ? -1.0f : 1.0f) * elapsed), free);
            const float yaw = (input.turn_right_key ? 1.0f : 0.0f) - (input.turn_left_key ? 1.0f : 0.0f);
            const float pitch = (input.tilt_down_key ? 1.0f : 0.0f) - (input.tilt_up_key ? 1.0f : 0.0f);
            if (yaw != 0 || pitch != 0)
                c = park::turned(c, yaw * kKeyTurn * elapsed, pitch * kKeyTurn * elapsed, free);
            scene.camera = free ? c : park::on_ground(c, scene.grid);
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

        /// The sky and the light as the day clock shows them, with a look that is fading out over them.
        void show_time_of_day(ParkScene& scene, glm::vec3 viewer)
        {
            const simulation::DayClock& day = scene.day;
            const park::Daylight light = park::mixed(park::daylight(day.shown), park::daylight(day.faded), day.fade);
            scene.light = { .sun_direction = light.sun_direction, .sun = light.sun, .sky = light.sky, .ground = light.ground };
            const float column = park::sky_column(day.shown);
            const float faded = park::sky_column(day.faded);
            const float night = glm::mix(park::sky_darkness(day.shown), park::sky_darkness(day.faded), day.fade);
            scene.sky_mesh = park::sky_mesh(scene.sky, { .eye = viewer, .radius = kSkyRadius, .day = column, .faded_day = faded, .fade = day.fade, .night = night, .time = scene.clock });
            if (!scene.horizon_band.empty())
            {
                const glm::vec4 c = glm::mix(park::band_colour(scene.horizon_band, column), park::band_colour(scene.horizon_band, faded), day.fade);
                scene.horizon = { c.r, c.g, c.b, c.a };
            }
            // Far land fades into the horizon's colour.
            scene.light.haze = { scene.horizon.r, scene.horizon.g, scene.horizon.b };
            scene.light.haze_rate = scene.haze_rate;
            scene.light.surface_detail = scene.surface_detail;
        }

    } // namespace

    std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const config::GameOptions& options, const std::filesystem::path& path, std::uint64_t seed, const ParkLoadReport& report)
    {
        const profiling::Scope zone("LoadPark");
        const auto log = logging::logger("park");
        const auto begin = [&](ParkLoadStage stage) {
            if (report)
                report(stage);
        };
        begin(ParkLoadStage::ReadFile);
        const auto bytes = profiling::section("ReadFile", [&] { return read_file(path); });
        if (!bytes)
            return std::unexpected(bytes.error());
        const auto file = profiling::section("ParseSave", [&] { return save::read_save(*bytes, { .verify_checksum = false }); });
        if (!file)
            return std::unexpected(std::string(save::to_string(file.error())));
        const save::ObjectIndex index(*file);
        auto world = save::load_world(*file, index);
        if (!world)
            return std::unexpected(std::string(save::to_string(world.error())));
        if (world->terrain.empty())
            return std::unexpected("the park has no ground");

        ParkScene scene;
        scene.surface_detail = options.choice(config::GameOption::SurfaceDetail);
        scene.relief_maps = scene.surface_detail >= 2;
        scene.grid = std::move(world->terrain);
        scene.seed = seed;
        scene.file = path;
        scene.gameplay = random::stream(seed, random::RandomStream::Gameplay);
        log.info("{}: seed {} (--seed repeats it)", path.filename().string(), random::seed_text(seed));
        scene.camera = park::overview(scene.grid);
        scene.day = park_day(*file, index, *world, options);
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
        const std::vector<std::byte> noise = park::variation_texels();
        // Without it the ground is drawn plain.
        if (auto variation = device.create_texture({ .width = park::kVariationSize, .height = park::kVariationSize, .pixels = noise, .label = "ground variation" }))
            scene.variation = *variation;

        park::GroundImages images;
        for (const std::string_view name : kGroundFiles)
        {
            const auto found = install.find(name);
            if (!found)
                continue;
            if (const auto added = park::add_ground_images(images, *found); !added)
                log.warning("{}: {}", name, added.error());
        }
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
        scene.bins = std::move(world->bins);
        scene.signs = std::move(world->signs);
        if (auto rides = save::load_rides(*file, index))
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
            load_models(install, built, false, scene.models, scene.pictures);
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
            if (auto built = build_objects(scene, park::eye(scene.camera), { }, scene.blend_band))
                show_objects(device, scene, std::move(*built));
        });
        if (auto sky = load_sky(device, install, options, park::background_theme(world->sky_background), scene); !sky)
            return fail(sky.error());

        {
            const auto model = save::load_park(*file, index);
            scene.park = simulation::park_state(model ? &*model : nullptr, *world);
            scene.build = start_build_menus(install, model ? &model->content : nullptr);
            if (model)
                scene.park_model = *model;
        }
        // Behind the bar, which it hangs from.
        // Under the bar, even while it is dragged.
        scene.desktop->root().add<ui::MessageDrawer>(std::string(kMessagesName), skins::message_drawer_style(), strings).stays_behind = true;
        auto& tools = scene.desktop->root().add<ui::ToolRow>(std::string(kToolsName), skins::tool_row_style(), strings);
        // Only these menu items do anything yet; the rest show faded until their tools exist.
        for (std::size_t t = 0; t < ui::kParkToolCount; ++t)
        {
            for (const ui::ToolMenuItem& entry : ui::tool_menu(static_cast<ui::ParkTool>(t)))
                tools.set_available(entry.item, entry.item == ui::ToolItem::People || entry.item == ui::ToolItem::Scenery || entry.item == ui::ToolItem::Options || entry.item == ui::ToolItem::About || entry.item == ui::ToolItem::Save || entry.item == ui::ToolItem::Load || ui::build_list(entry.item) || ui::management_topic(entry.item));
        }
        scene.desktop->root().add<ui::ParkBar>(std::string(kBarName), skins::park_bar_style(), strings);
        scene.desktop->root().add<ui::InfoPanel>(std::string(kInfoName), skins::info_panel_style());
        scene.desktop->root().add<ui::FootpathPanel>(std::string(kFootpathPanelName), skins::footpath_panel_style(), strings);
        scene.attractions = park::attraction_objects(scene.rides, scene.objects);
        if (auto people = profiling::section("ReadPeople", [&] { return save::load_people(*file, index); }))
        {
            scene.people = std::move(*people);
            load_people_models(device, scene, install, seed);
            profiling::section("Simulation", [&] {
                scene.sim.seed = seed;
                simulation::import_paths(scene.sim, *world);
                const auto guests = simulation::import_guests(scene.sim, scene.people);
                simulation::import_stalls(scene.sim, scene.rides);
                const auto timings = simulation::flat_ride_timings(scene.rides, scene.objects, [&](const save::PlacedObject& o) -> const park::ObjectModel* {
                    const auto found = scene.models.find(park::model_key(o));
                    return found != scene.models.end() ? &found->second : nullptr;
                });
                // The courses of the rides whose trains go round, for the simulation to run them on.
                std::map<std::string, std::optional<park::PieceCatalogue>, std::less<>> catalogues;
                const park::PieceShapeOf shape_of = [&](const save::TrackPiece& piece, const save::PlacedObject& object) -> const park::PieceShape* {
                    auto [at, added] = catalogues.try_emplace(object.overlay);
                    if (added)
                    {
                        if (const auto file = install.find(object.overlay + ".common.ovl"))
                        {
                            if (auto loaded = park::load_piece_catalogue(*file, object.overlay))
                                at->second = std::move(*loaded);
                        }
                    }
                    return at->second ? park::shape_named(*at->second, piece.symbol) : nullptr;
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
                scene.ride_entities = simulation::import_rides(scene.sim, scene.rides, timings, coasters);
                for (std::size_t i = 0; i < scene.car_spots.size(); ++i)
                    if (const std::size_t ride = scene.car_spots[i].ride; ride < scene.ride_entities.size() && scene.ride_entities[ride] != entt::null)
                        scene.ride_cars[scene.ride_entities[ride]].push_back(i);
                // Flat rides seat their riders on the object that has the seats, as flat_ride_timings finds it.
                for (std::size_t i = 0; i < scene.rides.rides.size() && i < scene.ride_entities.size(); ++i)
                {
                    const save::Ride& ride = scene.rides.rides[i];
                    if (ride.kind != save::RideKind::Flat || ride.base.placement == 0 || scene.ride_entities[i] == entt::null)
                        continue;
                    for (std::size_t k = 0; k < scene.objects.size(); ++k)
                    {
                        const auto model = scene.objects[k].owner_ref == ride.base.placement ? scene.models.find(park::model_key(scene.objects[k])) : scene.models.end();
                        if (model != scene.models.end() && !model->second.seat_places.empty())
                        {
                            scene.ride_seats[scene.ride_entities[i]] = k;
                            if (model->second.rig)
                            {
                                ParkScene::BonedRide& boned = scene.boned_rides.emplace_back();
                                boned.ride = scene.ride_entities[i];
                                boned.object = k;
                                boned.placed = park::placed_transform(scene.objects[k], &model->second, scene.grid, scene.fitted);
                                std::tie(boned.mesh, boned.bones) = park::boned_meshes(scene.objects[k], model->second, scene.grid);
                                boned.first_seat = scene.seat_carriers.size();
                                scene.seat_carriers.resize(scene.seat_carriers.size() + model->second.seat_bones.size(), boned.placed);
                                add_textures(device, scene, boned.mesh.mesh.batches);
                            }
                            break;
                        }
                    }
                }
                simulation::start_guests(scene.sim);
                simulation::board_saved_riders(scene.sim, scene.people, guests, scene.rides, scene.ride_entities);
                simulation::import_parties(scene.sim, scene.people, guests);
                simulation::import_litter(scene.sim, scene.people, scene.park.time);
                simulation::import_path_items(scene.sim, scene.objects, scene.bins, world->benches, scene.signs);
                simulation::import_cover(scene.sim, [&](std::span<const glm::vec3> points) { return park::overheads(points, scene.objects, scene.models, scene.grid, scene.fitted); });
                simulation::import_facilities(scene.sim, scene.rides, [&](const save::Ride& ride) -> std::optional<std::uint32_t> {
                    const auto file = install.find(ride.base.overlay_path + ".common.ovl");
                    return file ? park::special_attraction_kind(*file, std::string_view(ride.base.symbol).substr(0, ride.base.symbol.find(':'))) : std::nullopt;
                });
                simulation::import_sights(scene.sim, scene.objects, [&](const save::PlacedObject& o) {
                    const auto model = scene.models.find(park::model_key(o));
                    return model != scene.models.end() && park::is_sight(model->second.category);
                });
                simulation::import_gate(scene.sim, scene.objects, [&](const save::PlacedObject& o) {
                    const auto model = scene.models.find(park::model_key(o));
                    return model != scene.models.end() && model->second.category == std::to_underlying(park::ItemCategory::ParkEntrance);
                });
                const auto staff = simulation::import_staff(scene.sim, scene.people);
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

    namespace
    {

        /// The first of the attraction's objects that takes colours, and the slots it takes.
        std::optional<std::pair<std::size_t, std::vector<std::size_t>>> painted_object(const ParkScene& scene, park::AttractionRef ref)
        {
            for (std::size_t i = 0; i < scene.objects.size(); ++i)
            {
                const auto owner = scene.attractions.find(scene.objects[i].id);
                if (owner == scene.attractions.end() || owner->second != ref)
                    continue;
                const auto model = scene.models.find(park::model_key(scene.objects[i]));
                ui::SceneryFacts f;
                f.colours = scene.objects[i].colours;
                f.colour_slots = model == scene.models.end() ? 0 : park::colour_slots(model->second);
                if (std::vector<std::size_t> slots = ui::scenery_paint_slots(f); !slots.empty())
                    return std::pair { i, std::move(slots) };
            }
            return std::nullopt;
        }

        ui::AttractionFacts facts(const ParkScene& scene, park::AttractionRef ref)
        {
            ui::AttractionFacts f { .guests = scene.people.guests };
            if (ref.kind == park::AttractionRef::Kind::Stall)
            {
                if (ref.index < scene.rides.stalls.size())
                {
                    f.stall = &scene.rides.stalls[ref.index];
                    f.common = &f.stall->base;
                }
            }
            else if (ref.index < scene.rides.rides.size())
            {
                f.ride = &scene.rides.rides[ref.index];
                f.common = &f.ride->base;
            }
            // A running ride's seats, queue and riders an hour.
            if (f.ride != nullptr && scene.simulate && ref.index < scene.ride_entities.size() && scene.ride_entities[ref.index] != entt::null)
            {
                if (const auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(scene.ride_entities[ref.index]))
                    f.flow = ui::RideFlow { cycle->capacity, cycle->queue.size(), cycle->per_hour };
            }
            if (const auto painted = painted_object(scene, ref))
            {
                for (const std::size_t slot : painted->second)
                    f.colours.push_back(static_cast<std::size_t>(std::max(scene.objects[painted->first].colours.at(slot), 0)));
            }
            return f;
        }

        save::AttractionCommon* attraction_common(ParkScene& scene, park::AttractionRef ref)
        {
            if (ref.kind == park::AttractionRef::Kind::Stall)
                return ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index].base : nullptr;
            return ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index].base : nullptr;
        }

        ui::PersonFacts person_facts(const ParkScene& scene, std::size_t shown)
        {
            ui::PersonFacts f;
            f.people = &scene.people;
            f.rides = &scene.rides;
            if (shown >= scene.shown_people.size())
                return f;
            const ShownPerson& p = scene.shown_people[shown];
            if (p.staff && p.saved < scene.people.staff.size())
                f.staff = &scene.people.staff[p.saved];
            else if (!p.staff && p.saved < scene.people.guests.size())
                f.guest = &scene.people.guests[p.saved];
            if (const entt::entity e = shown_staff(scene); e != entt::null)
            {
                const auto* patrol = scene.sim.registry.try_get<simulation::Patrol>(e);
                f.patrol_tiles = patrol != nullptr ? patrol->tiles.size() : 0;
                f.painting_patrol = scene.painting_patrol;
                f.patrol_brush = scene.patrol_brush;
                f.patrol_zone = patrol != nullptr ? patrol->zone : 0;
                f.uncovered_paths = simulation::uncovered_paths(scene.sim, scene.sim.registry.get<simulation::Staff>(e).role);
            }
            return f;
        }

        ui::InfoUnits info_units(const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            ui::InfoUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
            units.distance = static_cast<ui::DistanceUnits>(options.choice(config::GameOption::DistanceUnits));
            return units;
        }

        ui::SceneryFacts prop_facts(const ParkScene& scene, std::size_t index)
        {
            ui::SceneryFacts f;
            if (index >= scene.objects.size())
                return f;
            const save::PlacedObject& o = scene.objects[index];
            f.symbol = o.symbol;
            f.colours = o.colours;
            if (const auto model = scene.models.find(park::model_key(o)); model != scene.models.end())
                f.colour_slots = park::colour_slots(model->second);
            if (const auto bin = std::ranges::find(scene.bins, o.id, &save::BinState::object_ref); bin != scene.bins.end())
                f.bin = &*bin;
            if (const auto sign = std::ranges::find(scene.signs, o.id, &save::SignState::object_ref); sign != scene.signs.end())
                f.sign = &*sign;
            return f;
        }

        void show_info_page(ParkScene& scene, ui::InfoPanel& panel, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (scene.shown_prop)
                panel.show_page(ui::scenery_page(scene.prop_tab, prop_facts(scene, *scene.shown_prop), *assets.strings));
            else if (scene.shown_person)
                panel.show_page(ui::person_page(scene.person_tab, person_facts(scene, *scene.shown_person), *assets.strings, info_units(assets, options)));
            else if (scene.shown)
                panel.show_page(ui::attraction_page(scene.info_tab, facts(scene, *scene.shown), *assets.strings, info_units(assets, options), scene.info_section));
        }

        /// The round view of a person: looking at them from where the player looks from, turning from there.
        park::ParkCamera person_view(const ParkScene& scene, std::size_t shown, float yaw)
        {
            park::ParkCamera camera;
            camera.target = glm::vec3(scene.shown_people[shown].pose.world[3]) + glm::vec3(0, kPersonViewHeight, 0);
            camera.yaw = yaw;
            camera.pitch = kViewPitch;
            camera.distance = kPersonViewDistance;
            return camera;
        }

        /// Opens the panel on the person `shown` (of the shown people), at the tab last opened on a person if
        /// they have it.
        void open_person(ParkScene& scene, ui::InfoPanel& panel, std::size_t shown, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (shown >= scene.shown_people.size())
                return;
            std::vector<ui::InfoTab> tabs;
            scene.person_tabs.clear();
            std::size_t picked = 0;
            const ui::PersonFacts f = person_facts(scene, shown);
            for (const ui::PersonTabInfo& t : ui::person_tabs(f))
            {
                if (t.tab == scene.person_tab)
                    picked = tabs.size();
                ui::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                // A uniform's pots show the colours the person is drawn in.
                for (std::size_t pot = 0; pot < t.pots; ++pot)
                    tab.paints.push_back(scene.shown_people[shown].pose.look.colours.at(ui::kUpperPalette + pot));
                tab.colours = ui::kUniformColours;
                scene.person_tabs.push_back(t.tab);
            }
            // A uniform tab has no page of its own.
            if (scene.person_tabs.at(picked) == ui::PersonTab::Uniform)
                picked = 0;
            scene.person_tab = scene.person_tabs.at(picked);
            if (scene.shown_person != shown)
            {
                scene.view_camera = person_view(scene, shown, scene.camera.yaw);
                scene.painting_patrol = false;
            }
            scene.shown.reset();
            scene.shown_prop.reset();
            scene.shown_person = shown;
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the attraction `ref`, at the tab last opened if it has one.
        void open_info(ParkScene& scene, ui::InfoPanel& panel, park::AttractionRef ref, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            const ui::AttractionFacts f = facts(scene, ref);
            if (f.common == nullptr)
                return;
            const ui::AttractionKind kind = f.stall ? ui::AttractionKind::Stall : ui::attraction_kind(*f.ride);
            std::vector<ui::InfoTab> tabs;
            scene.shown_tabs.clear();
            std::size_t picked = 0;
            for (const ui::AttractionTabInfo& t : ui::attraction_tabs(kind, f.common->state))
            {
                if (t.tab == scene.info_tab)
                    picked = tabs.size();
                ui::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                tab.action = t.action;
                if (t.tab == ui::AttractionTab::State)
                {
                    for (const ui::StateChoice& c : ui::state_choices(kind))
                    {
                        const bool now = c.state == f.common->state;
                        tab.choices.push_back({ std::string(now ? c.disabled_icon : c.icon), std::u16string(assets.strings->get(c.help)), !now });
                    }
                }
                scene.shown_tabs.push_back(t.tab);
            }
            // The open-or-close tab has no page of its own.
            if (scene.shown_tabs.at(picked) == ui::AttractionTab::State)
                picked = 0;
            scene.info_tab = scene.shown_tabs.at(picked);
            if (scene.shown != ref || scene.shown_person)
            {
                // The view starts from where the player looks from, and turns from there.
                std::vector<glm::vec3> points;
                for (std::size_t i = 0; i < scene.objects.size() && i < scene.anchors.size(); ++i)
                {
                    const auto owner = scene.attractions.find(scene.objects[i].id);
                    if (owner != scene.attractions.end() && owner->second == ref)
                        points.push_back(scene.anchors[i]);
                }
                scene.view_camera = park::framed(points, scene.camera.yaw, kViewPitch);
            }
            scene.shown_person.reset();
            scene.shown_prop.reset();
            scene.shown = ref;
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the prop `index` (of the objects), with only its name showing.
        void open_prop(ParkScene& scene, ui::InfoPanel& panel, std::size_t index, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (index >= scene.objects.size())
                return;
            std::vector<ui::InfoTab> tabs;
            scene.prop_tabs.clear();
            for (const ui::SceneryTabInfo& t : ui::scenery_tabs(prop_facts(scene, index)))
            {
                ui::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                tab.action = t.action;
                tab.paints = t.paints;
                scene.prop_tabs.push_back(t.tab);
            }
            if (scene.shown_prop != index)
                scene.view_camera = park::framed(park::object_corners(scene.objects[index], scene.models, scene.grid, scene.fitted), scene.camera.yaw, kViewPitch, kPropViewClosest);
            scene.shown.reset();
            scene.shown_person.reset();
            scene.shown_prop = index;
            scene.prop_tab.reset();
            const std::size_t none = tabs.size();
            panel.show(std::move(tabs), none);
            show_info_page(scene, panel, assets, options);
        }

        /// Changes the object `index` by `change`. The objects' meshes are built beside the frame from the
        /// objects, so the build in hand is dropped first and another started with the change.
        template<typename Change> void change_object(ParkScene& scene, std::size_t index, Change&& change)
        {
            if (index >= scene.objects.size())
                return;
            if (scene.object_build.valid())
                (void)scene.object_build.get();
            change(scene.objects[index]);
            scene.levels = { };
            scene.since_rebuild = kObjectRebuildInterval;
        }

        /// Takes the prop `index` out of the park.
        void destroy_prop(ParkScene& scene, std::size_t index)
        {
            change_object(scene, index, [](save::PlacedObject& o) { o.hidden = true; });
        }

        /// Paints colour slot `slot` of the prop `index` in colour `colour` of the 32.
        void paint_prop(ParkScene& scene, std::size_t index, std::size_t slot, std::int32_t colour)
        {
            if (slot < park::kFlexiSlotCount)
                change_object(scene, index, [&](save::PlacedObject& o) { o.colours.at(slot) = colour; });
        }

        /// Writes a change the player made on an attraction's page into the save. Nothing is shown again, so a
        /// spinner held down keeps stepping; a page that looks different after the change is set up here.
        void change_attraction(ParkScene& scene, ui::InfoPanel& panel, park::AttractionRef ref, const ui::InfoChange& change)
        {
            using enum ui::AttractionControl;
            save::Ride* ride = ref.kind == park::AttractionRef::Kind::Ride && ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index] : nullptr;
            save::Stall* stall = ref.kind == park::AttractionRef::Kind::Stall && ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index] : nullptr;
            const auto wait_on = [&](std::int32_t& wait, ui::AttractionControl seconds) {
                ui::Widget* const field = panel.control(ui::control_id(seconds));
                wait = change.value != 0 ? (field != nullptr ? static_cast<const ui::Spinner*>(field)->value() : 0) : -1;
                if (field != nullptr)
                    field->enabled = change.value != 0;
            };
            if (change.id == ui::control_id(Colours))
            {
                // Every object of the attraction takes the colour in the slot of the pot.
                const auto painted = painted_object(scene, ref);
                if (!painted || change.pot >= painted->second.size())
                    return;
                const std::size_t slot = painted->second[change.pot];
                for (std::size_t i = 0; i < scene.objects.size(); ++i)
                {
                    const auto owner = scene.attractions.find(scene.objects[i].id);
                    if (owner != scene.attractions.end() && owner->second == ref)
                        paint_prop(scene, i, slot, change.value);
                }
            }
            else if (stall != nullptr && change.id >= ui::control_id(ItemPrice))
            {
                if (const auto item = static_cast<std::size_t>(change.id - ui::control_id(ItemPrice)); item < stall->items.size())
                    stall->items[item].price = change.value;
            }
            else if (ride == nullptr)
                return;
            else if (change.id == ui::control_id(Price))
                ride->ticket_price = change.value;
            else if (change.id == ui::control_id(Inspection))
            {
                ride->breakdown.inspection_interval = change.value;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* upkeep = scene.sim.registry.try_get<simulation::Upkeep>(e))
                        upkeep->inspect_every = simulation::upkeep_of(*ride).inspect_every;
            }
            else if (change.id == ui::control_id(MinWaitOn))
                wait_on(ride->options.min_wait, MinWait);
            else if (change.id == ui::control_id(MaxWaitOn))
                wait_on(ride->options.max_wait, MaxWait);
            else if (change.id == ui::control_id(MinWait))
                ride->options.min_wait = change.value;
            else if (change.id == ui::control_id(MaxWait))
                ride->options.max_wait = change.value;
            else if (change.id == ui::control_id(WaitFor))
                ride->options.wait_for_load = change.value;
            // The running ride leaves as its settings now say.
            if (ride != nullptr && ref.index < scene.ride_entities.size() && scene.ride_entities[ref.index] != entt::null)
                if (auto* departure = scene.sim.registry.try_get<simulation::Departure>(scene.ride_entities[ref.index]))
                    *departure = simulation::departure_of(ride->options);
        }

        /// Lets the people's meshes be changed: the build beside the frame reads them, so it is dropped first,
        /// and all are built again after.
        void change_people(ParkScene& scene)
        {
            if (scene.people_build.valid())
                (void)scene.people_build.get();
            if (scene.moving_build.valid())
                (void)scene.moving_build.get();
            if (scene.riders_build.valid())
                (void)scene.riders_build.get();
            scene.people_levels.clear();
            scene.since_people_levels = kObjectRebuildInterval;
        }

        /// Fires the member of staff shown as `shown`: they leave the park at once, as in the game.
        void fire(ParkScene& scene, std::size_t shown)
        {
            change_people(scene);
            const std::size_t saved = scene.shown_people.at(shown).saved;
            if (const entt::entity walker = scene.shown_people[shown].guest; walker != entt::null)
                scene.sim.registry.destroy(walker);
            scene.shown_people.erase(scene.shown_people.begin() + static_cast<std::ptrdiff_t>(shown));
            scene.people.staff.erase(scene.people.staff.begin() + static_cast<std::ptrdiff_t>(saved));
            for (ShownPerson& p : scene.shown_people)
            {
                if (p.staff && p.saved > saved)
                    --p.saved;
            }
        }

        /// Writes a change the player made on a person's page into the save. Returns whether the page is to be
        /// shown again for its new look.
        bool change_person(ParkScene& scene, std::size_t shown, const ui::InfoChange& change)
        {
            using enum ui::PersonControl;
            if (shown >= scene.shown_people.size())
                return false;
            const ShownPerson& p = scene.shown_people[shown];
            save::Staff* staff = p.staff && p.saved < scene.people.staff.size() ? &scene.people.staff[p.saved] : nullptr;
            save::Guest* guest = !p.staff && p.saved < scene.people.guests.size() ? &scene.people.guests[p.saved] : nullptr;
            save::Person* person = staff != nullptr ? &staff->person : guest != nullptr ? &guest->person : nullptr;
            if (person == nullptr)
                return false;
            if (change.id == ui::person_control(Name))
            {
                // A blank name keeps the old one.
                if (change.text.empty())
                    return true;
                person->name = strings::to_utf8(change.text);
                return true;
            }
            if (staff == nullptr)
                return false;
            if (change.id == ui::person_control(PatrolPaint))
            {
                scene.painting_patrol = change.value != 0;
                return false;
            }
            if (const entt::entity e = shown_staff(scene); e != entt::null && change.id == ui::person_control(PatrolClear))
            {
                simulation::clear_patrol(scene.sim.registry, e);
                return true;
            }
            else if (e != entt::null && change.id == ui::person_control(PatrolFill))
            {
                simulation::fill_patrol(scene.sim.registry, e);
                return true;
            }
            else if (e != entt::null && change.id == ui::person_control(PatrolZone))
            {
                simulation::join_zone(scene.sim.registry, e, static_cast<std::uint32_t>(std::max(change.value, 0)));
                return true;
            }
            if (change.id == ui::person_control(PatrolBrush))
            {
                scene.patrol_brush = change.value;
                return false;
            }
            if (change.id == ui::person_control(PatrolCopy))
            {
                copy_patrol(scene);
                return true;
            }
            if (change.id == ui::person_control(Warn))
            {
                staff->warnings = std::min(staff->warnings + 1, ui::kMostWarnings);
                return true;
            }
            if (change.id == ui::person_control(Wage))
                staff->wage_level = change.value;
            else if (change.id == ui::person_control(Costume))
                person->sub_type = static_cast<std::uint8_t>(change.value);
            else if (change.id >= ui::person_control(Task))
                staff->task_flags ^= 1 << (change.id - ui::person_control(Task));
            return false;
        }

        /// Paints the clothes of pot `pot` of the person shown as `shown` in colour `colour`.
        void paint_person(ParkScene& scene, std::size_t shown, std::size_t pot, std::size_t colour)
        {
            if (shown >= scene.shown_people.size() || ui::kUpperPalette + pot >= park::PersonLook { }.colours.size())
                return;
            change_people(scene);
            ShownPerson& p = scene.shown_people[shown];
            const auto c = static_cast<std::uint8_t>(colour);
            p.pose.look.colours.at(ui::kUpperPalette + pot) = c;
            save::Person* person = p.staff ? &scene.people.staff.at(p.saved).person : &scene.people.guests.at(p.saved).person;
            person->appearance.palettes.at(ui::kUpperPalette + pot) = c;
        }

        /// Whether the object is a prop the panel can tell about: one of the park's own.
        bool clickable_prop(const ParkScene& scene, const save::PlacedObject& o)
        {
            if (scene.attractions.contains(o.id))
                return false;
            const auto model = scene.models.find(park::model_key(o));
            return model != scene.models.end() && park::is_prop(model->second.category) && park::on_owned_land(o, model->second.squares, scene.grid);
        }

        ui::ManagementFacts management_facts(const ParkScene& scene)
        {
            return { .park = &scene.park_model, .state = &scene.park, .people = &scene.people, .rides = &scene.rides };
        }

        /// Opens the info panel on what a management window's row is about, if the park shows it.
        void open_row(ParkScene& scene, ui::InfoPanel& panel, ui::ManagementRow row, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            using Kind = ui::ManagementRow::Kind;
            if (row.kind == Kind::Ride || row.kind == Kind::Stall)
            {
                open_info(scene, panel, { row.kind == Kind::Stall ? park::AttractionRef::Kind::Stall : park::AttractionRef::Kind::Ride, row.index }, assets, options);
                return;
            }
            const bool staff = row.kind == Kind::Staff;
            if (!staff && row.kind != Kind::Guest)
                return;
            const auto shown = std::ranges::find_if(scene.shown_people, [&](const ShownPerson& p) { return p.staff == staff && p.saved == row.index; });
            if (shown != scene.shown_people.end())
                open_person(scene, panel, static_cast<std::size_t>(shown - scene.shown_people.begin()), assets, options);
        }

        /// What the pointer is on: an attraction, a person of the shown people or a prop of the objects.
        struct Picked
        {
            std::optional<park::AttractionRef> attraction;
            std::optional<std::size_t> person;
            std::optional<std::size_t> prop;
        };

        /// What is under the pointer at `at`, in interface units on a screen of `size`: the nearest of an
        /// attraction, a prop and a person.
        Picked picked_at(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            if (size.x <= 0 || size.y <= 0)
                return { };
            const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
            const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, ndc);
            // What is hidden cannot be clicked.
            if (scene.pick_boxes_of != scene.objects.size())
            {
                scene.pick_boxes = park::pick_boxes(scene.objects, scene.models, scene.grid, scene.fitted);
                scene.pick_boxes_of = scene.objects.size();
            }
            const auto object = park::pick_object(ray, scene.pick_boxes, scene.grid, [&](std::size_t i) {
                const save::PlacedObject& o = scene.objects[i];
                return scene.show_scenery && !o.hidden && (scene.attractions.contains(o.id) || clickable_prop(scene, o));
            });
            std::vector<glm::mat4> placements;
            if (scene.show_people)
            {
                placements.reserve(scene.shown_people.size());
                for (const ShownPerson& p : scene.shown_people)
                    placements.push_back(scene.simulate && p.guest != entt::null ? glm::translate(glm::mat4(1.0f), standing_at(scene, p)) : p.pose.world);
            }
            const auto person = park::pick_person(ray, placements, scene.grid);
            if (person && (!object || person->distance < object->distance))
                return { .attraction = std::nullopt, .person = person->index, .prop = std::nullopt };
            if (!object)
                return { };
            const auto owner = scene.attractions.find(scene.objects[object->index].id);
            if (owner != scene.attractions.end())
                return { .attraction = owner->second, .person = std::nullopt, .prop = std::nullopt };
            return { .attraction = std::nullopt, .person = std::nullopt, .prop = object->index };
        }

        /// The people as the map draws them.
        park::MapPeople map_people(const ParkScene& scene)
        {
            park::MapPeople shown;
            shown.people.reserve(scene.shown_people.size());
            for (const ShownPerson& p : scene.shown_people)
                shown.people.push_back({ { p.pose.world[3].x, p.pose.world[3].z }, p.staff });
            for (const auto [e, patrol] : scene.sim.registry.view<const simulation::Patrol>().each())
                shown.patrol_tiles.insert(shown.patrol_tiles.end(), patrol.tiles.begin(), patrol.tiles.end());
            return shown;
        }

    } // namespace

    bool ripple_at(ParkScene& scene, ui::Point at, ui::Point size)
    {
        if (size.x <= 0 || size.y <= 0)
            return false;
        const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
        return ripple_park_water(scene.water, park::view_ray(scene.camera, size.x / size.y, ndc), scene.grid);
    }

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed)
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
            auto* tools = static_cast<ui::ToolRow*>(desktop.root().find(kToolsName));
            if (!escape.empty() && tools != nullptr && tools->open_menu())
                tools->close_menu();
            else if (!escape.empty() && !close_top_management_window(desktop))
                ask_leave(scene, *assets.strings);
            keys.presses.erase(escape.begin(), escape.end());
        }

        ui::PointerState pointer = input.pointer;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        scene.current_fonts = &ui::fonts_for(device, assets, desktop.root().rect().width());
        const bool over = profiling::section("Interface", [&] { return desktop.update(pointer, keys, { elapsed, scene.current_fonts }); });
        profiling::section("Camera", [&] { move_camera(scene, input, over, options.flag(config::GameOption::AutoScrollAtEdge), elapsed); });
        scene.zooming = input.zoom_in_key == input.zoom_out_key ? 0 : (input.zoom_in_key ? 1 : -1);
        auto* info = static_cast<ui::InfoPanel*>(desktop.root().find(kInfoName));
        // What a click would open, for the pointer's look; not looked at every frame, as picking walks the park.
        scene.since_hover_pick += elapsed;
        if (over || !pointer.present || info == nullptr || scene.turning || scene.dragging)
            scene.hover_pickable = false;
        else if (scene.since_hover_pick >= kHoverPickInterval)
        {
            const profiling::Scope zone("HoverPick");
            const ui::Rect screen = desktop.root().rect();
            const Picked picked = picked_at(scene, pointer.position, { screen.width(), screen.height() });
            scene.hover_pickable = picked.person || picked.attraction || picked.prop;
            scene.since_hover_pick = 0;
        }
        if (info != nullptr)
        {
            const profiling::Scope zone("InfoPanel");
            // Pressing the park puts open colours away without picking.
            if ((pointer.down || input.right) && !over && info->painting())
                info->close_palette();
            // A stroke paints the patrol area while the left button is held over the park; its page shows the area
            // again when the stroke ends.
            const ui::Rect view = desktop.root().rect();
            if (paint_patrol(scene, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle && (scene.patrol_stroke || !over)))
                show_info_page(scene, *info, assets, options);
            // A stroke over the ground while the path panel shows its controls lays paths on every tile it spans.
            const bool going = !scene.footpaths.brush.empty() || scene.footpaths.slope_press.has_value();
            footpath_drag(scene, pointer.position, { view.width(), view.height() }, pointer.down && !input.middle && (going || !over), over, input.right);
            // A click is the left button pressed and let go over the park without the pointer going far.
            if (pointer.down && !scene.press && !over && !input.right && !input.middle)
                scene.press = pointer.position;
            else if (!pointer.down && scene.press)
            {
                const ui::Point from = *std::exchange(scene.press, std::nullopt);
                const bool still = std::abs(pointer.position.x - from.x) <= kClickSlack && std::abs(pointer.position.y - from.y) <= kClickSlack;
                const ui::Rect screen = desktop.root().rect();
                // While the path panel shows its controls, a click on the ground picks where the path starts.
                // While painting a patrol area, the left button paints and does not pick.
                if (painting_patrol(scene))
                    ;
                else if (still && !over && !footpath_click(scene, pointer.position, { screen.width(), screen.height() }))
                {
                    const Picked picked = picked_at(scene, pointer.position, { screen.width(), screen.height() });
                    if (picked.person)
                        open_person(scene, *info, *picked.person, assets, options);
                    else if (picked.attraction)
                        open_info(scene, *info, *picked.attraction, assets, options);
                    else if (picked.prop)
                        open_prop(scene, *info, *picked.prop, assets, options);
                    else
                        ripple_at(scene, pointer.position, { screen.width(), screen.height() });
                }
            }
            if (input.right || input.middle)
                scene.press.reset();
            // The customers page follows the running park; it has no controls to lose a half-made change in.
            scene.since_info_page += elapsed;
            if (scene.shown && scene.simulate && scene.info_tab == ui::AttractionTab::Customers && scene.since_info_page >= kLivePageInterval)
            {
                scene.since_info_page = 0;
                show_info_page(scene, *info, assets, options);
            }
            const auto action = info->take_action();
            if (action && scene.shown_prop && *action < scene.prop_tabs.size() && scene.prop_tabs[*action] == ui::SceneryTab::Destroy)
            {
                // Pulled down at once, as in the game; the panel goes with it.
                destroy_prop(scene, *scene.shown_prop);
                scene.shown_prop.reset();
                info->hide();
            }
            if (action && scene.shown && *action < scene.shown_tabs.size() && scene.shown_tabs[*action] == ui::AttractionTab::Construction)
                scene.construct_requested = *scene.shown;
            if (const auto change = info->take_change(); change && scene.shown)
                change_attraction(scene, *info, *scene.shown, *change);
            else if (change && scene.shown_person)
            {
                if (change->id == ui::person_control(ui::PersonControl::Sack))
                {
                    fire(scene, *scene.shown_person);
                    scene.shown_person.reset();
                    info->hide();
                }
                else if (change_person(scene, *scene.shown_person, *change))
                    show_info_page(scene, *info, assets, options);
            }
            const auto paint = info->take_paint();
            if (paint && scene.shown_person)
                paint_person(scene, *scene.shown_person, paint->pot, paint->colour);
            if (paint && scene.shown_prop)
            {
                // Pot k paints the k-th slot the prop takes; free, as in the game.
                const std::vector<std::size_t> slots = ui::scenery_paint_slots(prop_facts(scene, *scene.shown_prop));
                if (paint->pot < slots.size())
                    paint_prop(scene, *scene.shown_prop, slots[paint->pot], static_cast<std::int32_t>(paint->colour));
            }
            if (const auto tab = info->take_picked(); tab && scene.shown_prop)
            {
                if (*tab < scene.prop_tabs.size())
                {
                    scene.prop_tab = scene.prop_tabs[*tab];
                    show_info_page(scene, *info, assets, options);
                }
            }
            else if (tab && scene.shown_person)
            {
                if (*tab < scene.person_tabs.size())
                {
                    scene.person_tab = scene.person_tabs[*tab];
                    show_info_page(scene, *info, assets, options);
                }
            }
            else if (tab && *tab < scene.shown_tabs.size())
            {
                scene.info_tab = scene.shown_tabs[*tab];
                scene.info_section = 0;
                show_info_page(scene, *info, assets, options);
            }
            if (const auto section = info->take_section())
            {
                scene.info_section = *section;
                show_info_page(scene, *info, assets, options);
            }
            if (const auto choice = info->take_choice(); choice && scene.shown)
            {
                // Put in the state picked, then shown again for the state tab's new look.
                const ui::AttractionFacts f = facts(scene, *scene.shown);
                const ui::AttractionKind kind = f.stall ? ui::AttractionKind::Stall : ui::attraction_kind(*f.ride);
                const std::vector<ui::StateChoice> states = ui::state_choices(kind);
                if (save::AttractionCommon* common = attraction_common(scene, *scene.shown); common != nullptr && choice->second < states.size())
                {
                    common->state = states[choice->second].state;
                    open_info(scene, *info, *scene.shown, assets, options);
                }
            }
            if (info->take_closed())
            {
                scene.shown.reset();
                scene.shown_person.reset();
                scene.shown_prop.reset();
            }
        }
        // The Construction tab has no page: it opens the panel that builds the ride's track.
        if (const auto asked = std::exchange(scene.construct_requested, std::nullopt); asked && asked->kind == park::AttractionRef::Kind::Ride)
        {
            if (const auto overlay = ride_track_overlay(scene, asked->index))
            {
                if (open_track_building(scene, asked->index, *overlay, assets))
                    close_footpath_panel(scene); // One tool panel at the bottom left at a time
            }
        }
        profiling::section("TrackBuilding", [&] { update_track_building(device, scene, assets, options.choice(config::GameOption::Currency)); });
        scene.view_camera.yaw += kViewTurn * elapsed;
        if (scene.shown_person && *scene.shown_person < scene.shown_people.size())
            scene.view_camera = person_view(scene, *scene.shown_person, scene.view_camera.yaw);
        const glm::vec3 viewer = park::eye(scene.camera);
        profiling::section("Objects", [&] { refresh_objects(device, scene, viewer, elapsed); });
        if (scene.billboards_eye != viewer)
        {
            const profiling::Scope zone("Billboards");
            for (ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                park::face_billboards(shown->billboards, viewer);
            scene.billboards_eye = viewer;
        }
        scene.clock += elapsed;
        scene.water.look = water_look(options);
        scene.shadows = options.choice(config::GameOption::Shadows);
        scene.blend_shadows = options.flag(config::GameOption::BlendShadows);
        scene.blend_band = options.flag(config::GameOption::BlendDetailLevels) ? options.get(config::GameOption::DetailBlendBand) : 0.0f;
        scene.bloom = options.flag(config::GameOption::Bloom);
        scene.surface_detail = options.choice(config::GameOption::SurfaceDetail);
        advance_park_water(scene.water, elapsed);
        profiling::section("People", [&] { refresh_people(scene, viewer, park::view_projection(scene.camera, aspect(device)), elapsed); });
        auto* bar = static_cast<ui::ParkBar*>(desktop.root().find(kBarName));
        if (bar != nullptr)
        {
            if (const auto asked = bar->take_speed())
                scene.park.speed = *asked;
        }
        for (const ui::PlayCommand c : input.commands)
        {
            if (c == ui::PlayCommand::TogglePause)
                scene.park.speed = simulation::pause_toggled(scene.park.speed);
        }
        profiling::section("Simulation", [&] {
            // The park's day, its calendar and its messages' ages run at the speed picked, and stop while paused.
            const float game_seconds = elapsed * simulation::time_factor(scene.park.speed);
            simulation::advance(scene.day, game_seconds);
            // Each new week rates the park again; weeks run through in one frame are rated once.
            if (simulation::pass_time(scene.park, game_seconds) > 0)
            {
                // While the park runs, its own litter counts, and much of it is told to the player.
                const std::optional<std::int32_t> old = scene.simulate ? std::optional(simulation::old_litter(scene.sim)) : std::nullopt;
                profiling::section("Rating", [&] { simulation::rate_week(scene.park, scene.people, scene.rides, old); });
                if (old >= kLitterWarning)
                    simulation::post(scene.park.messages, { .text_key = "Thought_PI_Park_Litter1" });
            }
            simulation::age(scene.park.messages, game_seconds);
            if (scene.simulate)
            {
                // A long frame steps at most a few seconds, so the park catches up rather than stalls.
                constexpr float kMostSeconds = 2;
                scene.sim_time = std::min(scene.sim_time + game_seconds, kMostSeconds);
                scene.sim.registry.ctx().insert_or_assign(simulation::guest_weather(scene.park));
                scene.sim.registry.ctx().insert_or_assign(simulation::Daylight { scene.day.time });
                profiling::section("Guests", [&] {
                    for (; scene.sim_time >= simulation::kTickSeconds; scene.sim_time -= simulation::kTickSeconds)
                        simulation::step(scene.sim);
                });
                // Each thing broken costs the park some rating, and bad runs of it are told to the player.
                if (const auto* vandalism = scene.sim.registry.ctx().find<simulation::Vandalism>())
                {
                    const auto broken = static_cast<float>(vandalism->broken - scene.vandalism_seen);
                    if (scene.park.rating)
                        *scene.park.rating = std::max(0.0f, *scene.park.rating - kVandalismPenalty * broken / static_cast<float>(simulation::kMaxRating));
                    scene.vandalism_seen = vandalism->broken;
                    for (; scene.vandalism_warnings_seen < vandalism->warnings; ++scene.vandalism_warnings_seen)
                        simulation::post(scene.park.messages, { .text_key = "Thought_PI_Park_Vandalism1" });
                }
                // New thoughts go into the guests' saved thoughts, newest first, for their windows to show.
                for (ShownPerson& p : scene.shown_people)
                {
                    const auto* thoughts = p.staff || p.guest == entt::null || p.saved >= scene.people.guests.size() ? nullptr : scene.sim.registry.try_get<simulation::Thoughts>(p.guest);
                    if (thoughts == nullptr || thoughts->had == p.thoughts_seen)
                        continue;
                    auto& slots = scene.people.guests[p.saved].person.thoughts;
                    for (std::size_t k = std::min<std::size_t>(thoughts->had - p.thoughts_seen, thoughts->recent.size()); k-- > 0;)
                    {
                        const simulation::Thought& t = thoughts->recent[k];
                        const float newest = std::ranges::max(slots, { }, &save::Thought::time).time;
                        const auto* about = scene.sim.registry.valid(t.about) ? scene.sim.registry.try_get<simulation::Attraction>(t.about) : nullptr;
                        std::shift_right(slots.begin(), slots.end(), 1);
                        slots[0] = { .about = { about != nullptr ? about->saved : 0 }, .id = std::to_underlying(t.kind), .time = newest + 1, .variant = static_cast<std::int32_t>(t.variant) };
                    }
                    p.thoughts_seen = thoughts->had;
                }
                // Staff are paid from the park's cash, and the player hears of rides breaking down and being fixed.
                if (const auto* payroll = scene.sim.registry.ctx().find<simulation::Payroll>())
                {
                    if (scene.park.cash)
                        *scene.park.cash -= payroll->paid - scene.wages_seen;
                    scene.wages_seen = payroll->paid;
                }
                // The rides' upkeep into the saved rides, for their windows to show.
                for (std::size_t i = 0; i < scene.ride_entities.size() && i < scene.rides.rides.size(); ++i)
                {
                    const auto* upkeep = scene.ride_entities[i] != entt::null ? scene.sim.registry.try_get<simulation::Upkeep>(scene.ride_entities[i]) : nullptr;
                    if (upkeep == nullptr)
                        continue;
                    save::Ride& ride = scene.rides.rides[i];
                    ride.breakdown.reliability = upkeep->reliability * 100;
                    ride.breakdown.current = static_cast<save::BreakdownKind>(std::to_underlying(upkeep->kind));
                    ride.breakdown.last = static_cast<save::BreakdownKind>(std::to_underlying(upkeep->last));
                    ride.base.time_since_inspection = upkeep->since_inspection;
                    ride.breakdown.downtime_windows = upkeep->downtime;
                    ride.breakdown.saved_downtime_percent.reset();
                    using enum save::RepairStatus;
                    ride.base.repair_status = !upkeep->broken ? Fine : upkeep->mechanic == entt::null ? MechanicCalled : MechanicOnTheWay;
                }
                for (const auto [e, upkeep, attraction] : scene.sim.registry.view<const simulation::Upkeep, const simulation::Attraction>().each())
                {
                    const auto told = std::ranges::find(scene.broken_rides, e);
                    if (upkeep.broken == (told != scene.broken_rides.end()))
                        continue;
                    if (upkeep.broken)
                        scene.broken_rides.push_back(e);
                    else
                        scene.broken_rides.erase(told);
                    simulation::post(scene.park.messages, { .text_key = upkeep.broken ? "RideBrokenMessage" : "RideFixedMessage", .subject_name = attraction.name });
                }
            }
            profiling::section("Cars", [&] { place_cars(scene); });
        });
        profiling::section("Daylight", [&] { show_time_of_day(scene, viewer); });

        const profiling::Scope zone("Bar");
        if (bar != nullptr)
        {
            ui::ParkBarUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
            units.fahrenheit = options.choice(config::GameOption::TemperatureUnits) == kFahrenheit;
            // The game writes its times with am and pm in English only.
            units.twelve_hour = assets.fonts.language == ui::kDefaultLanguage;
            bar->show(ui::park_bar_values(scene.park, scene.day.time, *assets.strings, units));
        }
        if (auto* messages = static_cast<ui::MessageDrawer*>(desktop.root().find(kMessagesName)))
        {
            messages->show(ui::drawer_messages(scene.park.messages, *assets.strings));
            scene.message_alert = scene.message_alert || messages->take_alert();
        }
        if (auto* tools = static_cast<ui::ToolRow*>(desktop.root().find(kToolsName)))
        {
            const std::optional<ui::ParkTool> pressed = tools->take_pressed();
            if (pressed == ui::ParkTool::Leave)
                ask_leave(scene, *assets.strings);
            else if (pressed == ui::ParkTool::Map)
                toggle_park_map(scene.map, desktop, *assets.strings);
            else if (pressed == ui::ParkTool::Paths)
            {
                close_track_building(scene); // One tool panel at the bottom left at a time
                toggle_footpath_panel(device, scene, assets, options);
            }
            if (const auto item = tools->take_item())
            {
                if (*item == ui::ToolItem::People)
                    scene.show_people = tools->switched_on(*item);
                else if (*item == ui::ToolItem::Scenery)
                    scene.show_scenery = tools->switched_on(*item);
                else if (*item == ui::ToolItem::Options || *item == ui::ToolItem::About || *item == ui::ToolItem::Save || *item == ui::ToolItem::Load)
                    scene.window_requests.push_back(*item);
                else if (const auto topic = ui::management_topic(*item))
                    toggle_management_window(desktop, *topic, management_facts(scene), *assets.strings, info_units(assets, options));
                else
                    open_build_list(scene.build, desktop.root(), *item, tools->item(*item), *assets.strings, ui::currency(*assets.strings, options.choice(config::GameOption::Currency)));
            }
            update_build_menus(scene.build, desktop.root());
        }
        if (const auto picked = update_park_map(device, scene.map, desktop, scene.grid, scene.camera, aspect(device), [&] { return map_people(scene); }, elapsed))
        {
            scene.camera.target = { picked->x, scene.camera.target.y, picked->y };
            scene.camera = park::on_ground(scene.camera, scene.grid);
        }
        if (const auto row = update_management_windows(desktop, management_facts(scene), *assets.strings, info_units(assets, options)); row && info != nullptr)
            open_row(scene, *info, *row, assets, options);
        profiling::section("Footpaths", [&] { update_footpaths(device, scene, assets, options); });
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

    bool take_park_alert(ParkScene& scene)
    {
        return std::exchange(scene.message_alert, false);
    }

    render::Color park_background(const ParkScene& scene)
    {
        return scene.horizon;
    }

    namespace
    {
        /// Which parts of the world a draw shows: a reflection leaves some out, as the options ask.
        struct WorldParts
        {
            bool background = true; ///< Sky and mountains
            bool terrain = true;
            bool scenery = true;
            bool people = true;
            bool water = true;
            bool see_through = true;   ///< Blended parts, such as glass
            bool shaded_water = false; ///< With the water shader, seen from `eye`
            glm::vec3 eye { 0.0f };
        };

        /// The park's ground, objects, water, sky and mountains as `transform` sees them, lit by `light`.
        void draw_world(render::Device& device, ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light, const WorldParts& parts = { })
        {
            if (scene.sky_pipeline && parts.background)
            {
                const profiling::Scope zone("Sky");
                // The dome reads one column of its picture, so it must not wrap into the next.
                draw_transient(device, scene, scene.sky_mesh, scene.sky_textures, transform, light, [&](const park::ObjectBatch&) { return scene.sky_pipeline; }, [&](const park::ObjectBatch& b) { return b.texture == scene.sky.dome.texture ? render::AddressMode::Clamp : render::AddressMode::Repeat; });
            }
            // Before the ground, writing depth, so land reaching beyond the ring stays hidden behind it.
            if (parts.background)
                profiling::section("Mountains", [&] { draw_transient(device, scene, scene.mountains, scene.sky_textures, transform, light, [&](const park::ObjectBatch&) { return scene.cutout_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; }); });
            const auto pipeline_for = [&](const ShownObjects& shown, park::PartBlend blend) {
                const bool crossing = &shown != &scene.steady_objects;
                if (blend == park::PartBlend::Opaque)
                    return crossing ? scene.crossing_pipeline : scene.object_pipeline;
                if (blend == park::PartBlend::Cutout)
                    return crossing ? scene.crossing_cutout_pipeline : scene.cutout_pipeline;
                return crossing ? scene.crossing_see_through_pipeline : scene.see_through_pipeline;
            };
            const auto sided = [&](render::PipelineHandle pipeline, const park::ObjectBatch& batch) { return batch.one_sided ? scene.front_only.at(pipeline.id) : pipeline; };
            const auto object_pipeline = [&](const park::ObjectBatch& b) { return sided(pipeline_for(scene.steady_objects, b.blend), b); };
            const auto draw_shown = [&](const ShownObjects& shown, bool see_through) {
                if (!shown.indices)
                    return;
                for (const park::ObjectBatch& batch : shown.batches)
                {
                    if ((batch.blend == park::PartBlend::Blended) != see_through)
                        continue;
                    const auto texture = scene.object_textures.find(batch.texture);
                    render::DrawCommand draw;
                    draw.pipeline = sided(pipeline_for(shown, batch.blend), batch);
                    draw.vertices = { shown.vertices };
                    draw.indices = { shown.indices };
                    draw.index_type = render::IndexType::U32;
                    draw.first = batch.first;
                    draw.count = batch.count;
                    draw.transform = transform;
                    draw.texture = texture != scene.object_textures.end() ? texture->second.handle : scene.blank;
                    draw.filter = render::Filter::Linear;
                    draw.address = render::AddressMode::Repeat;
                    draw.light = light;
                    draw.extra_textures[1] = relief_of(scene.object_textures, batch.texture);
                    device.draw(draw);
                }
            };
            if (parts.terrain)
                profiling::section("Ground", [&] {
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
                        draw.light = light;
                        draw.extra_textures[0] = scene.variation;
                        draw.extra_textures[1] = relief_of(scene.ground_textures, park::ground_key(batch.paint, batch.index));
                        device.draw(draw);
                    }
                    draw_footpath_ghost(device, scene, transform, light);
                    draw_patrol(device, scene, transform, light);
                });
            const auto draw_objects = [&](bool see_through) {
                for (const ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                    draw_shown(*shown, see_through);
            };
            if (scene.show_scenery && parts.scenery)
                profiling::section("Objects", [&] { draw_objects(false); });
            if (scene.show_scenery && parts.scenery)
                profiling::section("Billboards", [&] {
                    for (const ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                    {
                        // shortcut: crossing billboards are copied with their bands each frame; fine for the
                        // few in bands, upgrade to a second vertex stream if many trees cross at once.
                        const std::vector<CrossingVertex> crossing = crossing_vertices(shown->billboards.mesh.vertices, shown->bands);
                        draw_transient(device, scene, shown->billboards.mesh, scene.object_textures, transform, light, [&](const park::ObjectBatch& b) { return sided(pipeline_for(*shown, b.blend), b); }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; }, std::as_bytes(std::span(crossing)));
                    }
                });
            if (scene.show_people && parts.people)
                profiling::section("People", [&] {
                    draw_transient(device, scene, scene.still_people, scene.people_textures, transform, light, [&](const park::ObjectBatch&) { return scene.object_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
                    draw_transient(device, scene, scene.moving_people, scene.people_textures, transform, light, [&](const park::ObjectBatch&) { return scene.object_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
                    if (scene.simulate)
                        draw_transient(device, scene, scene.riders.mesh, scene.people_textures, transform, light, [&](const park::ObjectBatch&) { return scene.object_pipeline; }, [](const park::ObjectBatch&) { return render::AddressMode::Clamp; });
                });
            if (scene.show_scenery && parts.scenery)
                profiling::section("Cars", [&] { draw_transient(device, scene, scene.car_meshes.mesh, scene.object_textures, transform, light, object_pipeline, [](const park::ObjectBatch&) { return render::AddressMode::Repeat; }); });
            profiling::section("Rides", [&] {
                for (const ParkScene::BonedRide& boned : scene.boned_rides)
                    draw_transient(device, scene, boned.mesh.mesh, scene.object_textures, transform, light, object_pipeline, [](const park::ObjectBatch&) { return render::AddressMode::Repeat; });
            });
            if (parts.water)
                profiling::section("Water", [&] { draw_park_water(device, scene.water, transform, light, parts.eye, scene.clock, parts.shaded_water); });
            if (scene.show_scenery && parts.scenery && parts.see_through)
                profiling::section("SeeThrough", [&] { draw_objects(true); });
        }

        constexpr std::uint32_t kShadowTexels = 2048;

        /// Makes the textures of `drawing`; false when the device cannot.
        bool make_shadow_drawing(render::Device& device, ShadowDrawing& drawing)
        {
            render::TextureDesc desc;
            desc.width = kShadowTexels;
            desc.height = kShadowTexels;
            desc.target = true;
            desc.readable_depth = true;
            desc.label = "shadow map";
            auto made = device.create_texture(desc);
            std::vector<std::byte> blank(5 * 16);
            auto view = device.create_texture({ .width = 5, .height = 1, .format = render::TextureFormat::Rgba32Float, .pixels = blank, .label = "shadow view" });
            if (!made || !view)
            {
                if (made)
                    device.destroy_texture(*made);
                if (view)
                    device.destroy_texture(*view);
                return false;
            }
            drawing.target = *made;
            drawing.view = *view;
            return true;
        }

        /// Rolls the flowing water's picture on as the water flows (park/water_flow.hpp). Call inside the screen
        /// pass; it ends the pass and begins it again keeping what was drawn when the picture moves.
        void roll_flow(render::Device& device, ParkScene& scene)
        {
            const auto texture = scene.object_textures.find(park::kFlowPicture);
            if (texture == scene.object_textures.end())
                return;
            const std::uint32_t shift = park::flow_shift(scene.flow_picture.height, scene.clock);
            if (shift == scene.flow_shift)
                return;
            scene.flow_shift = shift;
            const ovl::RgbaImage rolled = park::flow_rows(scene.flow_picture, shift);
            device.end_pass();
            device.update_texture(texture->second.handle, std::as_bytes(std::span(rolled.pixels)));
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

        /// Draws the park round the camera from `drawing.sun` into its map, outside any pass.
        void draw_shadow(render::Device& device, ParkScene& scene, ShadowDrawing& drawing, const park::SunView& sun)
        {
            // The depth bias: about a tenth of a world unit, in the map's depth.
            const std::array<float, 20> values
                = { sun.view_projection[0][0], sun.view_projection[0][1], sun.view_projection[0][2], sun.view_projection[0][3], sun.view_projection[1][0], sun.view_projection[1][1], sun.view_projection[1][2], sun.view_projection[1][3], sun.view_projection[2][0], sun.view_projection[2][1], sun.view_projection[2][2], sun.view_projection[2][3], sun.view_projection[3][0], sun.view_projection[3][1], sun.view_projection[3][2], sun.view_projection[3][3], sun.texel, 0.1f / (2 * park::kSunViewDepth), 0, 0 };
            drawing.drawn = sun.view_projection;
            device.update_texture(drawing.view, std::as_bytes(std::span(values)));
            render::PassDesc into;
            into.target = drawing.target;
            device.begin_pass(into);
            // Without shadows: the map would be read while drawn into.
            render::SceneLight light = scene.light;
            light.shadow_map = light.shadow_map_before = { };
            light.shadow_view = light.shadow_view_before = { };
            draw_world(device, scene, sun.view_projection, light, { .background = false, .water = false, .see_through = false });
            device.end_pass();
        }

        /// Draws the park round the camera from the sun into the shadow maps, and gives `scene.light` them;
        /// without shadows, or with the sun down, takes them away. Cut-out pictures leave their clear pixels out
        /// of the maps, so leaves and bushes cast their own shapes.
        ///
        /// The sun is drawn from only now and then, when it has gone half a degree, and the drawing before is
        /// kept: the shader mixes the two by how far the sun is on its way to the next step, so the shadow
        /// moves smoothly yet each map is drawn from a steady direction, without the crawl of one drawn anew
        /// each frame. It lags the sun by a step.
        void draw_shadow_map(render::Device& device, ParkScene& scene)
        {
            scene.light.shadow_map = { };
            scene.light.shadow_view = { };
            scene.light.shadow_map_before = { };
            scene.light.shadow_view_before = { };
            scene.light.shadow_before = 0;
            if (!scene.shadows || scene.light.sun_direction.y <= 0.02f)
                return;
            for (ShadowDrawing& drawing : scene.shadow_drawings)
            {
                if (!drawing.target && !make_shadow_drawing(device, drawing))
                {
                    scene.shadows = 0;
                    return;
                }
            }
            const float step = glm::radians(0.5f);
            const glm::vec3 to_sun = glm::normalize(scene.light.sun_direction);
            ShadowDrawing* now = &scene.shadow_drawings[scene.shadow_now];
            const bool stepped = park::steady_sun(now->sun, to_sun, step) != now->sun;
            if (stepped)
            {
                scene.shadow_now = 1 - scene.shadow_now;
                now = &scene.shadow_drawings[scene.shadow_now];
                now->sun = to_sun;
            }
            ShadowDrawing& before = scene.shadow_drawings[1 - scene.shadow_now];
            const bool blend = scene.blend_shadows && before.drawn != glm::mat4(0.0f);
            const float centre_radius = park::shadow_radius(scene.camera.distance);
            const park::SunView sun = park::sun_view(now->sun, scene.camera.target, centre_radius, kShadowTexels);
            const park::SunView sun_before = park::sun_view(before.sun, scene.camera.target, centre_radius, kShadowTexels);
            // Static shadows redraw when the view moves, or every two seconds to catch what changed in the park;
            // the older drawing then too, so both cover the same ground and show the same park. A step alone
            // draws only the new one: the older is the one drawn before it.
            // Dynamic shadows draw the newer every frame and the older, fading out, every other frame: a frame
            // behind is not seen, and it saves half the cost of the second drawing.
            const bool timed = scene.clock - scene.shadow_drawn_at > 2;
            const bool dynamic = scene.shadows > 1;
            scene.shadow_before_due = dynamic && !scene.shadow_before_due;
            const bool draw_now = dynamic || timed || stepped || sun.view_projection != now->drawn;
            const bool draw_before = blend && (timed || scene.shadow_before_due || sun_before.view_projection != before.drawn);
            scene.light.shadow_map = device.depth_texture(now->target);
            scene.light.shadow_view = now->view;
            if (blend)
            {
                scene.light.shadow_map_before = device.depth_texture(before.target);
                scene.light.shadow_view_before = before.view;
                scene.light.shadow_before = 1 - park::sun_step_share(now->sun, to_sun, step);
            }
            if (!draw_now && !draw_before)
                return;
            if (timed)
                scene.shadow_drawn_at = scene.clock;
            device.end_pass();
            if (draw_now)
                profiling::section("Now", [&] { draw_shadow(device, scene, *now, sun); });
            if (draw_before)
                profiling::section("Before", [&] { draw_shadow(device, scene, before, sun_before); });
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
        }

        /// Draws the attraction the info panel shows into the panel's round view, from its own camera.
        void draw_view(render::Device& device, ParkScene& scene, ui::InfoPanel& panel)
        {
            const float scale = scale_for(device, scene);
            const auto pixels = static_cast<std::uint32_t>(std::lround(ui::info_panel::kView * scale));
            if (scene.view_target && scene.view_pixels != pixels)
            {
                device.destroy_texture(scene.view_target);
                scene.view_target = { };
            }
            if (!scene.view_target && pixels > 0)
            {
                render::TextureDesc desc;
                desc.width = pixels;
                desc.height = pixels;
                desc.target = true;
                desc.label = "attraction view";
                if (auto made = device.create_texture(desc))
                {
                    scene.view_target = *made;
                    scene.view_pixels = pixels;
                }
            }
            if (!scene.view_target)
                return;
            device.end_pass();
            render::PassDesc into;
            into.target = scene.view_target;
            into.clear_color = scene.horizon;
            device.begin_pass(into);
            draw_world(device, scene, park::view_projection(scene.view_camera, 1), scene.light);
            device.end_pass();
            render::PassDesc back;
            back.clear = false;
            device.begin_pass(back);
            panel.show_view(ui::texture_id(scene.view_target));
        }
    } // namespace

    ui::CursorKind park_scene_cursor(const ParkScene& scene)
    {
        ui::ParkPointer pointer;
        pointer.interface = scene.desktop->cursor();
        pointer.turning = scene.turning;
        pointer.panning = scene.dragging;
        pointer.zooming = scene.zooming;
        pointer.over_pickable = scene.hover_pickable;
        // While the path panel shows its controls, a click on the ground starts a path there.
        if (const auto* paths = static_cast<const ui::FootpathPanel*>(scene.desktop->root().find(kFootpathPanelName)); paths != nullptr && paths->showing() && paths->page() == ui::FootpathPage::Controls)
            pointer.tool = ui::PointerTool::Path;
        return ui::park_cursor(pointer);
    }

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        profiling::section("Flow", [&] { roll_flow(device, scene); });
        if (auto* info = static_cast<ui::InfoPanel*>(scene.desktop->root().find(kInfoName)); info != nullptr && (scene.shown || scene.shown_person || scene.shown_prop))
            profiling::section("InfoView", [&] { draw_view(device, scene, *info); });
        const glm::mat4 view_projection = park::view_projection(scene.camera, aspect(device));
        const glm::vec3 eye = park::eye(scene.camera);
        profiling::section("Shadows", [&] { draw_shadow_map(device, scene); });
        profiling::section("Reflections", [&] {
            const WaterLook& look = scene.water.look;
            const WorldParts mirrored { .background = look.reflect_background, .terrain = look.reflect_terrain, .scenery = look.reflect_scenery, .people = look.reflect_people, .water = false };
            prepare_park_water(device, scene.water, view_projection, eye, scene.horizon, [&](const glm::mat4& transform) { draw_world(device, scene, transform, scene.light, mirrored); });
        });
        profiling::section("World", [&] {
            device.set_wireframe(scene.wireframe);
            draw_world(device, scene, view_projection, scene.light, { .shaded_water = true, .eye = eye });
            draw_track_ghost(device, scene, view_projection, scene.light);
            draw_raised_footpath_ghost(device, scene, view_projection, scene.light);
            device.set_wireframe(false);
        });
        if (scene.bloom)
            profiling::section("Glow", [&] { draw_park_glow(device, scene.glow); });

        const profiling::Scope zone("Interface");
        scene.list.clear();
        scene.list.set_scale(scale_for(device, scene));
        static const ui::FontSet kNoFonts;
        ui::DrawContext context { scene.list, assets.skins, scene.current_fonts != nullptr ? *scene.current_fonts : kNoFonts };
        scene.desktop->draw(context);
        ui::render_draw_list(device, assets.renderer, scene.list);
    }

    void destroy_park_scene(render::Device& device, ParkScene& scene)
    {
        finish_builds(scene);
        destroy_park_water(device, scene.water);
        destroy_park_glow(device, scene.glow);
        destroy_save_browser(device, scene.saves);
        for (const render::BufferHandle b : { scene.ground_vertices, scene.ground_indices, scene.steady_objects.vertices, scene.steady_objects.indices, scene.finer_objects.vertices, scene.finer_objects.indices, scene.coarser_objects.vertices, scene.coarser_objects.indices })
        {
            if (b)
                device.destroy_buffer(b);
        }
        for (const render::PipelineHandle p : { scene.ground_pipeline, scene.object_pipeline, scene.cutout_pipeline, scene.see_through_pipeline, scene.crossing_pipeline, scene.crossing_cutout_pipeline, scene.crossing_see_through_pipeline, scene.sky_pipeline })
        {
            if (p)
                device.destroy_pipeline(p);
        }
        for (const auto& [id, p] : scene.front_only)
            device.destroy_pipeline(p);
        scene.front_only.clear();
        destroy_park_map(device, scene.map);
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
        for (const render::TextureHandle t : { scene.shadow_drawings[0].target, scene.shadow_drawings[0].view, scene.shadow_drawings[1].target, scene.shadow_drawings[1].view })
        {
            if (t)
                device.destroy_texture(t);
        }
        scene = { };
    }

} // namespace rct3::scene
