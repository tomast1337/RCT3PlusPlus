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
// slope (ramps, then stairs), and letting go lays the piece, which the trail then goes on from.
//
// With the panel's route tab on and a trail started, pointing at a tile shows the cheapest route from the
// arrow to it (park/path_route.hpp) with its cost, and a click lays it piece by piece; taking a piece away
// right after takes the whole route away. With cutting on, the route may cut through hills.
//
// The styles and their pictures are read from the install the first time the panel opens.
#pragma once

#include "park/footpath_builder.hpp"
#include "park/ground_queues.hpp"
#include "park/object_mesh.hpp"
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
        std::vector<save::PathTile> paths; ///< The park's paths as they are now
        save::WaterLayer water;            ///< To build the ground again with
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
        std::optional<park::PathSpot> hover_tile;          ///< Bare ground pointed at, shown as the path a click would lay
        bool drop = false;                                 ///< The stroke ended, so its paths are laid
        std::optional<park::DragPlan> plan;                ///< What the stroke would lay, worked out when it changes
        std::vector<std::uint64_t> stroke;                 ///< The paths the last stroke laid, until another piece is laid
        bool routing = false;                              ///< The panel's route tab was on last frame
        std::optional<park::PathSpot> route_goal;          ///< With routing on: the tile pointed at
        std::optional<std::int32_t> route_goal_height;     ///< And the height of the raised path pointed at there
        std::optional<std::vector<park::RouteStep>> route; ///< The route there, worked out when it changes
        bool route_press = false;                          ///< The button went down to lay the route
        bool lay_route = false;                            ///< And came up, so the route is laid
        std::size_t route_laid = 0;                        ///< Pieces the last route laid, until another piece is laid
        std::optional<std::uint64_t> sell;                 ///< A right click picked this path to sell
        bool dirty = false;                                ///< The next piece changed, so the ghost is built again
        std::u16string notice;                             ///< Why the last piece was not laid, until the next control
    };

    /// Opens the path panel, or closes it when it is open.
    void toggle_footpath_panel(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options);

    /// Closes the path panel if it is open.
    void close_footpath_panel(ParkScene& scene);

    /// Acts on what the player did in the panel since the last frame, and shows the result.
    void update_footpaths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const config::GameOptions& options);

    /// Whether a click on the park at `at` (interface units, on a view `size` across) went to the path panel:
    /// while it shows its controls, a click on the ground picks where the path starts.
    bool footpath_click(ParkScene& scene, ui::Point at, ui::Point size);

    /// A right click while the path panel shows its controls sells the path under the pointer, as in the game.
    /// Whether it picked one.
    bool footpath_sell(ParkScene& scene, ui::Point at, ui::Point size);

    /// Follows the pointer over the park at `at` (`over` the interface or not): strokes while the left button is
    /// `down`, slope drags on an arrow, and the path edge pointed at; `cancel` calls a stroke or drag off.
    void footpath_drag(ParkScene& scene, ui::Point at, ui::Point size, bool down, bool over, bool cancel);

    /// Draws the next piece over the ground.
    /// Whether the ground is drawn see-through, switched in the view menu or while building into a tunnel. Then
    /// the pointer picks paths through it, as the game's pick does when its ground is see-through.
    [[nodiscard]] bool ground_faded(const ParkScene& scene);

    void draw_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    /// Draws the next piece see-through when it is raised.
    void draw_raised_footpath_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene
