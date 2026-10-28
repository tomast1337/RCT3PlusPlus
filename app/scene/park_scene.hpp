// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park played: its ground drawn in 3D under a camera the player moves, and the interface over it. For now
// it shows the ground, water, paths and queues with their supports and railings, and the placed objects of
// a park file, the land round it, the mountains on the horizon and the sky and, for a tutorial park, the
// window that leads through the tutorial. Leaving the tutorial asks first; the owner then goes back to the
// front end.
//
// The camera moves as the control mode option says. In the normal mode the right button drags the ground
// along, the middle button (or both) turns and tilts, the wheel zooms, and the keys the player bound to the
// game's camera commands do the same from the keyboard. The isometric mode looks down from one of the map's
// four diagonals at a fixed tilt, and right with left (or the turn keys) turns it a quarter at a time. Free
// look flies the eye about: the right button looks round, the middle button and the keys fly it. The
// advanced mode turns round the point a drag starts on and shares the buttons out as the camera controls
// option picks. With
// the option on, the pointer at an edge of the screen slides the view that way. Nothing moves
// the camera while the pointer is over a window. A click on an attraction opens the
// round panel that tells about it; a click on open water starts a ripple there. The Files menu's options and
// about items are left for the owner to open (park_windows.hpp).
//
// The scene borrows the interface's skins, strings and fonts from the front end, which must outlive it.
#pragma once

