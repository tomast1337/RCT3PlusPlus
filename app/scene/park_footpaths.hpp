// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's paths as the player lays them: the path panel (ui/screens/footpath_panel.hpp) opened from the
// tool row, the styles it offers, and the paths laid with it. Laying changes the park's paths in memory and
// builds the ground again so they show at once; the piece the controls would lay next is drawn faintly over its
// tile, tinted when it cannot be laid there. A raised piece adds the object that draws it and the supports
// under it to the park's objects, and the raised pieces around it take the pieces their new neighbour calls for.
// Queue tiles get railings on their closed sides, and a queue tile laid beside a ride's entrance joins the
// station's line (park/queue_lines.hpp).
//
// With a path style chosen, the path a click would lay shows under the pointer; dragging over bare ground paints paths on the tiles the pointer passes over, and a
// click lays one (park/path_drag.hpp); they show faintly while the button is held, are laid when it is let go,
// and the right button calls the stroke off. Taking a piece away right after a stroke takes the whole stroke
// away. Pointing at a path shows an arrow on its edge nearest the pointer and the piece that would go on from
// it; an arrow also shows over the trail's next piece. Pressing on either and moving up or down picks the
// slope (ramps, then stairs), and letting go lays the piece, which the trail then goes on from. A ride's
// entrance or exit pointed at, at its own height even off the ground, offers the piece going on from it the same way.
//
// Holding Shift starts a trail on the tile pointed at (or picks up the trail's next piece) and raises or sinks
// its first piece with the pointer, as for scenery; below the ground it is a tunnel piece. A press while Shift
// is held lays it; let go, the height stays for the arrow. With Control held, a trail started on bare ground,
// or raised with Shift, starts at the height the last piece laid here ended at.
//
// With the panel's route tab on and a trail started, pointing at a tile shows the cheapest route from the
// arrow to it (park/path_route.hpp) with its cost, and a click lays it piece by piece; taking a piece away
// right after takes the whole route away. With cutting on, the route may cut through hills.
//
// The styles and their pictures are read from the install the first time the panel opens.
#pragma once

#include "park/footpath_builder.hpp"
#include "park/ground_paths.hpp"
#include "park/ground_queues.hpp"
#include "park/object_mesh.hpp"
#include "park/path_clearance.hpp"
#include "park/path_drag.hpp"
#include "park/path_route.hpp"
#include "park/path_styles.hpp"
#include "park/support_sets.hpp"
#include "park/terrain_mesh.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "scene/track_building.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/footpath_panel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rct3::config
{
    class GameOptions;
}

namespace rct3::scene
{

    struct ParkScene;

    /// The widget's name on the park's desktop.
    inline constexpr std::string_view kFootpathPanelName = "footpaths";

