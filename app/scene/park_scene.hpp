// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park played: its ground drawn in 3D under a camera the player moves, and the interface over it. For now
// it shows the ground, water, paths and queues with their supports and railings, and the placed objects of
// a park file, the land round it, the mountains on the horizon and the sky and, for a tutorial park, the
// window that leads through the tutorial. Leaving the tutorial asks first; the owner then goes back to the
// front end.
//
// The camera, as in the game's normal mode: the right button drags the ground along, the middle button (or
// both) turns and tilts, the wheel zooms, and the keys the player bound to the game's camera commands do the
// same from the keyboard. With the option on, the pointer at an edge of the screen slides the view that way. Nothing moves
// the camera while the pointer is over a window. A click on an attraction opens the
// round panel that tells about it; a click on open water starts a ripple there. The Files menu's options and
// about items are left for the owner to open (park_windows.hpp).
//
// The scene borrows the interface's skins, strings and fonts from the front end, which must outlive it.
#pragma once

#include "config/game_options.hpp"
#include "park/attraction_objects.hpp"
#include "park/camera.hpp"
#include "park/object_mesh.hpp"
#include "park/person_mesh.hpp"
#include "park/ride_cars.hpp"
#include "park/sky_mesh.hpp"
#include "park/terrain_mesh.hpp"
#include "park/track_course.hpp"
#include "random/generator.hpp"
#include "render/device.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "scene/build_menus.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_glow.hpp"
#include "scene/park_map_view.hpp"
#include "scene/park_water.hpp"
#include "scene/save_browser.hpp"
#include "scene/track_building.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park_state.hpp"
#include "simulation/sim_world.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list.hpp"
#include "ui/interface_assets.hpp"
#include "ui/key_map.hpp"
#include "ui/park_cursor.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/options_session.hpp"
#include "ui/screens/park_bar.hpp"
#include "ui/screens/person_info.hpp"
#include "ui/screens/scenery_info.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rct3::scene
{

    /// The pointer and the keys that act for as long as they are held, in framebuffer pixels.
    struct ParkInput
    {
        ui::PointerState pointer;
        bool right = false;
        bool middle = false;
        bool left_key = false;
        bool right_key = false;
        bool up_key = false;
        bool down_key = false;
        bool zoom_in_key = false;
        bool zoom_out_key = false;
        bool turn_left_key = false;
        bool turn_right_key = false;
        bool tilt_up_key = false;
        bool tilt_down_key = false;
        bool in_window = false;                ///< The pointer is over the focused window, so it may push at its edges
        std::vector<ui::PlayCommand> commands; ///< The game's commands bound to the keys pressed
    };

    /// The object buffers are built again at most this often, in seconds, while the view moves.
    inline constexpr float kObjectRebuildInterval = 0.25f;

    /// Behind the ground when the sky cannot be read.
    inline constexpr render::Color kParkSky = render::hex("#8cb3d9");
    /// How far the sky is drawn from the eye: inside the camera's far limit.
    inline constexpr float kSkyRadius = park::kFar * 0.9f;

    /// The people in motion at each level of detail, finest first, are posed again this often, in seconds: far
    /// people move too little on screen to need it as often. Only those in view are posed. Those standing
    /// still are only built again when one's level of detail changes, checked as often as objects' levels.
    inline constexpr std::array kPeopleRebuildIntervals { 1.0f / 30.0f, 1.0f / 15.0f, 1.0f / 6.0f };

    /// The objects' meshes for new levels, built away from the frame.
    struct ObjectBuild
    {
        park::ObjectLevels levels;
        park::ObjectMesh mesh;
        park::Billboards billboards;
    };

    /// The still people's mesh for new levels, built away from the frame.
    struct PeopleBuild
    {
        park::ObjectLevels levels;
        park::ObjectMesh mesh;
        bool simulate = false; ///< Whether it left out the simulated guests
    };

    /// A person as the park shows it.
    struct ShownPerson
    {
        park::PlacedPerson pose;
        float saved_time = 0;            ///< Into its motion, when the park was saved
        float speed = 1;                 ///< How fast its motion plays
        bool walking = false;            ///< Walkers stand still in their stride until people move
        bool staff = false;              ///< A member of staff rather than a guest
        std::size_t saved = 0;           ///< Of the guests or of the staff in the saved people
        entt::entity guest = entt::null; ///< The simulated guest it shows, for guests the simulation walks
    };

    struct ParkScene
    {
        save::TerrainGrid grid;
        std::uint64_t seed = 0;
        random::RandomState gameplay; ///< For the simulation; to be kept with a saved park
        park::ParkCamera camera;
        std::vector<park::GroundBatch> batches;
        render::BufferHandle ground_vertices;
        render::BufferHandle ground_indices;
        render::PipelineHandle ground_pipeline;
        ParkWater water;
        ParkGlow glow;
        bool bloom = false; ///< The Bloom option: glow round bright parts
        /// How quickly far land fades into haze, per world unit beyond its start. The default leaves the hills
        /// round the park about half hidden; weather can raise it for misty or rainy days, or set 0 for none.
        float haze_rate = 0.0004f;
        bool wireframe = false;   ///< Developer switch: outline the world's triangles
        bool free_camera = false; ///< Developer switch: no camera limits, and the camera leaves the ground
        ui::UiTextures ground_textures;
        render::TextureHandle blank;     ///< For ground without a picture
        render::TextureHandle variation; ///< Noise the ground's shade wanders by (park/ground_variation.hpp)
        std::vector<save::PlacedObject> objects;
        park::ObjectModels models;
        park::ModelPictures pictures; ///< For painting the batches new levels bring
        park::Leans leans;            ///< Of the built pieces that follow a slope
        park::Fitted fitted;          ///< Of the built pieces placed off the tile grid
        std::vector<glm::vec3> anchors;
        park::ObjectLevels levels; ///< Those the object buffers hold
        float since_rebuild = 0;   ///< Seconds since the levels were last looked at
        /// Meshes for levels that differ from those shown, while being built. Building only reads what is
        /// set when the park loads, so it may run beside the frame; it must be waited for before the scene
        /// goes.
        std::future<std::optional<ObjectBuild>> object_build;
        std::vector<park::ObjectBatch> object_batches;
        render::BufferHandle object_vertices;
        render::BufferHandle object_indices;
        park::Billboards billboards;                 ///< Gathered with the levels, turned whenever the eye moves
        std::optional<glm::vec3> billboards_eye;     ///< The eye they face, none when not turned yet
        render::PipelineHandle object_pipeline;      ///< Opaque parts
        render::PipelineHandle cutout_pipeline;      ///< Parts with holes
        render::PipelineHandle see_through_pipeline; ///< Blended parts, drawn last without writing depth
        ui::UiTextures object_textures;
        park::SkyModels sky;
        park::ObjectMesh sky_mesh; ///< Built again each frame round the eye
        ui::UiTextures sky_textures;
        render::PipelineHandle sky_pipeline; ///< Drawn first, behind everything, without depth
        render::Color horizon = kParkSky;    ///< What shows below the sky
        std::vector<glm::vec4> horizon_band; ///< Of the sky's picture, for the horizon at each time of day
        park::ObjectMesh mountains;          ///< Round the map; empty when not shown
        float clock = 0;                     ///< Seconds the park has been shown
        simulation::ParkState park;          ///< Money, guests, rating, weather, date and speed as they are now
        BuildMenus build;                    ///< The lists of rides, shops and scenery to build
        save::ParkModel park_model;          ///< As saved: finances, objectives, research; read by the management windows
        simulation::DayClock day;            ///< The park's time of day
        save::RideModel rides;
        save::PeopleModel people; ///< Guests, staff and litter as saved; read for the weekly rating
        simulation::SimWorld sim; ///< The paths, shops and guests the simulation runs
        bool simulate = false;    ///< Developer switch: run the simulation and show its guests
        float sim_time = 0;       ///< Park seconds the simulation has still to step
        /// Ride cars, drawn apart from the objects so the simulation can move them round their rides' courses.
        std::vector<save::PlacedObject> cars;
        std::vector<park::CarSpot> car_spots;                  ///< By car, as the park kept them
        park::Fitted saved_cars;                               ///< Where the park kept each car, by id
        park::Fitted car_places;                               ///< Where each car stands now, by id
        park::ObjectMesh car_mesh;                             ///< At car_places
        std::vector<std::optional<park::TrackCourse>> courses; ///< By ride, of those whose trains go round
        std::vector<entt::entity> ride_entities;               ///< In the simulation, by ride; null when not there
        std::optional<park::PersonLibrary> person_models;      ///< Of the park's people
        std::vector<ShownPerson> shown_people;
        park::ObjectLevels people_levels; ///< Of each shown person, as the still people's mesh holds them
        park::ObjectMesh still_people;
        std::array<park::ObjectMesh, park::kPersonLevels> moving_people; ///< By level of detail
        std::array<float, park::kPersonLevels> since_people { kPeopleRebuildIntervals };
        float since_people_levels = kObjectRebuildInterval;
        std::future<PeopleBuild> people_build; ///< Like object_build, for the still people
        ui::UiTextures people_textures;
        park::AttractionObjects attractions;                  ///< Which attraction each object belongs to, for clicks
        std::optional<park::AttractionRef> shown;             ///< The attraction the info panel tells about
        std::vector<ui::AttractionTab> shown_tabs;            ///< Its tabs in the panel's order
        ui::AttractionTab info_tab = ui::AttractionTab::View; ///< The tab last opened, opened again next time
        std::size_t info_section = 0;                         ///< Of the open tab's page
        /// The attraction whose construction tab was pressed, for the construction window to take; the panel only asks.
        std::optional<park::AttractionRef> construct_requested;
        std::optional<std::size_t> shown_person;           ///< Of shown_people, the one the info panel tells about instead
        std::vector<ui::PersonTab> person_tabs;            ///< Their tabs in the panel's order
        ui::PersonTab person_tab = ui::PersonTab::General; ///< The tab last opened on a person
        std::vector<save::BinState> bins;                  ///< Of the litter bins among the objects
        std::vector<save::SignState> signs;                ///< Of the signs among the objects
        std::optional<std::size_t> shown_prop;             ///< Of objects, the prop the info panel tells about instead
        std::vector<ui::SceneryTab> prop_tabs;             ///< Its tabs in the panel's order
        std::optional<ui::SceneryTab> prop_tab;            ///< Its open tab; none while only its name shows
        bool show_people = true;                           ///< Switched in the view menu
        bool show_scenery = true;                          ///< Objects and rides, switched in the view menu
        park::ParkCamera view_camera;                      ///< Of the info panel's round view, turning slowly round the attraction
        render::TextureHandle view_target;                 ///< The round view is drawn into
        std::uint32_t view_pixels = 0;                     ///< Across the view's target
        std::optional<ui::Point> press;                    ///< Where the left button went down over the park, until it is let go
        render::SceneLight light;                          ///< What the day clock shows, for this frame
        int shadows = 0;                                   ///< The Shadows option: none, static (drawn now and then), dynamic
        render::TextureHandle shadow_target;               ///< The park drawn from the sun, for its depth
        render::TextureHandle shadow_view;                 ///< How it was drawn, for the Lit shaders
        glm::vec3 shadow_sun { 0.0f, 1.0f, 0.0f };         ///< The sun direction the map was last drawn with
        glm::mat4 shadow_drawn { 0.0f };                   ///< The view the map was last drawn from
        float shadow_drawn_at = 0;                         ///< `clock` when it was
        // On the heap: windows keep a reference to the desktop, and the scene moves.
        std::unique_ptr<ui::Desktop> desktop = std::make_unique<ui::Desktop>();
        const ui::FontSet* current_fonts = nullptr;
        ui::DrawList list;
        std::optional<float> scale;
        ui::Point last_pointer;
        bool turning = false;
        bool dragging = false;
        int zooming = 0;             ///< From the keys: 1 in, -1 out
        bool hover_pickable = false; ///< A click would open a panel on what is under the pointer
        float since_hover_pick = 0;  ///< Seconds since that was looked at
        bool leave = false;
        bool message_alert = false;
        std::vector<ui::ToolItem> window_requests;         ///< Files menu items that open a window the owner opens (park_windows.hpp)
        std::unique_ptr<ui::OptionsSession> options;       ///< The options window over the park, once opened
        std::filesystem::path file;                        ///< The park file loaded, whose name saving offers
        SaveBrowser saves;                                 ///< Behind the save and load windows over the park
        std::size_t file_windows_opened = 0;               ///< Counts them, so the browser tells a new one from the last
        std::optional<std::filesystem::path> park_to_load; ///< Picked in the load window, until taken
        ParkMap map;                                       ///< The map window's pictures and what they show
        ParkFootpaths footpaths;                           ///< The path panel's styles and the paths laid with it
        TrackBuilding building;                            ///< The construction panel's ride and the piece it would build
    };

    /// The stages of loading a park, in the order they run.
    enum class ParkLoadStage : std::uint8_t
    {
        ReadFile, ///< Reading and taking apart the park file
        Ground,   ///< The ground, water, paths and queues, and their pictures
        Objects,  ///< The placed objects' models, and the supports, railings, fences and trees built round them
        Scene,    ///< What is shown at first: the objects in view, the sky, the people and the interface
    };
    inline constexpr std::size_t kParkLoadStages = 4;

    /// Told as each stage begins, so that the owner can show how far loading has got. Loading waits for it.
    using ParkLoadReport = std::function<void(ParkLoadStage)>;

    /// Loads the park file at `path`, its objects as the game options ask. The strings must outlive the scene.
    /// Its random numbers come from `seed`: the same park and seed give the same park.
    [[nodiscard]] std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const config::GameOptions& options, const std::filesystem::path& path, std::uint64_t seed, const ParkLoadReport& report = { });

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed);

    /// True once the player has confirmed leaving.
    [[nodiscard]] bool take_park_leave(ParkScene& scene);

    /// True once after new messages came into the park's message drawer.
    [[nodiscard]] bool take_park_alert(ParkScene& scene);

    /// The pointer's look over the park this frame (ui/park_cursor.hpp).
    [[nodiscard]] ui::CursorKind park_scene_cursor(const ParkScene& scene);

    /// Starts a ripple on the water under `at`, in interface units on a screen of `size`, if water is there. True
    /// if it did.
    bool ripple_at(ParkScene& scene, ui::Point at, ui::Point size);

    /// The colour to clear to before drawing the park.
    [[nodiscard]] render::Color park_background(const ParkScene& scene);

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    void destroy_park_scene(render::Device& device, ParkScene& scene);

} // namespace rct3::scene
