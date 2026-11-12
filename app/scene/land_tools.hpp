// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land menu's tools that work on the ground itself.
//
// Shape ground and paint ground open the land panel (ui/screens/land_panel.hpp) in the bottom-left corner.
// Shaping's tabs hold the tile tools and the smoothing, raising and lowering brushes.
//
// Tile tools: press on the ground and move the pointer up or down to move it: the ground follows the pointer, up
// and down an upright plane through the point pressed, so it stays under it at any distance;
// seen from steeply overhead it moves less than the pointer (park/tile_shaping.hpp). The tools stand in pairs: the first of each works on the one
// tile under the pointer, where the pointer's place in it picks the whole tile, an edge or a corner; the second
// on a square of tiles as wide as the brush. The pairs, in order: snap to the heights of the tiles round, move freely,
// move in whole units. Only the tiles' own corners move, so cliffs open at their sides. A tile is only shaped
// on land the park owns outright, and only as far as what stands on it lets it (park/ground/holds.hpp); a tile
// refused keeps its corners for the rest of the stroke. Scenery standing on the ground moves with it. A lake on
// ground that moves settles on it (see Water below); a waterfall beside it goes.
//
// Raising and lowering, one tab. Dragged: press on the ground and move the pointer up to raise a ridge, mountain, mesa or
// hill there, or down to sink a canyon, trough or crater: once past a short dead zone the ground moves evenly
// with the pointer (park/ground/shapes.hpp). Moved the other way, each makes its shape upside down: a mountain dragged down digs
// a pit, a crater dragged up builds a mound. The shape reaches as far as the brush's radius from where the pointer pressed;
// a ridge runs across the view. Ground the park does not own outright or that what stands on it holds stays;
// a cliff under the brush moves with the ground and keeps its height. Each unit a tile's corner moves is paid for, once.
// Sprayed, a raising brush only raises and a lowering one only lowers, under the pointer as it moves.
//
// The mode tab sets how the raising, lowering and smoothing brushes are worked, for all of them at once:
//   - Drag (as the panel opens): press and move the pointer up or down; the ground under the brush goes with
//     it, both ways, and comes back when the pointer does. Good for fine work.
//   - Spray: hold the button and the ground under the pointer goes one way, a little every moment, at the
//     spray speed set by the tab's number (1 to 9); moving the pointer while holding leaves a trail. Good for
//     big changes.
//
// Smoothing (park/ground/smoothing.hpp) is strongest at the brush's middle and falls off to its edge. Sprayed,
// it works the ground under the pointer for as long as the button is held; dragged, it works the ground round
// where the pointer pressed further the further the pointer is dragged, either way. Its tools by their place in the tab: average, level to
// the height under the pointer as it moves, level to the height where the stroke began, level to that height's
// whole unit (a flat pad for rides and scenery), join the tiles' edges, and square them. The game's data also holds pictures for roughening and a second whole-unit
// level, but they are not usable (one picture is malformed, the other repeats a third), so those are left out.
// The ground moved is paid for as it goes.
//
// Paint ground: the panel's surface tab shows the land surfaces the game's terrain files list, each by its
// own picture. Pressing the ground and moving over it paints the tile under the pointer and those with
// a corner within the brush's radius (park/ground/brush.hpp), with the surface chosen, on land the park owns
// outright, each tile paid for. The cliff tab paints the faces of the cliffs on the tiles under the brush
// the same way, with the cliff faces the terrain files list. Closing the panel puts the tool away.
//
// Water: a click on the ground pours a lake up to the height clicked, rounded up to a whole unit, so clicking
// higher up a bank fills it deeper; it spreads over every tile it can reach below that (park/water/fill.hpp)
// and takes in the lakes it meets. A click on a lake takes it away, with its waterfalls. Shaping the ground
// under or beside a lake leaves it what the new ground holds: risen tiles dry, dug ones flood, a bank cut open
// lowers it a whole unit at a time, and it goes only when no level holds. Water only goes on land
// the park owns outright without paths on the ground, and never under a higher lake; a message says when it
// cannot. Water is free.
//
// Waterfalls: click one lake, then another; water pours from the higher over its nearest straight edge into the
// lower (park/waterfall_making.hpp), or a message says why it cannot. A right click on a waterfall deletes it;
// anywhere else it puts the tool away.
//
// While a tool is out, marks on the ground show what it would work on: a ring as wide as the brush (and the tiles
// it would paint), the tile being poured on, the tiles to buy, or the one tile, edge or corner a tile tool
// picks. The brush is a radius in world units round the point under the pointer, 1 to 64, set by the number
// on the panel's disc and shared by shaping, smoothing and painting.
//
// Buying land: the park window's buy land and buy rights buttons open the panel with a tab of the two. Pressing
// the ground and moving over it buys the tiles on sale under the brush's square (park/land_buying.hpp), each
// paid for, and a message says when money ran short. The park's fence and where its staff may walk follow.
//
// Every other change is paid for. A right click or Escape puts either tool away.
//
// A shaping stroke rumbles from press to release, softly while the ground stays still and loudest as it moves.
#pragma once