    struct ParkFootpaths
    {
        std::vector<park::PathStyle> styles; ///< Path styles, then queue styles; read when the panel first opens
        bool loaded = false;
        std::optional<std::size_t> style; ///< Of styles, the one chosen
        park::FootpathBuilder builder;
        std::vector<save::PathTile> paths;   ///< The park's paths as they are now
        save::WaterLayer water;              ///< To build the ground again with
        std::vector<save::PoolWater> pools;  ///< The same
        std::vector<park::RideFloor> floors; ///< The rides' floors, paved with the ground's paths
        bool floors_stale = false;           ///< The ground was built without the floors as they are now
        /// The path styles whose ground pictures the scene has, by the index the ground's batches use.
        std::vector<std::string> pictured;
        std::vector<park::QueuePainting> queues_pictured;       ///< The same for the ground queues' pictures
        park::TerrainMesh ghost;                                ///< The next piece, drawn over the ground
        park::ObjectMesh raised_ghost;                          ///< The next piece when it is raised
        std::map<std::string, TrackModels, std::less<>> models; ///< Of the raised pieces and supports laid
        std::optional<park::SupportSets> support_sets;          ///< Read when the first raised piece is laid
        /// Of the raised paths laid here, the supports holding each up.
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> supports;
        /// Of the queue tiles laid here, the railings along them.
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> railings;
        park::TerrainMesh arrow;                              ///< Over the next piece, pointing the way it goes
        std::optional<float> slope_press;                     ///< While dragging on the arrow: the pointer's height when pressed
        park::PathSlope slope_before = park::PathSlope::Flat; ///< And the slope then, to step from
        bool lay_next = false;                                ///< Letting go on the arrow lays the next piece
        bool under_ground = false;                            ///< The ghost goes into a tunnel, so the ground is drawn faded
        bool took_click = false;                              ///< That press was not a click picking a start
        std::vector<park::PathSpot> brush;                    ///< While a stroke goes on: the tiles it passed over, in order
        /// The path edge the pointer is at, as the builder going on from it, and which path and side that is.
        std::optional<park::FootpathBuilder> hover;
        std::optional<std::pair<std::size_t, std::uint8_t>> hover_edge;
        std::optional<park::PathSpot> hover_tile;              ///< Bare ground pointed at, shown as the path a click would lay
        std::optional<std::array<std::int32_t, 4>> hover_door; ///< The door edge pointed at: column, row, height, side
        /// While Shift is held: where the pointer met the ground when it went down, and the first piece's height then.
        struct Lift
        {
            glm::vec3 at { 0.0f };
            std::int32_t height = 0;
        };
        std::optional<Lift> lift;
        std::optional<std::int32_t> last_height;           ///< Where the last piece laid here ended, for Control
        bool drop = false;                                 ///< The stroke ended, so its paths are laid
        std::optional<park::DragPlan> plan;                ///< What the stroke would lay, worked out when it changes
        std::vector<std::uint64_t> stroke;                 ///< The paths the last stroke laid, until another piece is laid
        bool routing = false;                              ///< The panel's route tab was on last frame
        std::optional<park::PathSpot> route_goal;          ///< With routing on: the tile pointed at
        std::optional<std::int32_t> route_goal_height;     ///< And the height of the raised path pointed at there
        std::optional<std::vector<park::RouteStep>> route; ///< The route there, worked out when it changes
        bool route_blocked = false;                        ///< Only through what is in the way, so it is shown refused
        park::SolidTiles solids;                           ///< What stands in the paths' way
        std::size_t solids_of = 0;                         ///< The scene's object count when worked out
        bool route_press = false;                          ///< The button went down to lay the route
        bool lay_route = false;                            ///< And came up, so the route is laid
        std::size_t route_laid = 0;                        ///< Pieces the last route laid, until another piece is laid
        std::optional<std::uint64_t> sell;                 ///< A right click picked this path to sell
        std::optional<park::PathSpot> followed;            ///< With camera follow on: the next piece's tile the camera went to
        std::optional<glm::vec3> camera_goal;              ///< Where the camera glides to, until it is there or moved
        bool dirty = false;                                ///< The next piece changed, so the ghost is built again
        std::u16string notice;                             ///< Why the last piece was not laid, until the next control
    };

    /// Opens the path panel, or closes it when it is open.
    void toggle_footpath_panel(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options);

    /// Closes the path panel if it is open.
    void close_footpath_panel(ParkScene& scene);

    /// Builds the ground again from the scene's grid, with the paths and the rides' floors as they are now.
    void rebuild_ground(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    /// Works out the rides' floors again from the scene's rides and the models of what they are placed as, and
    /// marks the ground for building again.
    void refresh_ride_floors(ParkScene& scene);

    /// The supports' pieces, read when first needed; none when the install has none.
    const park::SupportSets* support_sets(ParkScene& scene, const ui::InterfaceAssets& assets);

    /// Acts on what the player did in the panel since the last frame, and shows the result.
    void update_footpaths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const config::GameOptions& options);

    /// Whether a click on the park at `at` (interface units, on a view `size` across) went to the path panel:
    /// while it shows its controls, a click on the ground picks where the path starts.
    /// `control`: Control is held, so the trail starts at the last piece's height.
    bool footpath_click(ParkScene& scene, ui::Point at, ui::Point size, bool control);

    /// With the panel's camera follow on, glides the camera over `elapsed` seconds towards the next piece each
    /// time the trail goes on; the player moving the camera stops it until the next piece.
    void follow_footpaths(ParkScene& scene, float elapsed);

    /// A right click while the path panel shows its controls sells the path under the pointer, as in the game.
    /// Whether it picked one.
    bool footpath_sell(ParkScene& scene, ui::Point at, ui::Point size);

    /// Follows the pointer over the park at `at` (`over` the interface or not): strokes while the left button is
    /// `down`, slope drags on an arrow, and the path edge or door pointed at; `cancel` calls a stroke or drag off.
    /// `shift` and `control`: those keys are held, which raise and sink the trail's first piece.
    void footpath_drag(ParkScene& scene, ui::Point at, ui::Point size, bool down, bool over, bool cancel, bool shift, bool control);

    /// Draws the next piece over the ground.
    /// Whether the ground is drawn see-through, switched in the view menu or while building into a tunnel. Then
    /// the pointer picks paths through it, as the game's pick does when its ground is see-through.
    [[nodiscard]] bool ground_faded(const ParkScene& scene);

    void draw_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    /// Draws the next piece see-through when it is raised.
    void draw_raised_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene
