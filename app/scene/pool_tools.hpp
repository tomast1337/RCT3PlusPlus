// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pool list's tools (park/pools.hpp). With water or tiles picked, pressing the ground and dragging marks a
// rectangle of squares; letting go lays them, joining any pool they touch, and lays over the squares already
// there. The pool's surface is the ground's height where the drag began, or the surface of the pool begun
// there; the ground under every square is dug down to the pool's floor. With the delete tool, letting go takes
// the squares up and refunds them, leaving the ground flat at their surface. An area with land the park does
// not own outright, or that the park cannot pay for, shows its marks red and is refused. A right click, or
// picking another tool, puts the tool away.
#pragma once

#include "park/build_catalogue.hpp"
#include "render/device.hpp"
#include "ui/interface_assets.hpp"
#include "ui/widget.hpp"

namespace rct3::scene
{

    struct ParkScene;

    /// Takes out the pool tool for `item` when it is one; false otherwise.
    bool start_pooling(ParkScene& scene, const park::BuildItem& item);

    /// Puts the pool tool away.
    void stop_pooling(ParkScene& scene);

    /// Marks the squares under a drag at `at`, in a view of `size`, while `down`; lays them when it ends.
    void update_pooling(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over);

    /// Builds the pools' pieces and water again after they changed.
    /// shortcut: the old pieces stay hidden among the objects; drop them when objects can be taken out.
    void pools_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

} // namespace rct3::scene
