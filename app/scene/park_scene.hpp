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
#include "ovl/texture_image.hpp"
#include "park/attraction_objects.hpp"
#include "park/camera.hpp"
#include "park/moving_meshes.hpp"
#include "park/object_mesh.hpp"
#include "park/object_pick.hpp"
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
#include "scene/land_tools.hpp"
#include "scene/led_signs.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_glow.hpp"
#include "scene/park_map_view.hpp"
#include "scene/park_occlusion.hpp"
#include "scene/park_water.hpp"
#include "scene/ride_placing.hpp"
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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace rct3::scene
{

    /// The pointer and the keys that act for as long as they are held, in framebuffer pixels.
    struct ParkInput
    {
        ui::PointerState pointer;
        bool right = false;
        bool middle = false;
        bool shift = false;   ///< Either Shift key held down
        bool control = false; ///< Either Control key held down
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
    inline constexpr std::int32_t kVandalismPenalty = 10; ///< Of the rating for each thing broken.
    inline constexpr std::int32_t kLitterWarning = 20;    ///< Old pieces of litter that make the player be told.
    inline constexpr float kLivePageInterval = 1.0f;      ///< Seconds between showings of a page that follows the running park

    /// Behind the ground when the sky cannot be read.
    inline constexpr render::Color kParkSky = render::hex("#8cb3d9");
    /// How far the sky is drawn from the eye: inside the camera's far limit.
    inline constexpr float kSkyRadius = park::kFar * 0.9f;

    /// Seconds between posing the riders again; their cars carry them every frame between.
    inline constexpr float kRidersRebuildInterval = 1.0f / 15.0f;

    /// The park drawn from the sun into a shadow map.
    struct ShadowDrawing
    {
        render::TextureHandle target; ///< For its depth
        render::TextureHandle view;   ///< How it was drawn, for the Lit shaders
        glm::vec3 sun { 0.0f, 1.0f, 0.0f };
        glm::mat4 drawn { 0.0f }; ///< The view it was last drawn from; 0 when not drawn yet
    };

    /// Objects' shapes and billboards at some levels.
    struct ObjectShapes
    {
        park::ObjectMesh mesh;
        park::Billboards billboards;
    };

    /// The objects' meshes for new levels, built away from the frame: those drawn at one level apart from those
    /// crossing between two, at each.
    struct ObjectBuild
    {
        park::LevelPlan plan;
        ObjectShapes steady;
        ObjectShapes finer;
        ObjectShapes coarser;
    };

    /// A vertex of an object crossing between two levels: as GroundVertex, then its object's anchor and the
    /// distances its band starts and ends at, and its side (render::Shader::LitCrossing).
    struct CrossingVertex
    {
        park::GroundVertex ground;
        glm::vec4 band { 0.0f };
        glm::vec2 side { 0.0f };
    };

    /// Lines over the ground in buffers for drawing (park/view_lines.hpp), made when first shown and again
    /// when the ground changes.
    struct ViewLines
    {
        render::BufferHandle vertices;
        render::BufferHandle indices;
        std::uint32_t count = 0;
        bool stale = true; ///< The ground changed since they were made
    };

    /// Objects' shapes in buffers for drawing, and their billboards, turned whenever the eye moves. For crossing
    /// objects, the buffers hold CrossingVertex, and `bands` each billboard corner's band and side.
    struct ShownObjects
    {
        std::vector<park::ObjectBatch> batches;
        render::BufferHandle vertices;
        render::BufferHandle indices;
        park::Billboards billboards;
        std::vector<std::pair<glm::vec4, glm::vec2>> bands;
    };

    /// The still people's mesh for new levels, built away from the frame.
    struct PeopleBuild
    {
        park::ObjectLevels levels;
        park::ObjectMesh mesh;
        bool simulate = false; ///< Whether it left out the simulated guests
    };

    /// The riders posed away from the frame: their mesh, and what carries each piece of it.
    struct RidersBuild
    {
        park::MovingMeshes riders;
        std::vector<std::size_t> cars;
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
        std::uint32_t thoughts_seen = 0; ///< Of the simulated guest's thoughts, those already in the saved person
    };

    /// A photo of a player's guest to draw: where from, and the start of its file name.
    struct PhotoShot
    {
        park::ParkCamera camera;
        std::string name;
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
        /// The ground drawn see-through while building under it: its depth alone, then its colour faded over
        /// what is behind, so only its nearest layer shows.
        render::PipelineHandle ground_depth_pipeline;
        render::PipelineHandle ground_faded_pipeline;
        ParkWater water;
        ParkGlow glow;
        bool bloom = false; ///< The Bloom option: glow round bright parts
        ParkOcclusion occlusion;
        bool ambient_occlusion = false; ///< Our Ambient occlusion option
        int surface_detail = 0;         ///< The Surface detail option (render::SceneLight::surface_detail)
        /// Relief maps are made for the pictures loaded, as the Surface detail option was when the park loaded:
        /// they cost memory and loading time, so a park loaded without them gets them only when loaded again.
        bool relief_maps = false;
        /// How quickly far land fades into haze, per world unit beyond its start. The default leaves the hills
        /// round the park about half hidden; weather can raise it for misty or rainy days, or set 0 for none.
        float haze_rate = 0.0004f;
        bool wireframe = false;     ///< Developer switch: outline the world's triangles
        bool tint_crossing = false; ///< Developer switch: objects crossing levels show the finer blue, the coarser red
        bool free_camera = false;   ///< Developer switch: no camera limits, and the camera leaves the ground
        ui::UiTextures ground_textures;
        render::TextureHandle blank;     ///< For ground without a picture
        render::TextureHandle variation; ///< Noise the ground's shade wanders by (park/ground_variation.hpp)
        std::vector<save::PlacedObject> objects;
        park::ObjectModels models;
        park::ModelPictures pictures; ///< For painting the batches new levels bring
        park::Leans leans;            ///< Of the built pieces that follow a slope
        park::Fitted fitted;          ///< Of the built pieces placed off the tile grid
        std::vector<glm::vec3> anchors;
        /// The boxes the objects are clicked in, worked out again when objects are added.
        mutable std::vector<park::PickBox> pick_boxes;
        mutable std::size_t pick_boxes_of = 0; ///< How many objects they were worked out for
        park::LevelPlan levels;                ///< Those the object buffers hold
        /// Of a level's distance, how far before its edge objects start blending into the next; 0 jumps. Set
        /// from the options each frame, and handed to each build.
        float blend_band = 0;
        float since_rebuild = 0; ///< Seconds since the levels were last looked at
        /// Meshes for levels that differ from those shown, while being built. Building only reads what is
        /// set when the park loads, so it may run beside the frame; it must be waited for before the scene
        /// goes.
        std::future<std::optional<ObjectBuild>> object_build;
        ShownObjects steady_objects;                          ///< Objects drawn at one level
        ShownObjects finer_objects;                           ///< Those crossing between two, at the finer
        ShownObjects coarser_objects;                         ///< The same at the coarser
        std::optional<glm::vec3> billboards_eye;              ///< The eye they face, none when not turned yet
        render::PipelineHandle object_pipeline;               ///< Opaque parts
        render::PipelineHandle cutout_pipeline;               ///< Parts with holes
        render::PipelineHandle crossing_pipeline;             ///< Opaque parts of crossing objects
        render::PipelineHandle crossing_cutout_pipeline;      ///< Their parts with holes
        render::PipelineHandle crossing_see_through_pipeline; ///< Their blended parts
        render::PipelineHandle see_through_pipeline;          ///< Blended parts, drawn last without writing depth
        /// By the id of each object pipeline above, the same pipeline drawing only the front of one-sided parts.
        std::map<std::uint32_t, render::PipelineHandle> front_only;
        ui::UiTextures object_textures;
        ovl::RgbaImage flow_picture;  ///< The flowing water's, still; empty without one (park/water_flow.hpp)
        std::uint32_t flow_shift = 0; ///< Rows it is rolled by on the GPU
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
        bool simulate = true;     ///< Run the simulation and show its guests; off only to see the park as saved
        float sim_time = 0;       ///< Park seconds the simulation has still to step
        /// Ride cars, drawn apart from the objects so the simulation can move them round their rides' courses.
        std::vector<save::PlacedObject> cars;
        std::vector<park::CarSpot> car_spots;                  ///< By car, as the park kept them
        park::Fitted saved_cars;                               ///< Where the park kept each car, by id
        std::vector<glm::mat4> car_places;                     ///< Where each car stands now, by car
        park::MovingMeshes car_meshes;                         ///< At car_places
        std::vector<std::optional<park::TrackCourse>> courses; ///< By ride, of those whose trains go round
        std::vector<entt::entity> ride_entities;
        /// The cars of each simulated ride, by their places in `cars`, front to back.
        std::map<entt::entity, std::vector<std::size_t>> ride_cars; ///< In the simulation, by ride; null when not there
        /// The object each simulated flat ride stands as, the one with seats, by its place in `objects`.
        std::map<entt::entity, std::size_t> ride_seats;
        /// A flat ride its bones move, or scenery its bones move on its own (animatronics): drawn apart from the
        /// other objects, a piece per bone.
        struct BonedRide
        {
            entt::entity ride = entt::null; ///< Null for scenery, which loops its first motion
            std::size_t object = 0;         ///< Its place in `objects`
            glm::mat4 placed { 1.0f };
            park::MovingMeshes mesh;
            std::vector<std::int32_t> bones; ///< By piece of `mesh`
            std::size_t first_seat = 0;      ///< Its first seat's place in seat_carriers
            float reach = 0;                 ///< Of scenery, about how far it reaches from its place
        };
        std::vector<BonedRide> boned_rides;
        /// Where each boned ride's seat bones carry their seats now, all rides' seats in turn.
        std::vector<glm::mat4> seat_carriers;
        std::optional<park::PersonLibrary> person_models; ///< Of the park's people
        /// Umbrellas guests hold up, for adults and teens, then for children; empty when missing.
        std::array<std::optional<park::ObjectModel>, 2> umbrellas;
        std::vector<ShownPerson> shown_people;
        park::ObjectLevels people_levels; ///< Of each shown person, as the still people's mesh holds them
        park::ObjectMesh still_people;
        park::ObjectMesh moving_people; ///< The people in motion in view, as last posed
        float since_people_levels = kObjectRebuildInterval;
        std::future<PeopleBuild> people_build;      ///< Like object_build, for the still people
        std::future<park::ObjectMesh> moving_build; ///< The people in motion, posed away from the frame
        std::future<RidersBuild> riders_build;      ///< The riders, posed away from the frame
        /// Riders seated in the ride cars, each car's posed in the car's own space so they move with it.
        park::MovingMeshes riders;
        /// By piece of `riders`: what carries them, a car in `cars` or past those a seat in `seat_carriers`, or kNoCar
        /// where they sit still.
        std::vector<std::size_t> rider_cars;
        std::vector<glm::mat4> rider_places; ///< By piece of `riders`: its car's place, put each frame
        float since_riders = 0;
        ui::UiTextures people_textures;
        park::AttractionObjects attractions;                  ///< Which attraction each object belongs to, for clicks
        std::optional<park::AttractionRef> shown;             ///< The attraction the info panel tells about
        std::vector<ui::AttractionTab> shown_tabs;            ///< Its tabs in the panel's order
        ui::AttractionTab info_tab = ui::AttractionTab::View; ///< The tab last opened, opened again next time
        std::size_t info_section = 0;                         ///< Of the open tab's page
        float since_info_page = 0;                            ///< Seconds since a live page was shown again
        std::uint32_t vandalism_seen = 0;                     ///< Breaks the park has been charged for
        std::uint32_t vandalism_warnings_seen = 0;            ///< Warnings already posted as messages
        std::int64_t wages_seen = 0;                          ///< Of the staff's pay, what has come out of the park's cash
        std::vector<entt::entity> broken_rides;               ///< Rides the player has been told are broken down
        /// The attraction whose construction tab was pressed, for the construction window to take; the panel only asks.
        std::optional<park::AttractionRef> construct_requested;
        std::optional<std::size_t> shown_person;           ///< Of shown_people, the one the info panel tells about instead
        std::vector<ui::PersonTab> person_tabs;            ///< Their tabs in the panel's order
        ui::PersonTab person_tab = ui::PersonTab::General; ///< The tab last opened on a person
        bool painting_patrol = false;                      ///< Strokes on the ground paint the shown staff member's patrol area
        int patrol_brush = 0;                              ///< Of ui::kPatrolBrushes
        std::optional<bool> patrol_stroke;                 ///< While a stroke goes on: whether it adds tiles, or takes them away
        std::vector<save::BinState> bins;                  ///< Of the litter bins among the objects
        std::vector<save::SignState> signs;                ///< Of the signs among the objects
        std::optional<std::size_t> shown_prop;             ///< Of objects, the prop the info panel tells about instead
        std::vector<ui::SceneryTab> prop_tabs;             ///< Its tabs in the panel's order
        std::optional<ui::SceneryTab> prop_tab;            ///< Its open tab; none while only its name shows
        bool show_people = true;                           ///< Switched in the view menu
        bool show_scenery = true;                          ///< Objects and rides, switched in the view menu
        bool see_through_ground = false;                   ///< Likewise the ground drawn faded
        bool show_grid = false;                            ///< Likewise the tile grid over the ground
        bool show_contours = false;                        ///< Likewise contours over the ground
        bool show_supports = true;                         ///< Likewise the supports under paths and track
        bool show_path_heights = false;                    ///< Likewise a label of its height over each path
        bool show_ride_heights = false;                    ///< Likewise over each piece of track
        ViewLines grid_lines;                              ///< The tile grid over the ground, while the view menu shows it
        ViewLines contour_lines;                           ///< Likewise the contours
        ViewLines land_marks;                              ///< What the land tool out would work on
        park::ParkCamera view_camera;                      ///< Of the info panel's round view, turning slowly round the attraction
        render::TextureHandle view_target;                 ///< The round view is drawn into
        std::uint32_t view_pixels = 0;                     ///< Across the view's target
        std::vector<PhotoShot> photos;                     ///< Taken in the park, waiting to be drawn, one a frame
        render::TextureHandle photo_target;                ///< A photo is drawn into
        std::optional<std::string> photo_reading;          ///< The file name start of the photo read from the target this frame, for the app to save
        std::optional<ui::Point> press;                    ///< Where the left button went down over the park, until it is let go
        std::optional<ui::Point> right_press;              ///< Likewise the right button
        render::SceneLight light;                          ///< What the day clock shows, for this frame
        int shadows = 0;                                   ///< The Shadows option: none, static (drawn now and then), dynamic
        /// The shadow drawn at the sun's last kept direction, and the one before it, which fades out as the sun
        /// goes on towards its next step (park::sun_step_share).
        std::array<ShadowDrawing, 2> shadow_drawings;
        std::size_t shadow_now = 0;     ///< Which of them is the latest
        bool shadow_before_due = false; ///< Dynamic shadows draw the older drawing every other frame
        float shadow_drawn_at = 0;      ///< `clock` when they were last drawn
        bool blend_shadows = true;      ///< The BlendShadows option: without it, shadows step from one drawing to the next
        // On the heap: windows keep a reference to the desktop, and the scene moves.
        std::unique_ptr<ui::Desktop> desktop = std::make_unique<ui::Desktop>();
        const ui::FontSet* current_fonts = nullptr;
        ui::DrawList list;
        std::optional<float> scale;
        ui::Point last_pointer;
        float zoom_pending = 0; ///< Wheel zoom still to roll in, as notches towards the screen (negative away)
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
        std::vector<std::byte> base;                       ///< The original park file the park started from, kept for saving
        std::size_t own_objects = 0;                       ///< `objects` from the file; those after it were made on loading or built since
        std::unordered_set<std::uint64_t> columns;         ///< Support columns under rides and track, which a path laid under them takes away
        SaveBrowser saves;                                 ///< Behind the save and load windows over the park
        std::size_t file_windows_opened = 0;               ///< Counts them, so the browser tells a new one from the last
        std::optional<std::filesystem::path> park_to_load; ///< Picked in the load window, until taken
        ParkMap map;                                       ///< The map window's pictures and what they show
        ParkFootpaths footpaths;                           ///< The path panel's styles and the paths laid with it
        TrackBuilding building;                            ///< The construction panel's ride and the piece it would build
        LandTools land;                                    ///< The land menu's shape and paint tools
        RidePlacing placing;                               ///< The ride picked in a build list, while it is placed
        LedSigns led_signs;                                ///< The scrolling boards over queues and on park entrances
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
    [[nodiscard]] std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const config::GameOptions& options, const std::filesystem::path& path, std::uint64_t seed, const simulation::RatingModel& ratings, const ParkLoadReport& report = { });

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed);

    /// True once the player has confirmed leaving.
    [[nodiscard]] bool take_park_leave(ParkScene& scene);

    /// True once after new messages came into the park's message drawer.
    [[nodiscard]] bool take_park_alert(ParkScene& scene);

    /// The pointer's look over the park this frame (ui/park_cursor.hpp).
    [[nodiscard]] ui::CursorKind park_scene_cursor(const ParkScene& scene);

    /// Device pixels to each unit of the park's interface.
    [[nodiscard]] float park_interface_scale(const render::Device& device, const ParkScene& scene);

    /// Starts a ripple on the water under `at`, in interface units on a screen of `size`, if water is there. True
    /// if it did.
    bool ripple_at(ParkScene& scene, ui::Point at, ui::Point size);

    /// The colour to clear to before drawing the park.
    [[nodiscard]] render::Color park_background(const ParkScene& scene);

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    /// The park as it stands, in our own format (see parkfile/saved_park.hpp): the original park it started
    /// from and the player's changes. Guests, staff and the simulation are not kept.
    [[nodiscard]] std::vector<std::byte> save_park(const ParkScene& scene);

    /// Whether a box of the park's interface has the keyboard, so typed letters are not the game's keys.
    [[nodiscard]] bool typing(const ParkScene& scene);

    /// Has the simulated flat ride `ride_index` (of scene.rides) seat its riders on the object that has the
    /// seats, as flat_ride_timings finds it. Nothing for other rides, or one the simulation does not run.
    void seat_flat_ride(render::Device& device, ParkScene& scene, std::size_t ride_index);

    /// Has object `k` (of scene.objects) move on its own when its model has bones and it is scenery of no attraction,
    /// as animatronics are.
    void animate_scenery(render::Device& device, ParkScene& scene, std::size_t k);

    void destroy_park_scene(render::Device& device, ParkScene& scene);

} // namespace rct3::scene
