// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park played: its ground drawn in 3D under a camera the player moves, and the interface over it. For now
// it shows the ground, water, paths and queues with their supports and railings, and the placed objects of
// a park file, the land round it, the mountains on the horizon and the sky and, for a tutorial park, the
// window that leads through the tutorial. Leaving the tutorial asks first; the owner then goes back to the
// front end.
//
// The camera, as in the game's normal mode: the right button drags the ground along, the middle button (or
// both) turns and tilts, the wheel zooms, and the arrow keys and Page Up and Page Down do the same from the
// keyboard. Nothing moves the camera while the pointer is over a window.
//
// The scene borrows the interface's skins, strings and fonts from the front end, which must outlive it.
#pragma once

#include "config/game_options.hpp"
#include "park/camera.hpp"
#include "park/object_mesh.hpp"
#include "park/sky_mesh.hpp"
#include "park/terrain_mesh.hpp"
#include "random/generator.hpp"
#include "render/device.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park_state.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list.hpp"
#include "ui/interface_assets.hpp"
#include "ui/key_map.hpp"
#include "ui/screens/park_bar.hpp"
#include "ui/ui_textures.hpp"

#include <expected>
#include <filesystem>
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
        std::vector<ui::PlayCommand> commands; ///< The game's commands bound to the keys pressed
    };

    /// The object buffers are built again at most this often, in seconds, while the view moves.
    inline constexpr float kObjectRebuildInterval = 0.25f;

    /// Behind the ground when the sky cannot be read.
    inline constexpr render::Color kParkSky = render::hex("#8cb3d9");
    /// How far the sky is drawn from the eye: inside the camera's far limit.
    inline constexpr float kSkyRadius = park::kFar * 0.9f;

    struct ParkScene
    {
        save::TerrainGrid grid;
        std::uint64_t seed = 0;
        random::RandomState gameplay; ///< For the simulation; to be kept with a saved park
        park::ParkCamera camera;
        std::vector<park::GroundBatch> batches;
        render::BufferHandle ground_vertices;
        render::BufferHandle ground_indices;
        render::BufferHandle water;
        std::uint32_t water_count = 0;
        render::PipelineHandle ground_pipeline;
        render::PipelineHandle water_pipeline;
        ui::UiTextures ground_textures;
        render::TextureHandle blank; ///< For ground without a picture
        std::vector<save::PlacedObject> objects;
        park::ObjectModels models;
        park::ModelPictures pictures; ///< For painting the batches new levels bring
        park::Leans leans;            ///< Of the built pieces that follow a slope
        park::Fitted fitted;          ///< Of the built pieces placed off the tile grid
        std::vector<glm::vec3> anchors;
        park::ObjectLevels levels; ///< Those the object buffers hold
        float since_rebuild = 0;   ///< Seconds since the object buffers were last built
        std::vector<park::ObjectBatch> object_batches;
        render::BufferHandle object_vertices;
        render::BufferHandle object_indices;
        park::ObjectMesh billboards;                 ///< Built again each frame, as they turn to face the eye
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
        simulation::DayClock day;            ///< The park's time of day
        render::SceneLight light;            ///< What the day clock shows, for this frame
        // On the heap: windows keep a reference to the desktop, and the scene moves.
        std::unique_ptr<ui::Desktop> desktop = std::make_unique<ui::Desktop>();
        const ui::FontSet* current_fonts = nullptr;
        ui::DrawList list;
        std::optional<float> scale;
        ui::Point last_pointer;
        bool turning = false;
        bool dragging = false;
        bool leave = false;
        bool message_alert = false;
    };

    /// Loads the park file at `path`, its objects as the game options ask. The strings must outlive the scene.
    /// Its random numbers come from `seed`: the same park and seed give the same park.
    [[nodiscard]] std::expected<ParkScene, std::string> create_park_scene(render::Device& device, const config::GameInstall& install, const ui::StringTable& strings, const config::GameOptions& options, const std::filesystem::path& path, std::uint64_t seed);

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed);

    /// True once the player has confirmed leaving.
    [[nodiscard]] bool take_park_leave(ParkScene& scene);

    /// True once after new messages came into the park's message drawer.
    [[nodiscard]] bool take_park_alert(ParkScene& scene);

    /// The colour to clear to before drawing the park.
    [[nodiscard]] render::Color park_background(const ParkScene& scene);

    void draw_park_scene(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    void destroy_park_scene(render::Device& device, ParkScene& scene);

} // namespace rct3::scene
