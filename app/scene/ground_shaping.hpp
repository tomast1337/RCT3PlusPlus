// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shape ground tool of the land menu: press on a tile and move the pointer up or down to raise or lower
// it, a step for each kSlopeDragStep moved. A sloped tile is flattened to its top or bottom first. The tiles
// round it keep their far corners and slope to it. A tile is only shaped on land the park owns outright,
// without a path and with nothing solid standing over it, and each step is paid for. A right click puts the
// tool away.
#pragma once

#include "park/path_clearance.hpp"
#include "render/device.hpp"
#include "ui/widget.hpp"

#include <optional>

namespace rct3::ui
{
    struct InterfaceAssets;
}

namespace rct3::scene
{

    struct ParkScene;

    struct GroundShaping
    {
        bool on = false;
        std::optional<park::PathSpot> tile; ///< Being shaped, while the button is held
        float press_y = 0;
        int steps = 0; ///< Taken in this stroke, up positive
        park::SolidTiles solids;
    };

    void toggle_ground_shaping(ParkScene& scene);
    void stop_ground_shaping(ParkScene& scene);
    [[nodiscard]] bool ground_shaping(const ParkScene& scene);

    /// Each frame: follows the pointer at `at` (of a view `size`, not `over` the interface) while the left
    /// button is `down`.
    void update_ground_shaping(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over);

} // namespace rct3::scene
