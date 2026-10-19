// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land menu's tools that work on the ground itself.
//
// Shape ground: press on a tile and move the pointer up or down to raise or lower it, a step for each
// kSlopeDragStep moved. A sloped tile is flattened to its top or bottom first. The tiles round it keep their
// far corners and slope to it. A tile is only shaped on land the park owns outright, without a path and with
// nothing solid standing over it.
//
// Paint ground: a window shows the land surfaces the game's terrain files list, each by its own picture; the
// one chosen is pressed in. Under them, the brush: how many tiles across, and whether round or square
// (park/ground_brush.hpp). Pressing the ground and moving over it paints the tiles the brush covers with that
// surface, on land the park owns outright, each tile paid for. Closing the window puts the tool away.
//
// Water: press on a tile and move the pointer up or down to raise or lower the water there, a step for each
// kSlopeDragStep moved. Starting on a lake moves its level; starting on dry ground pours a new one from the
// tile's lowest corner. The water spreads over every tile it can reach below its level (park/water_fill.hpp)
// and takes in the lakes it meets. It only spreads over land the park owns outright without paths on the
// ground; where it would go further it stops rising.
//
// Every change is paid for. A right click puts either tool away.
#pragma once

#include "park/ground_textures.hpp"
#include "park/path_clearance.hpp"
#include "render/device.hpp"
#include "ui/widget.hpp"

#include <cstdint>
#include <optional>
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
        };
        Tool tool = Tool::None;
        std::optional<park::PathSpot> tile; ///< Being shaped, or last painted, while the button is held
        float press_y = 0;
        int steps = 0; ///< Taken in this stroke, up positive
        park::SolidTiles solids;
        save::WaterLayer water_before;             ///< As the water stroke found it
        float level_before = 0;                    ///< Of the water on the pressed tile, as the stroke found it
        std::vector<park::GroundSurface> surfaces; ///< Read once, the first time the paint tool opens
        bool surfaces_read = false;
        std::size_t surface = 0; ///< Chosen, of `surfaces`
        int brush = 1;           ///< Tiles across the paint brush covers
        bool round = false;      ///< The brush is round, not square
    };

    /// Picks `tool` from the land menu, or puts it away when it is already out.
    void toggle_land_tool(ParkScene& scene, LandTools::Tool tool, const ui::InterfaceAssets& assets);
    void stop_land_tools(ParkScene& scene);
    [[nodiscard]] bool land_tool(const ParkScene& scene);

    /// Each frame: follows the pointer at `at` (of a view `size`, not `over` the interface) while the left
    /// button is `down`.
    void update_land_tools(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over);

} // namespace rct3::scene
