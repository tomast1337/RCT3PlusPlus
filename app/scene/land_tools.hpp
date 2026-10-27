// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land menu's tools that work on the ground itself.
//
// Shape ground and paint ground open the land panel (ui/screens/land_panel.hpp) in the bottom-left corner.
// Shaping's tabs hold the tile tools and the smoothing, raising and lowering brushes.
//
// Tile tools: press on the ground and move the pointer up or down to move it, a height unit for each
// kSlopeDragStep moved (park/tile_shaping.hpp). The tools stand in pairs: the first of each works on the one
// tile under the pointer, where the pointer's place in it picks the whole tile, an edge or a corner; the second
// on the brush's square of tiles. The pairs, in order: snap to the heights of the tiles round, move freely,
// move in whole units. Only the tiles' own corners move, so cliffs open at their sides. A tile is only shaped
// on land the park owns outright, and only as far as what stands on it lets it (park/ground_holds.hpp); a tile
// refused keeps its corners for the rest of the stroke. Scenery standing on the ground moves with it. A lake on
// ground that moves, or beside ground that sinks under its level, goes, as does a waterfall beside it.
//
// Raising and lowering: press on the ground and move the pointer up to raise a ridge, mountain, mesa or hill
// there, or down to sink a canyon, trough or crater, a height unit for each kSlopeDragStep moved
// (park/ground_shapes.hpp). The shape is as wide as the brush's number, centred where the pointer pressed;
// a ridge runs across the view. Ground the park does not own outright or that what stands on it holds stays,
// as do cliff edges. Each unit of ground moved is paid for, once.
//
// Smoothing: holding the button works the ground under the brush a little more every moment
// (park/ground_smoothing.hpp), following the pointer. Its tools by their place in the tab: average, level to
// the height under the pointer as it moves, level to the height where the stroke began, to whole units, join
// the tiles' edges, and square them. The game's data also holds pictures for roughening and a second whole-unit
// level, but they are not usable (one picture is malformed, the other repeats a third), so those are left out.
// The ground moved is paid for as it goes.
//
// Paint ground: the panel's surface tab shows the land surfaces the game's terrain files list, each by its
// own picture. Pressing the ground and moving over it paints the tiles a round brush covers, as wide as the
// number on the panel's disc (park/ground_brush.hpp), with the surface chosen, on land the park owns
// outright, each tile paid for. The cliff tab paints the faces of the cliffs on the tiles under the brush
// the same way, with the cliff faces the terrain files list. Closing the panel puts the tool away.
//
// Water: a click on the ground pours a lake up to the height clicked, rounded up to a whole unit, so clicking
// higher up a bank fills it deeper; it spreads over every tile it can reach below that (park/water_fill.hpp)
// and takes in the lakes it meets. A click on a lake takes it away, with its waterfalls. Water only goes on land
// the park owns outright without paths on the ground, and never under a higher lake; a message says when it
// cannot. Water is free.
//
// Waterfalls: click one lake, then another; water pours from the higher over its nearest straight edge into the
// lower (park/waterfall_making.hpp), or a message says why it cannot. A right click on a waterfall deletes it;
// anywhere else it puts the tool away.
//
// While a tool is out, marks on the ground show what it would work on: the tiles under the brush, the tile
// being poured on, or the one tile, edge or corner a tile tool picks.
//
// Buying land: the park window's buy land and buy rights buttons open the panel with a tab of the two. Pressing
// the ground and moving over it buys the tiles on sale under the brush's square (park/land_buying.hpp), each
// paid for, and a message says when money ran short. The park's fence and where its staff may walk follow.
//
// Every other change is paid for. A right click puts either tool away.
#pragma once

#include "park/ground_holds.hpp"
#include "park/ground_shapes.hpp"
#include "park/ground_smoothing.hpp"
#include "park/ground_textures.hpp"
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

    struct ParkScene;

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
        std::optional<save::Tile> tile; ///< Being shaped, or last painted, while the button is held
        float press_y = 0;
        std::size_t tile_tool = 0;                        ///< The tile tool chosen, by its place in its tab
        park::TileStroke stroke;                          ///< Of a tile tool
        std::vector<glm::vec3> marked;                    ///< Where the marks on the ground were made, to make them again only on a change
        park::GroundHolds holds;                          ///< Gathered as a stroke starts
        std::unordered_set<std::uint32_t> refused;        ///< Tiles the stroke may not move, as GroundHolds keys them
        std::optional<std::array<save::Tile, 2>> stirred; ///< The first and last tiles the stroke moved
        std::vector<park::GroundSurface> surfaces;        ///< Read once, the first time the paint tool opens
        bool surfaces_read = false;
        std::size_t surface = 0;                 ///< Chosen, of `surfaces`
        std::vector<park::GroundSurface> cliffs; ///< The cliff faces, read with the surfaces
        std::optional<std::size_t> cliff;        ///< Chosen, of `cliffs`, in place of a surface
        int brush = 3;                           ///< Tiles across the brushes cover
        std::optional<park::GroundShape> shape;  ///< The raising or lowering brush chosen; none for the tile tool
        park::CornerHeights before;              ///< As a brush stroke found them
        glm::vec2 middle { 0 };                  ///< Of a brush stroke, in tile units
        glm::vec2 along { 1, 0 };                ///< Of a ridge: across the view as the stroke began
        float amount = 0;                        ///< Of the brush stroke so far, in height units
        float paid_for = 0;                      ///< The most ground the stroke has moved, already paid for
        std::optional<std::size_t> smoothing;    ///< The smoothing brush chosen, by its place in its tab
        float target = 0;                        ///< Of a levelling stroke
        float held = 0;                          ///< Seconds of smoothing not yet applied
        std::uint64_t passes = 0;                ///< Of smoothing, the seed of each pass
        std::optional<std::size_t> upper_lake;   ///< Of a waterfall: the lake picked first, of the park's water
        save::Tile upper_tile;                   ///< Where it was picked
        park::LandBuy buying = park::LandBuy::Land;
    };

    /// Picks `tool` from the land menu, or puts it away when it is already out.
    void toggle_land_tool(ParkScene& scene, LandTools::Tool tool, const ui::InterfaceAssets& assets);
    /// Takes out the tool that buys `what` of the land on sale, opening the panel on it.
    void buy_land_tool(ParkScene& scene, park::LandBuy what, const ui::InterfaceAssets& assets);
    void stop_land_tools(ParkScene& scene);
    [[nodiscard]] bool land_tool(const ParkScene& scene);

    /// A right click at `at` (of a view `size`) with the waterfall tool out: deletes the waterfall there; whether
    /// there was one.
    bool delete_waterfall(ParkScene& scene, ui::Point at, ui::Point size);

    /// Each frame: follows the pointer at `at` (of a view `size`, not `over` the interface) while the left
    /// button is `down`.
    void update_land_tools(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, float elapsed);

} // namespace rct3::scene
