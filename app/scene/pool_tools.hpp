// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pool tools (park/pools/pools.hpp), picked in the pool panel the rides menu's pools button opens, as it remembers
// them; scripts may still pick them from the build items. With water or tiles picked, pressing the ground and dragging marks a
// rectangle of squares; letting go lays them, joining any pool they touch, and lays over the squares already
// there. The pool's surface is the ground's height where the drag began, or the surface of the pool begun
// there, or the height the panel sets when it does not snap to the ground; the ground under every square is
// dug down to the pool's floor. A spa pool or a high diving board goes on the one square pressed; dragging up or
// down from it sets the board's height, and the turn keys turn it to face the water, or it is refused. With the delete tool, letting go takes
// the squares up and refunds them, leaving the ground flat at their surface. An area with land the park does
// not own outright, or that the park cannot pay for, shows its marks red and is refused. With an extra or a
// changing room picked, it follows the pointer as a see-through ghost on the square under it, red where it does
// not fit (park/pools/pools.hpp) or cannot be paid for, and shows alone, turning, in the panel's disc; the turn keys
// turn it a quarter. A click puts an extra in there. A changing room must open on a paved square of a pool; a
// click builds it there once: the tool and the panel go, and the room's window opens. With any of the tools out,
// a right click takes up the square pointed at as the delete tool does, or deletes what else is there as the
// delete menu's tool does; the tool stays out. Picking another tool puts it away.
#pragma once

#include "park/build_catalogue.hpp"
#include "park/pools/pools.hpp"
#include "park/terrain_mesh.hpp"
#include "render/device.hpp"
#include "ui/interface_assets.hpp"
#include "ui/key_map.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/pool_panel.hpp"
#include "ui/widget.hpp"

#include <optional>
#include <span>

namespace rct3::scene
{

    struct ParkScene;

    /// Takes out the pool tool for `item` when it is one; false otherwise.
    bool start_pooling(ParkScene& scene, const rct3::park::BuildItem& item);

    /// Puts the pool tool away, and the pool panel.
    void stop_pooling(ParkScene& scene);

    /// Opens the pool panel (ui/screens/pool_panel.hpp) with the editor's tool out, or closes it; with `page`, opens it
    /// on that page whether it shows or not.
    void toggle_pool_panel(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const ui::Currency& currency, std::optional<ui::screens::PoolPage> page = std::nullopt);

    /// Swaps the pool tool's work shown, not yet kept, with the pools and the ground as they were before it;
    /// twice swaps it back.
    void swap_pool_work(ParkScene& scene);

    /// Marks the squares under a drag at `at`, in a view of `size`, while `down`; lays them when it ends.
    void update_pooling(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over, bool shift, std::span<const ui::PlayCommand> commands);

    /// Takes up the pool complex the changing room `room` (of the pools' rooms) opens on, its squares, extras
    /// and rooms, refunded as the delete tool refunds them.
    void delete_pool_complex(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t room);

    /// A right click at `at`, in a view of `size`, with a pool tool out: takes up the top pool square pointed at,
    /// with what stands on it, refunded as the delete tool refunds it. False when no square is there.
    bool delete_pool_square(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size);

    /// Draws the extra or changing room picked, see-through where it would go.
    void draw_pool_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    /// Builds the pools' pieces and water again after they changed, on the tiles of `touched` or anywhere.
    /// shortcut: the old pieces stay hidden among the objects; drop them when objects can be taken out.
    /// Where the rides' pieces and slide stations join the pools: railings are left open there and paths join them.
    [[nodiscard]] std::vector<rct3::park::pools::PoolOpening> pool_openings(const ParkScene& scene);

    void pools_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<rct3::park::TileRect> touched = std::nullopt);

} // namespace rct3::scene