#include "config/game_options.hpp"
#include "ovl/texture_image.hpp"
#include "park/attraction_objects.hpp"
#include "park/bone_poses.hpp"
#include "park/camera.hpp"
#include "park/moving_meshes.hpp"
#include "park/object_mesh.hpp"
#include "park/object_pick.hpp"
#include "park/person_mesh.hpp"
#include "park/pools.hpp"
#include "park/ride_cars.hpp"
#include "park/sky_mesh.hpp"
#include "park/terrain_mesh.hpp"
#include "park/track_course.hpp"
#include "park/track_rules.hpp"
#include "park/weather_look.hpp"
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
#include "scene/park_lens.hpp"
#include "scene/park_map_view.hpp"
#include "scene/park_occlusion.hpp"
#include "scene/park_particles.hpp"
#include "scene/park_rain.hpp"
#include "scene/park_supports.hpp"
#include "scene/park_water.hpp"
#include "scene/ride_placing.hpp"
#include "scene/save_browser.hpp"
#include "scene/structure_tools.hpp"
#include "scene/track_building.hpp"
#include "scene/zoo_animals.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/objectives.hpp"
#include "simulation/park_graphs.hpp"
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
#include "ui/screens/party_info.hpp"
#include "ui/screens/person_info.hpp"
#include "ui/screens/scenery_info.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/ui_textures.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rct3::scene
{

    /// The mouse buttons holding a drag of the camera.
    enum class DragButtons : std::uint8_t
    {
        None,
        Right,
        Both, ///< Right and left
        Middle,
    };

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
    inline constexpr float kStillBrokenReminder = 300;    ///< Game seconds a ride stays broken between reminders.
    inline constexpr float kLowReliability = 0.4f;        ///< Under it the player is told once, and again after it climbs back.

    /// A ride the player has been told is broken down.
    struct BrokenRide
    {
        entt::entity ride = entt::null;
        float unrepaired = 0; ///< Game seconds since the player was last told
    };
    inline constexpr float kLivePageInterval = 1.0f; ///< Seconds between showings of a page that follows the running park

    /// Behind the ground when the sky cannot be read.
    inline constexpr render::Color kParkSky = render::hex("#8cb3d9");
    /// How far the sky is drawn from the eye: inside the camera's far limit.
    inline constexpr float kSkyRadius = park::kFar * 0.9f;

    /// Seconds between posing the riders again; their cars carry them every frame between.
    inline constexpr float kRidersRebuildInterval = 1.0f / 15.0f;

    /// The park drawn from the sun into a shadow map.
    struct ShadowDrawing
    {
        render::TextureHandle target;   ///< For its depth
        std::array<float, 20> view { }; ///< How it was drawn, a row of render::SceneLight::shadow_view
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

    /// The delete menu's tools: one deletes what a click lands on, the other what stands on the tiles dragged over.
    struct DeleteTools
    {
        enum class Tool : std::uint8_t
        {
            None,
            One,
            Area,
        };
        Tool tool = Tool::None;
        std::optional<save::Tile> corner; ///< Of the area marked, where the drag began
        std::optional<save::Tile> other;  ///< And the opposite corner, where it is now or ended
        bool dragging = false;
        std::optional<std::array<std::int32_t, 4>> drawn; ///< Columns and rows the marks show now
        std::vector<bool> kept { };                       ///< Tiles the drag may not take in or out, row by row; filled as it begins
        /// What letting go would do: the enclosures it would leave, and what that costs (below 0 when the fence
        /// taken down pays back more than any put up), for the columns and rows marked.
        struct Marked
        {
            std::array<std::int32_t, 4> area { };
            save::EnclosureMap enclosures { };
            std::int64_t cost = 0;
        };
        std::optional<Marked> marked { };
    };

    /// The enclosure list's fence tool out (scene/enclosure_tools.hpp): a drag marks tiles into an enclosure, or
    /// with the delete tool out of one. Or an animal from the animal list, put down where the button goes up.
    struct EnclosureTool
    {
        std::optional<park::BuildItem> item;              ///< The fence, delete tool or animal picked; none while put away
        std::optional<save::Tile> corner;                 ///< Of the area marked, where the drag began
        std::optional<save::Tile> other;                  ///< And the opposite corner, where it is now
        std::optional<std::array<std::int32_t, 4>> drawn; ///< Columns and rows the marks show now
        std::vector<bool> kept { };                       ///< Tiles the drag may not take in or out, row by row; filled as it begins
        /// What letting go would do: the enclosures it would leave, and what that costs (below 0 when the fence
        /// taken down pays back more than any put up), for the columns and rows marked.
        struct Marked
        {
            std::array<std::int32_t, 4> area { };
            save::EnclosureMap enclosures { };
            std::int64_t cost = 0;
        };
        std::optional<Marked> marked { };
    };

    /// The pool list's tool out (scene/pool_tools.hpp): a drag lays squares of a pool, or takes them up; a
    /// click puts an extra in.
    struct PoolTool
    {
        std::optional<park::BuildItem> item;              ///< The tool picked; none while put away
        std::optional<save::Tile> corner;                 ///< Of the area marked, where the drag began
        std::optional<save::Tile> other;                  ///< And the opposite corner, where it is now
        std::optional<std::array<std::int32_t, 4>> drawn; ///< Columns and rows the marks show now
        std::int32_t drawn_look = -1;                     ///< And how: the part of the tile marked, and the colour
        std::optional<std::int32_t> height;               ///< Of the surface, when the pool panel does not snap it to the ground
        bool grid = false;                                ///< The pool panel's grid switch, as last seen
        park::PoolEdge edge = park::PoolEdge::Straight;   ///< The pool panel's edge setting, for the squares laid
        /// The squares worked on now, from where the drag began to where it is, or the square under the pointer.
        std::optional<std::pair<save::Tile, save::Tile>> working;
        std::int64_t cost = 0; ///< Of the work, below zero for money back
        bool ok = false;       ///< Whether the work may be done
        /// The pools before the work was laid into them to be seen, and the ground it dug, to put back.
        std::optional<park::PoolMap> before;
        std::vector<std::pair<save::Tile, std::array<float, 4>>> dug;
        bool redraw = false;                    ///< The pools were put back away from the frame and are drawn on the next
        std::int32_t turns = 0;                 ///< Of an extra, changing room or high diving board, quarter turns from the turn keys
        std::optional<float> press_y;           ///< Where on the screen the drag began
        std::int32_t board = park::kBoardLevel; ///< A high diving board's or a staircase's height, from the drag
        std::int32_t worked_board = 0;          ///< And the turn and height of the work shown
        std::int32_t worked_turns = 0;
        bool faces = true; ///< The work shown is no high diving board facing away from the water
        /// The extra or changing room where it would go, drawn see-through, and whether it may go there.
        std::optional<save::PlacedObject> ghost_of;
        bool ghost_ok = false;
        park::ObjectMesh ghost;
        /// The extra or changing room alone, for the pool panel's disc, and the view of it there.
        std::string preview_of;
        park::ObjectMesh preview;
        park::ParkCamera preview_camera;
        render::TextureHandle preview_target;
        std::uint32_t preview_pixels = 0;
        std::optional<std::size_t> room_placed; ///< Of the pools' rooms, one just placed, its window to open
    };

    /// An amount of money floating up from a point of the park (scene/park_view.hpp).
    struct FloatingMoney
    {
        glm::vec3 at { 0.0f };
        std::int64_t amount = 0; ///< Hundredths; above 0 for money in
        float age = 0;           ///< Seconds
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

    /// A person the player holds under the pointer (park_holding.hpp).
    struct HeldPerson
    {
        entt::entity who = entt::null;
        glm::vec3 from { 0.0f };       ///< Where they were picked up, to put them back
        std::optional<glm::vec3> spot; ///< The ground or path under the pointer
        bool allowed = false;          ///< They may be let go there
    };

    /// Ways of going on foot, as simulation::Gait numbers them.
    inline constexpr std::size_t kGaits = 5;

    /// A person as the park shows it.
    struct ShownPerson
    {
        park::PlacedPerson pose;
        float saved_time = 0; ///< Into its motion, when the park was saved
        float speed = 1;      ///< How fast its motion plays
        /// Ground its legs cover in a second of each way of going on foot (by simulation::Gait); 0 unknown
        std::array<float, kGaits> paces { };
        bool walking = false;            ///< Walkers stand still in their stride until people move
        bool staff = false;              ///< A member of staff rather than a guest
        std::size_t saved = 0;           ///< Of the guests or of the staff in the saved people
        entt::entity guest = entt::null; ///< The simulated guest it shows, for guests the simulation walks
        std::uint32_t thoughts_seen = 0; ///< Of the simulated guest's thoughts, those already in the saved person
    };

    /// A head, body or legs that a guest coming in at the gate may wear.
    struct Outfit
    {
        save::PeopleObjectId id = 0; ///< Of its part group in the park's people
        const park::PartSet* set = nullptr;
        save::PersonType type = save::PersonType::Adult;
        save::Sex sex = save::Sex::Male;
        std::uint8_t section = 0; ///< As a part group's
        std::uint8_t vip = 0;     ///< The celebrity it dresses, by the number VIP goals give them; 0 for guests'
    };

    /// A photo of a player's guest to draw: where from, and the start of its file name.
    struct PhotoShot
    {
        park::ParkCamera camera;
        std::string name;
    };

    /// Tiles across a square of the ground built and drawn on its own: a change builds again only the
    /// squares it reaches. Chosen by eye: small enough to build in a few milliseconds, few enough to draw.
    inline constexpr int kGroundChunk = 32;

    /// One square of the ground, or the land round the park.
    struct GroundChunk
    {
        render::BufferHandle vertices;
        render::BufferHandle indices;
        std::vector<park::GroundBatch> batches { };
        glm::vec3 low { 0.0f }; ///< Corners of the box round it
        glm::vec3 high { 0.0f };
        std::vector<park::WaterVertex> water { }; ///< Its lakes' surfaces, with the rest drawn as one
    };

    struct ParkScene
    {
        save::TerrainGrid grid;
        random::Seed seed;
        random::RandomState gameplay; ///< For the simulation; to be kept with a saved park
        park::ParkCamera camera;
        std::vector<GroundChunk> ground_chunks; ///< kGroundChunk tiles square, row by row, then the land round the park
        render::PipelineHandle ground_pipeline;
        /// The ground drawn see-through while building under it: its depth alone, then its colour faded over
        /// what is behind, so only its nearest layer shows.
        render::PipelineHandle ground_depth_pipeline;
        render::PipelineHandle ground_faded_pipeline;
        ParkWater water;
        ParkGlow glow;
        ParkLens lens;                                          ///< Water on the camera's lens
        ParkParticles particles;                                ///< The effects scenery and rides show
        ParkRain rain;                                          ///< Rain falling round the camera
        park::WeatherLook weather_look;                         ///< How the weather shows now
        random::RandomState weather_dice = random::seeded({ }); ///< Looks only: when lightning strikes
        bool thunder = false;                                   ///< A stroke's thunder is to be heard this frame
        bool bloom = false;                                     ///< The Bloom option: glow round bright parts
        bool light_shafts = false;                              ///< The Light shafts option
        ParkOcclusion occlusion;
        bool ambient_occlusion = false; ///< The Ambient occlusion option
        int surface_detail = 0;         ///< The Surface detail option (render::SceneLight::surface_detail)
        /// Relief maps are made for the pictures loaded, as the Surface detail option was when the park loaded:
        /// they cost memory and loading time, so a park loaded without them gets them only when loaded again.
        bool relief_maps = false;
        float texture_detail = 1; ///< As the option was when the park loaded (fit_texture_detail)
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
        park::ModelPictures pictures;            ///< For painting the batches new levels bring
        park::Leans leans;                       ///< Of the built pieces that follow a slope
        park::Fitted fitted;                     ///< Of the built pieces placed off the tile grid
        park::Broken broken_objects;             ///< Path items vandals have broken, drawn so
        std::vector<std::uint64_t> fence_panels; ///< The objects of the fence round the park's land, as last built
        /// By ride id, the objects of the fence the park built round the ride and beside its doors, as last built.
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> ride_fence_pieces;
        save::EnclosureMap enclosures;               ///< The animal enclosures' tiles and fences
        std::vector<std::uint64_t> enclosure_panels; ///< The objects of their fences, as last built
        park::PoolMap pools;                         ///< The swimming pools the player laid
        std::vector<std::uint64_t> pool_pieces;      ///< Their pieces' objects, as last built
        std::size_t saved_pool_water = 0;            ///< footpaths.pools of the park's own file; the pools' follow
        std::size_t entrance_centred = 0;            ///< Of the park's entrances, the one the view last went to
        std::vector<glm::vec3> anchors;
        std::vector<park::Lamp> lamps; ///< The objects' lamps at full strength; packed again when `lamps_due`
        bool lamps_due = true;
        bool car_lamps = false;             ///< Some ride car carries lamps, so they are packed again each frame
        float darkness = 0;                 ///< How dark the sky is, 0 by day to 1 at night (park::sky_darkness)
        render::TextureHandle lamp_texture; ///< render::SceneLight::lamps
        std::uint32_t lamp_rows = 0;        ///< Of that texture
        float lamp_strength = -1;           ///< The strength it was packed with
        /// Where the objects stand, and the boxes they are clicked in, brought up to date (place_objects) after a
        /// change or when objects are added.
        mutable park::ObjectPlaces places;
        mutable std::vector<park::PickBox> pick_boxes;
        mutable std::size_t pick_boxes_of = 0; ///< How many objects they were brought up to date for
        park::LevelPlan levels;                ///< Those the object buffers hold
        /// Of a level's distance, how far before its edge objects start blending into the next; 0 jumps. Set
        /// from the options each frame, and handed to each build.
        float blend_band = 0;
        park::LevelScales level_scales { }; ///< Each kind's detail and draw distance; set as blend_band is
        float since_rebuild = 0;            ///< Seconds since the levels were last looked at
        /// Meshes for levels that differ from those shown, while being built. Building only reads what is
        /// set when the park loads, so it may run beside the frame; it must be waited for before the scene
        /// goes.
        std::future<std::optional<ObjectBuild>> object_build;
        ShownObjects steady_objects;   ///< Objects drawn at one level
        ShownObjects finer_objects;    ///< Those crossing between two, at the finer
        ShownObjects coarser_objects;  ///< The same at the coarser
        std::size_t objects_shown = 0; ///< How many objects those were built from
        /// Objects added since the shown ones were built, drawn on their own until a build takes them in, so a
        /// piece laid shows at once.
        park::ObjectMesh fresh_objects;
        std::array<std::size_t, 2> fresh_for { };             ///< objects_shown and the objects' count it was built for
        std::optional<glm::vec3> billboards_eye;              ///< The eye they face, none when not turned yet
        render::PipelineHandle object_pipeline;               ///< Opaque parts
        render::PipelineHandle cutout_pipeline;               ///< Parts with holes
        render::PipelineHandle crossing_pipeline;             ///< Opaque parts of crossing objects
        render::PipelineHandle crossing_cutout_pipeline;      ///< Their parts with holes
        render::PipelineHandle crossing_see_through_pipeline; ///< Their blended parts
        render::PipelineHandle see_through_pipeline;          ///< Blended parts, drawn last without writing depth
        render::PipelineHandle depth_only_pipeline;           ///< Parts that write depth only
        render::PipelineHandle crossing_depth_only_pipeline;  ///< Those of crossing objects
        render::PipelineHandle chrome_pipeline;               ///< Parts drawn with park::kChromePicture
        render::PipelineHandle crossing_chrome_pipeline;      ///< Those of crossing objects
        /// By the id of each object pipeline above, the same pipeline drawing only the front of one-sided parts.
        std::map<std::uint32_t, render::PipelineHandle> front_only;
        ui::UiTextures object_textures;
        ovl::RgbaImage flow_picture;                          ///< The flowing water's, still; empty without one (park/water_flow.hpp)
        std::uint32_t flow_shift = 0;                         ///< Rows it is rolled by on the GPU
        park::ObjectMesh waterfall_mesh;                      ///< The waterfalls' water, built again each frame as it runs
        render::PipelineHandle waterfall_added_pipeline;      ///< Its pictures with a second added
        render::PipelineHandle waterfall_multiplied_pipeline; ///< And with a second multiplying them
        park::SkyModels sky;
        park::ObjectMesh sky_mesh; ///< Built again each frame round the eye
        ui::UiTextures sky_textures;
        render::PipelineHandle sky_pipeline; ///< Drawn first, behind everything, without depth
        render::Color horizon = kParkSky;    ///< What shows below the sky
        std::vector<glm::vec4> horizon_band; ///< Of the sky's picture, for the horizon at each time of day
        park::ObjectMesh mountains;          ///< Round the map; empty when not shown
        float clock = 0;                     ///< Seconds the park has been shown
        /// Park seconds the park has been shown: at the speed picked, stopped while paused. Everything in the
        /// park moves by it, so a pause freezes it all; the interface and its previews go by `clock`.
        float park_clock = 0;
        std::vector<float> chain_shifts; ///< By ride, how far its lift chain has moved (park/chain_lifts.hpp)
        simulation::ParkState park;      ///< Money, guests, rating, weather, date and speed as they are now
        BuildMenus build;                ///< The lists of rides, shops and scenery to build
        save::ParkModel park_model;      ///< As saved: finances, objectives, research; read by the management windows
        simulation::DayClock day;        ///< The park's time of day
        save::RideModel rides;
        std::vector<save::RideShow> shows; ///< What the show rides put on, by ride id
        save::PeopleModel people;          ///< Guests, staff and litter as saved; read for the weekly rating
        simulation::SimWorld sim;          ///< The paths, shops and guests the simulation runs
        bool simulate = true;              ///< Run the simulation and show its guests; off only to see the park as saved
        float sim_time = 0;                ///< Park seconds the simulation has still to step
        /// Ride cars, drawn apart from the objects so the simulation can move them round their rides' courses.
        std::vector<save::PlacedObject> cars;
        std::vector<park::CarSpot> car_spots; ///< By car, as the park kept them
        park::Fitted saved_cars;              ///< Where the park kept each car, by id
        std::vector<glm::mat4> car_places;    ///< Where each car stands now, by car
        std::optional<std::size_t> riding;    ///< The car the view rides along in, of `cars`
        /// The flat ride (of the park's rides) and the rider seat or fixed camera on it the view looks out from;
        /// never with `riding`.
        std::optional<std::pair<std::size_t, std::size_t>> riding_flat;
        bool riding_fixed = false; ///< On a flat ride, from one of its own fixed cameras rather than a seat
        /// Of `shown_people`, the person the view goes with, from their eyes or from behind them; never with
        /// `riding` or `riding_flat`.
        std::optional<std::size_t> riding_person;
        bool person_behind = false; ///< Following the person from behind rather than from their eyes
        float ride_pull = 0;        ///< How far the wheel has pulled the view back off its mount, in notches
        /// Of a person in `shown_people`, a new name asked by a script, given as the name box gives one.
        std::optional<std::pair<std::size_t, std::u16string>> rename_requested;
        std::optional<std::size_t> person_requested;         ///< Of a person in `shown_people`, their window asked by a script
        park::Seat seat;                                     ///< Its rider's eyes, while riding
        float seat_across = park::kRideWideView;             ///< How far across the screen the rider's view takes in
        park::RideEye riding_eye = park::RideEye::Front;     ///< The kind of camera ridden
        glm::vec2 ride_turn { };                             ///< How far the rider looks round from ahead, yaw and pitch
        bool ride_dragging = false;                          ///< A drag over the view is turning the rider's head
        park::RideView ride_view;                            ///< How the rider's view moved, while riding
        random::RandomState ride_dice = random::seeded({ }); ///< For the shake of the rider's view
        std::vector<std::string> hidden_for_ride;            ///< The windows put away while riding, to show again after
        bool paused_for_ride = false;                        ///< The park was paused, so riding set it going
        park::MovingMeshes car_meshes;                       ///< At car_mesh_places
        /// A car whose riders sit on an arm: its place in `cars`, its arm's joints at rest, and how the last
        /// joint, the riders' seat, is moved now in the car's own space.
        struct ArmCar
        {
            std::size_t car = 0;
            std::vector<glm::mat4> joints;
            glm::mat4 seats { 1.0f };
        };
        std::vector<ArmCar> arm_cars;
        std::vector<glm::mat4> car_mesh_places;                ///< car_places, then each arm car's joints from the base out
        std::vector<std::optional<park::TrackCourse>> courses; ///< By ride, of those whose track is known whole
        std::vector<park::TrackRules> track_rules;             ///< By ride, what its kind asks of its track; defaults past the end
        std::vector<entt::entity> ride_entities;
        std::vector<simulation::RideGroup> ride_groups;     ///< By ride, for the scenario's coaster goals and the awards
        std::int32_t awards_month = -1;                     ///< The calendar month the awards were last looked at in
        simulation::Takings takings;                        ///< Last month's, for the scenario's goals on money
        std::int64_t goals_day = -1;                        ///< The calendar day the scenario's goals were last checked on
        std::vector<simulation::GoalReading> goal_readings; ///< By place in the park's goals, as last checked
        /// The park's running totals as the week began, for its weekly graphs; unset until a week has ended.
        struct
        {
            std::int64_t intake = 0, income = 0, cash = 0;
            bool set = false;
        } week_began;
        /// The cars of each simulated ride, by their places in `cars`, front to back.
        std::map<entt::entity, std::vector<std::size_t>> ride_cars; ///< In the simulation, by ride; null when not there
        /// Of tracked rides whose trains were made here (fit_trains), by ride (of rides): how each car of a train
        /// sits on the track, by car.
        std::map<std::size_t, std::vector<park::CarFit>> car_fits;
        /// Tracked rides put to test or opened since the last update, whose trains are made ready to run then.
        std::vector<std::size_t> readying;
        /// Of tracked rides whose window has shown or built them, by ride: how many cars their trains may have.
        /// Their trains can be changed while they are not open.
        std::map<std::size_t, ui::TrainLimits> train_limits;
        /// Of the same rides: the trains their kind's file offers, by name, the first theirs at first.
        std::map<std::size_t, std::vector<std::string>> train_choices;
        /// The object each simulated flat ride stands as, the one with seats, by its place in `objects`.
        std::map<entt::entity, std::size_t> ride_seats;
        /// How the riders of each simulated flat ride sit, where its item says (see park::rider_motions).
        std::map<entt::entity, std::uint8_t> ride_seatings;
        /// A rider's last change of reaction as their pose shows it: from what to what, and scene.park_clock then.
        struct RiderTurn
        {
            simulation::RideReaction was = simulation::RideReaction::Waiting;
            simulation::RideReaction now = simulation::RideReaction::Waiting;
            float since = 0;
        };
        /// Of the riders posed last, by guest.
        std::map<entt::entity, RiderTurn> rider_turns;
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
            std::size_t seats = 0;           ///< Of seat_carriers it owns; none for gates and scenery
            std::vector<glm::mat4> views;    ///< Of a ride, how its fixed cameras' bones have moved, by number
            float reach = 0;                 ///< Of scenery, about how far it reaches from its place
            /// Of a ride's entrance or exit, its motions shut, opening, open and shutting (park::kGateShut...): it
            /// opens while the ride's riders walk through, on or off as `exit` says.
            std::optional<std::array<std::size_t, 4>> gate;
            bool exit = false;
            std::size_t step = park::kGateShut; ///< Which of the gate's motions plays
            float since = 0;                    ///< scene.park_clock when that motion began
            float open_until = 0;               ///< scene.park_clock till which the gate is to stay open
        };
        std::vector<BonedRide> boned_rides;
        /// Where each boned ride's seat bones carry their seats now, all rides' seats in turn.
        std::vector<glm::mat4> seat_carriers;
        std::optional<park::PersonLibrary> person_models;                     ///< Of the park's people
        std::map<const park::PartSet*, std::array<float, kGaits>> walk_paces; ///< Of each legs set measured so far, 0 for none
        /// Umbrellas guests hold up, for adults and teens, then for children; empty when missing.
        std::array<std::optional<park::ObjectModel>, 2> umbrellas;
        std::vector<ShownPerson> shown_people;
        std::vector<Outfit> wardrobe;                  ///< What guests coming in at the gate wear
        std::unordered_set<entt::entity> known_guests; ///< The simulated guests the shown people were made for
        random::RandomState looks;                     ///< For the colours of people's clothes left to pick
        park::ObjectLevels people_levels;              ///< Of each shown person, as the still people's mesh holds them
        park::ObjectMesh still_people;
        park::ObjectMesh moving_people; ///< The people in motion in view, as last posed
        float since_people_levels = kObjectRebuildInterval;
        std::future<PeopleBuild> people_build; ///< Like object_build, for the still people
        /// Kept for moving_build, the only one to touch it, so its memory is reused. Declared first, so it
        /// outlives a build still running when the scene goes.
        park::PersonRuns moving_runs { };
        std::future<park::ObjectMesh> moving_build; ///< The people in motion, posed away from the frame
        park::PersonRuns rider_runs { };            ///< Kept for riders_build, like moving_runs
        park::PersonRuns rider_gathered { };        ///< Kept for riders_build, like moving_runs
        std::future<RidersBuild> riders_build;      ///< The riders, posed away from the frame
        /// Riders seated in the ride cars, each car's posed in the car's own space so they move with it.
        park::MovingMeshes riders;
        /// By piece of `riders`: what carries them, a car in `cars` or past those a seat in `seat_carriers`, or kNoCar
        /// where they sit still.
        std::vector<std::size_t> rider_cars;
        std::vector<glm::mat4> rider_places; ///< By piece of `riders`: its car's place, put each frame
        float since_riders = 0;
        ui::UiTextures people_textures;
        DrawnAnimals drawn_animals;                           ///< The saved zoo animals, drawn with their models
        park::AttractionObjects attractions;                  ///< Which attraction each object belongs to, for clicks
        std::optional<park::AttractionRef> shown;             ///< The attraction the info panel tells about
        std::vector<ui::AttractionTab> shown_tabs;            ///< Its tabs in the panel's order
        ui::AttractionTab info_tab = ui::AttractionTab::View; ///< The tab last opened, opened again next time
        /// Tracked rides and the rest each keep the tab last opened, as in the game: this is the other one's, swapped
        /// with info_tab when the panel goes from one to the other.
        ui::AttractionTab other_info_tab = ui::AttractionTab::View;
        bool info_tracked = false;         ///< Whether info_tab is the tracked rides' one
        std::size_t info_section = 0;      ///< Of the open tab's page
        std::vector<std::u16string> tunes; ///< The install's music, by name, for rides to play
        park::CarriedKinds carried;        ///< What stalls sell and its extras, read when a stall's window first opens
        bool carried_read = false;
        /// The tune each ride plays, by ride id, as ui::AttractionFacts::tune; none for no music.
        /// shortcut: rides keep their tune only while the park is open and play nothing; save it and play it once
        /// ride music is read from and written to saves.
        std::unordered_map<std::uint32_t, std::size_t> ride_tunes;
        /// The tracked rides, by id, whose track is greased.
        /// shortcut: kept only while the park is open and the trains run as before; save it and lower the
        /// trains' losses once friction is part of the train model.
        std::unordered_set<std::uint32_t> low_friction;
        /// Which series each ride's graph shows, by ride id, as ui::AttractionFacts::graph_series; speed alone
        /// for those not listed. shortcut: not saved with the park; keep it with the ride once saves carry it.
        std::unordered_map<std::uint32_t, std::uint8_t> graph_series;
        float since_info_page = 0;                  ///< Seconds since a live page was shown again
        bool shown_closed = false;                  ///< The park closed the ride shown, as a crash does, so its page shows again
        std::uint32_t vandalism_seen = 0;           ///< Breaks the park has been charged for
        std::uint32_t vandalism_warnings_seen = 0;  ///< Warnings already posted as messages
        std::int64_t wages_seen = 0;                ///< Of the staff's pay, what has come out of the park's cash
        std::int64_t receipts_seen = 0;             ///< Of what guests have paid, what has gone into the park's cash
        std::vector<BrokenRide> broken_rides;       ///< Rides the player has been told are broken down
        std::vector<entt::entity> unreliable_rides; ///< Rides the player has been told are unreliable, until they recover
        /// Of each ride whose trains have crashed, the crashes, dangers and injured (simulation::Wrecks) already taken in.
        std::unordered_map<entt::entity, std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> crashes_seen;
        std::set<std::tuple<entt::entity, std::size_t, std::size_t>> wrecks_blown; ///< By ride, train and car: wrecks shown blowing up
        /// The attraction whose construction tab was pressed, for the construction window to take; the panel only asks.
        std::optional<park::AttractionRef> construct_requested;
        /// Whether the view rides along in a car, on a flat ride or with a person.
        [[nodiscard]] bool riding_along() const
        {
            return riding || riding_flat || riding_person;
        }
        std::optional<std::size_t> doors_requested;                 ///< A tracked ride whose circuit closed, to place its doors next
        std::optional<park::AttractionRef> info_requested;          ///< Asked by a script or a finished building, opened as a click on it does
        std::string refusal;                                        ///< String key: why the shown attraction was not opened, tested or edited
        std::optional<std::size_t> shown_person;                    ///< Of shown_people, the one the info panel tells about instead
        std::vector<ui::PersonTab> person_tabs;                     ///< Their tabs in the panel's order
        ui::PersonTab person_tab = ui::PersonTab::General;          ///< The tab last opened on a person
        std::vector<std::size_t> shown_party;                       ///< Of shown_people, the party the info panel tells about instead, its leader first
        ui::PartyTab party_tab = ui::PartyTab::Members;             ///< The tab last opened on a party
        bool painting_patrol = false;                               ///< Strokes on the ground paint the shown staff member's patrol area
        int patrol_brush = 0;                                       ///< Of ui::kPatrolBrushes
        std::optional<bool> patrol_stroke;                          ///< While a stroke goes on: whether it adds tiles, or takes them away
        std::optional<HeldPerson> holding;                          ///< The person the player has picked up
        std::vector<save::BinState> bins;                           ///< Of the litter bins among the objects
        std::vector<save::SignState> signs;                         ///< Of the signs among the objects
        std::optional<std::size_t> shown_prop;                      ///< Of objects, the prop the info panel tells about instead
        std::vector<ui::SceneryTab> prop_tabs;                      ///< Its tabs in the panel's order
        std::optional<ui::SceneryTab> prop_tab;                     ///< Its open tab; none while only its name shows
        bool show_people = true;                                    ///< Switched in the view menu
        bool show_scenery = true;                                   ///< Objects and rides, switched in the view menu
        bool see_through_ground = false;                            ///< Likewise the ground drawn faded
        bool show_grid = false;                                     ///< Likewise the tile grid over the ground
        bool show_contours = false;                                 ///< Likewise contours over the ground
        bool show_supports = true;                                  ///< Likewise the supports under paths and track
        bool show_path_heights = false;                             ///< Likewise a label of its height over each path
        bool show_ride_heights = false;                             ///< Likewise over each piece of track
        bool show_money = true;                                     ///< Likewise amounts floating up where money is spent or paid back
        bool show_guest_payments = false;                           ///< Likewise where guests pay for rides and stalls
        std::vector<FloatingMoney> money;                           ///< The amounts floating up now
        std::unordered_map<entt::entity, std::int64_t> income_seen; ///< Of each attraction's takings, what has floated up
        ViewLines grid_lines;                                       ///< The tile grid over the ground, while the view menu shows it
        ViewLines contour_lines;                                    ///< Likewise the contours
        ViewLines delete_marks;                                     ///< The tiles the area delete tool marked
        ViewLines enclosure_marks;                                  ///< The tiles the enclosure tool marks
        ViewLines pool_marks;                                       ///< The squares the pool tool marks
        ViewLines land_marks;                                       ///< What the land tool out would work on
        park::ParkCamera view_camera;                               ///< Of the info panel's round view, turning slowly round the attraction
        render::TextureHandle view_target;                          ///< The round view is drawn into
        std::uint32_t view_pixels = 0;                              ///< Across the view's target
        std::vector<PhotoShot> photos;                              ///< Taken in the park, waiting to be drawn, one a frame
        render::TextureHandle photo_target;                         ///< A photo is drawn into
        std::optional<std::string> photo_reading;                   ///< The file name start of the photo read from the target this frame, for the app to save
        std::optional<ui::Point> press;                             ///< Where the left button went down over the park, until it is let go
        std::optional<ui::Point> right_press;                       ///< Likewise the right button
        render::SceneLight light;                                   ///< What the day clock shows, for this frame
        int shadows = 0;                                            ///< The Shadows option: none, static (drawn now and then), dynamic
        /// The shadow drawn at the sun's last kept direction, and the one before it, which fades out as the sun
        /// goes on towards its next step (park::sun_step_share).
        std::array<ShadowDrawing, 2> shadow_drawings;
        std::size_t shadow_now = 0;         ///< Which of them is the latest
        render::TextureHandle shadow_views; ///< Both drawings' views, the latest first (render::SceneLight::shadow_view)
        bool shadow_before_due = false;     ///< Dynamic shadows draw the older drawing every other frame
        float shadow_drawn_at = 0;          ///< `clock` when they were last drawn
        bool blend_shadows = true;          ///< The BlendShadows option: without it, shadows step from one drawing to the next
        // On the heap: windows keep a reference to the desktop, and the scene moves.
        std::unique_ptr<ui::Desktop> desktop = std::make_unique<ui::Desktop>();
        const ui::FontSet* current_fonts = nullptr;
        ui::DrawList list;
        std::optional<float> scale;
        ui::Point last_pointer;
        float zoom_pending = 0;                                     ///< Wheel zoom still to roll in, as notches towards the screen (negative away)
        bool turning = false;                                       ///< The camera is being turned by a drag
        bool dragging = false;                                      ///< The camera is being slid by a drag
        DragButtons drag_buttons = DragButtons::None;               ///< Of the drag moving the camera
        park::ControlMode control_mode = park::ControlMode::Normal; ///< The camera last moved in
        int iso_step = 0;                                           ///< Isometric's diagonal, any whole number taken round the four
        float iso_drag = 0;                                         ///< Isometric: pixels across the turning drag has gone since it last turned
        std::optional<glm::vec3> pivot;                             ///< Advanced: what the drag turns round, picked when it started
        int zooming = 0;                                            ///< From the keys: 1 in, -1 out
        bool hover_pickable = false;                                ///< A click would open a panel on what is under the pointer
        float since_hover_pick = 0;                                 ///< Seconds since that was looked at
        bool leave = false;
        bool message_alert = false;
        std::optional<std::size_t> pool_complex_asked;                  ///< Of the pools' rooms, one whose complex the player was asked to sell
        std::optional<ui::SceneryTab> room_tab;                         ///< Last open in a changing room's window
        std::vector<park::AttractionRef> demolish_asked;                ///< Waiting on the question whether to pull them down
        std::vector<std::string> prizes;                                ///< Text keys told one box at a time, oldest first: prizes won, deletes refused
        std::vector<ui::ToolItem> window_requests;                      ///< Files menu items that open a window the owner opens (park_windows.hpp)
        std::unique_ptr<ui::OptionsSession> options;                    ///< The options window over the park, once opened
        std::filesystem::path file;                                     ///< The park file loaded, whose name saving offers
        std::vector<std::byte> base;                                    ///< The original park file the park started from, kept for saving
        std::size_t own_objects = 0;                                    ///< `objects` from the file; those after it were made on loading or built since
        std::unordered_set<std::uint64_t> placed;                       ///< Scenery the player put down since loading, by id; saved with the park
        std::optional<random::RandomState> off_map_stream;              ///< Where the trees beyond the edge left their stream; none when not grown
        std::size_t off_map_groves = 0;                                 ///< Groves grown beyond the edge
        bool modelled_plants = true;                                    ///< Trees and plants modelled, not flat, as the options were on loading
        std::unordered_map<std::uint64_t, std::pair<int, int>> columns; ///< Support pieces built, by id, and the tile their column stands on
        SupportWatch supports;                                          ///< How the park stood when its supports were last built
        SaveBrowser saves;                                              ///< Behind the save and load windows over the park
        ParkGame game;                                                  ///< Where the park's games go, and the career it counts for
        std::size_t file_windows_opened = 0;                            ///< Counts them, so the browser tells a new one from the last
        std::optional<std::filesystem::path> park_to_load;              ///< Picked in the load window, until taken
        ParkMap map;                                                    ///< The map window's pictures and what they show
        ParkFootpaths footpaths;                                        ///< The path panel's styles and the paths laid with it
        TrackBuilding building;                                         ///< The construction panel's ride and the piece it would build
        EnclosureTool enclosing;                                        ///< The enclosure list's fence tool out, or an animal
        PoolTool pooling;                                               ///< The pool list's tool out
        DeleteTools deleting;                                           ///< The delete menu's tool out
        LandTools land;                                                 ///< The land menu's shape and paint tools
        RidePlacing placing;                                            ///< The ride picked in a build list, while it is placed
        StructureTools structures;                                      ///< The scenery menu's tools that pick out and place structures
        LedSigns led_signs;                                             ///< The scrolling boards over queues and on park entrances
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
    [[nodiscard]] std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const ui::Palette& palette, const config::GameOptions& options, const std::filesystem::path& path, random::Seed seed, const simulation::RatingModel& ratings, const ParkLoadReport& report = { });

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

    /// Each kind's detail and draw distance, from the graphics options.
    [[nodiscard]] park::LevelScales level_scales(const config::GameOptions& options);

    /// The colour to clear to before drawing the park.
    [[nodiscard]] render::Color park_background(const ParkScene& scene);

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    /// The park as it stands, in our own format (see parkfile/saved_park.hpp): the original park it started
    /// from and the player's changes. Guests, staff and the simulation are not kept.
    [[nodiscard]] std::vector<std::byte> save_park(const ParkScene& scene);

    /// Whether a box of the park's interface has the keyboard, or selected text to copy, so keys are not the game's.
    [[nodiscard]] bool typing(const ParkScene& scene);

    /// Pulls attraction `ref` down: it goes from the park with its doors, track and fence, its guests walk off
    /// (out of its queue at the entrance, riders off at the exit), and what it is worth comes back. It keeps its
    /// place in the scene's rides or stalls, marked, and saving leaves it out.
    void demolish(ParkScene& scene, const ui::InterfaceAssets& assets, park::AttractionRef ref);

    /// Fires the member of staff shown as `shown` of shown_people: they leave the park at once, as in the game.
    void fire(ParkScene& scene, std::size_t shown);

    /// Puts an attraction in `state`, the running park with it: open lets guests on, testing runs it empty.
    void set_attraction_state(ParkScene& scene, save::AttractionCommon& attraction, save::AttractionState state);

    /// Why attraction `ref` cannot be put in `state`, as a string key: a ride by its track and doors, a stall when
    /// its menu is empty; nothing when it can.
    [[nodiscard]] std::optional<std::string> state_refusal(const ParkScene& scene, park::AttractionRef ref, save::AttractionState state);
    /// The states the open-or-close tab of `ref` offers, in the order its choices fan out.
    [[nodiscard]] std::vector<ui::StateChoice> state_choices(const ParkScene& scene, park::AttractionRef ref);
    /// Those choices as the tab fans them: each greyed, with why as its help, when `state_refusal` refuses it.
    [[nodiscard]] std::vector<ui::InfoChoice> state_fan(const ParkScene& scene, park::AttractionRef ref, const ui::StringTable& strings);

    /// Puts ride `ride` in `state` when its track and doors let it; otherwise leaves it and gives the string
    /// key of why not.
    [[nodiscard]] std::optional<std::string> change_ride_state(ParkScene& scene, std::size_t ride, save::AttractionState state);
    /// The same for stall `stall`, which cannot open with nothing on its menu.
    [[nodiscard]] std::optional<std::string> change_stall_state(ParkScene& scene, std::size_t stall, save::AttractionState state);

    /// Has the running show ride that `show` is for put it on from its next go.
    void stage_show(ParkScene& scene, const save::RideShow& show);

    /// Has the simulated flat ride `ride_index` (of scene.rides) seat its riders on the object that has the
    /// seats, as flat_ride_timings finds it. Nothing for other rides, or one the simulation does not run.
    void seat_flat_ride(render::Device& device, ParkScene& scene, std::size_t ride_index);

    /// Adds the cars of `count` rides from `from` (of scene.rides) to the scene, where the park keeps them; cars
    /// without a kept place are left out.
    void add_ride_cars(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::size_t from, std::size_t count);

    /// Has the scene's cars of ride `ride_index` (of scene.rides) be its trains' cars as they are now: those it had
    /// go and its trains' cars are added where they stand, boarded as board_tracked_ride boards them.
    void replace_ride_cars(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::size_t ride_index);

    /// Loads the models of `cars` the scene does not have yet, and their pictures.
    void load_car_models(ParkScene& scene, const config::GameInstall& install, const park::RideCars& cars);

    /// Has the simulated tracked ride `ride_index` (of scene.rides) carry riders in its scene cars, boarding its
    /// first train at the station. Nothing for a ride the simulation does not run.
    void board_tracked_ride(render::Device& device, ParkScene& scene, const config::GameInstall& install, std::size_t ride_index);

    /// Has object `k` (of scene.objects) move on its own when its model has bones and it is scenery of no attraction,
    /// as animatronics are.
    void animate_scenery(render::Device& device, ParkScene& scene, std::size_t k);

    void destroy_park_scene(render::Device& device, ParkScene& scene);

} // namespace rct3::scene