#include "park/camera.hpp"
#include "park/ground/brush.hpp"
#include "park/ground/holds.hpp"
#include "park/ground/shapes.hpp"
#include "park/ground/smoothing.hpp"
#include "park/ground/textures.hpp"
#include "park/land_buying.hpp"
#include "park/tile_shaping.hpp"
#include "render/device.hpp"
#include "ui/widget.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace rct3::ui
{
    struct InterfaceAssets;
}

namespace rct3::scene
{

    namespace park
    {
        struct ParkScene;
    }

    struct LandTools
    {
        enum class Tool : std::uint8_t
        {
            None,
            Shape,
            Paint,
            Water,
            Waterfall,
            Buy,
        };
        Tool tool = Tool::None;
        std::optional<save::model::Tile> tile;                   ///< Being shaped, or last painted, while the button is held
        float press_y = 0;                                       ///< Of a brush stroke, on the view
        glm::vec3 press_at { 0 };                                ///< Of a tile tool's stroke: the point of the ground pressed on
        rct3::park::ParkCamera press_camera;                     ///< The view as the stroke began
        float press_height = 0;                                  ///< What the pointer read there as the stroke began
        std::size_t tile_tool = 0;                               ///< The tile tool chosen, by its place in its tab
        rct3::park::TileStroke stroke;                           ///< Of a tile tool
        std::vector<glm::vec3> marked;                           ///< Where the marks on the ground were made, to make them again only on a change
        rct3::park::ground::GroundHolds holds;                   ///< Gathered as a stroke starts
        std::unordered_set<std::uint32_t> refused;               ///< Tiles the stroke may not move, as GroundHolds keys them
        std::optional<std::array<save::model::Tile, 2>> stirred; ///< The first and last tiles the stroke moved
        std::vector<rct3::park::ground::GroundSurface> surfaces; ///< Read once, the first time the paint tool opens
        bool surfaces_read = false;
        std::size_t surface = 0;                               ///< Chosen, of `surfaces`
        std::vector<rct3::park::ground::GroundSurface> cliffs; ///< The cliff faces, read with the surfaces
        std::optional<std::size_t> cliff;                      ///< Chosen, of `cliffs`, in place of a surface
        int brush = rct3::park::ground::kDefaultBrush;         ///< The brushes' radius, in world units
        std::optional<rct3::park::ground::GroundShape> shape;  ///< The raising or lowering brush chosen; none for the tile tool
        rct3::park::ground::GroundBefore before;               ///< The ground as a brush stroke found it
        glm::vec2 middle { 0 };                                ///< Of a brush stroke, in tile units
        glm::vec2 along { 1, 0 };                              ///< Of a ridge: across the view as the stroke began
        float amount = 0;                                      ///< Of the brush stroke so far, in height units
        /// The ground, lakes and waterfalls as the stroke found them. The water settles from these onto the ground
        /// as it is now, so a lake a drag lowered or dried comes back when the drag does, within the stroke.
        /// Later: with the ground now, all an undo of the stroke needs.
        struct Start
        {
            save::model::TerrainGrid grid;
            save::model::WaterLayer water;
            std::vector<save::model::Waterfall> waterfalls;
        };
        std::optional<Start> start;
        std::optional<glm::vec3> cost_at; ///< Where the stroke's cost is shown, once it has paid
        float paid_for = 0;               ///< The most ground the stroke has moved, already paid for
        bool spray = false;               ///< The brushes spray while held, rather than follow a drag
        int spray_speed = rct3::park::ground::kDefaultSpray;
        std::optional<std::size_t> smoothing;  ///< The smoothing brush chosen, by its place in its tab
        float target = 0;                      ///< Of a levelling stroke
        std::uint64_t passes = 0;              ///< Of smoothing, the seed of each pass
        std::optional<std::size_t> upper_lake; ///< Of a waterfall: the lake picked first, of the park's water
        save::model::Tile upper_tile;          ///< Where it was picked
        rct3::park::LandBuy buying = rct3::park::LandBuy::Land;
        std::optional<float> rumble; ///< While a shaping stroke goes: how loud its ground is heard moving, 0..1
    };

    /// Picks `tool` from the land menu, or puts it away when it is already out.
    void toggle_land_tool(park::ParkScene& scene, LandTools::Tool tool, const ui::InterfaceAssets& assets);
    /// Takes out the tool that buys `what` of the land on sale, opening the panel on it.
    void buy_land_tool(park::ParkScene& scene, rct3::park::LandBuy what, const ui::InterfaceAssets& assets);
    void stop_land_tools(park::ParkScene& scene);
    [[nodiscard]] bool land_tool(const park::ParkScene& scene);

    /// A right click at `at` (of a view `size`) with the waterfall tool out: deletes the waterfall there; whether
    /// there was one.
    bool delete_waterfall(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size);

    /// Each frame: follows the pointer at `at` (of a view `size`, not `over` the interface) while the left
    /// button is `down`.
    void update_land_tools(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, float elapsed);

} // namespace rct3::scene
