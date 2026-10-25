// SPDX-License-Identifier: GPL-3.0-or-later
//
// The enclosure list's fence tools. With a fence picked, pressing the ground and dragging marks a rectangle of
// tiles; letting go makes it an enclosure fenced with that fence, or adds it to the enclosures it takes in or
// touches (park/enclosures.hpp). With the list's delete tool, letting go takes the tiles out of enclosures.
// Every new length of fence is paid for at the fence's price; an area with land the park does not own
// outright, or that the park cannot pay for, shows its marks red and is refused. A right click, or picking
// another tool, puts the tool away.
#pragma once

#include "park/build_catalogue.hpp"
#include "render/device.hpp"
#include "ui/interface_assets.hpp"
#include "ui/widget.hpp"

namespace rct3::scene
{

    struct ParkScene;

    /// Takes out the fence tool for `item` when it is an enclosure fence or the delete tool; false otherwise.
    bool start_enclosing(ParkScene& scene, const park::BuildItem& item);

    /// Puts the fence tool away.
    void stop_enclosing(ParkScene& scene);

    /// Marks the tiles under a drag at `at`, in a view of `size`, while `down`; works on them when it ends.
    void update_enclosing(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over);

    /// Builds the enclosures' fences again after they changed.
    /// shortcut: the old panels stay hidden among the objects; drop them when objects can be taken out.
    void enclosures_changed(ParkScene& scene, const ui::InterfaceAssets& assets);

} // namespace rct3::scene
