// SPDX-License-Identifier: GPL-3.0-or-later
//
// The delete menu's tools and what they pull down: props and paths at once, with what pulling them down costs
// or pays back floating up, and rides and stalls after the player says yes (park/questions.hpp). A prop's own
// panel and a ride's demolish button pull down the same way.
#pragma once

#include "park/attraction_objects.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "ui/interface_assets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::scene::park
{

    struct ParkScene;

    inline constexpr std::string_view kDeleteAreaName = "delete_area";

    /// The delete menu's tools: one deletes what a click lands on, the other what stands on the tiles dragged over.
    struct DeleteTools
    {
        enum class Tool : std::uint8_t
        {
            None,
            One,
            Area,
        };
        Tool tool = Tool::None;
        std::optional<save::model::Tile> corner; ///< Of the area marked, where the drag began
        std::optional<save::model::Tile> other;  ///< And the opposite corner, where it is now or ended
        bool dragging = false;
        std::optional<std::array<std::int32_t, 4>> drawn; ///< Columns and rows the marks show now
        std::vector<bool> kept { };                       ///< Tiles the drag may not take in or out, row by row; filled as it begins
        /// What letting go would do: the enclosures it would leave, and what that costs (below 0 when the fence
        /// taken down pays back more than any put up), for the columns and rows marked.
        struct Marked
        {
            std::array<std::int32_t, 4> area { };
            save::model::EnclosureMap enclosures { };
            std::int64_t cost = 0;
        };
        std::optional<Marked> marked { };
    };

    /// Puts the delete menu's tool away.
    void stop_deleting(ParkScene& scene);

    /// Pulls props `indices` of the objects down with the delete tools or a prop's panel, paying what their kinds
    /// say pulling one down costs (or gets back), floated up even when nothing unless not `floated`. The park
    /// entrance stays, and nothing goes when the park cannot pay; either is told in a box. What was paid, when
    /// any went.
    std::optional<std::int64_t> delete_props(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::size_t> indices, bool floated = true);

    /// Takes paths `ids` away with the delete tools, and the bins, benches, signs and the like on their tiles, the
    /// money for both floating up as one sum.
    void delete_paths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::uint64_t> ids);

    /// Pulls attraction `ref` down: it goes from the park with its doors, track and fence, its guests walk off
    /// (out of its queue at the entrance, riders off at the exit), and what it is worth comes back. It keeps its
    /// place in the scene's rides or stalls, marked, and saving leaves it out.
    void demolish(ParkScene& scene, const ui::InterfaceAssets& assets, rct3::park::AttractionRef ref);

    /// Deletes what stands on the tiles from `a` to `b`: props and paths at once, and asks once about every
    /// ride and stall with a part there.
    void delete_area(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Tile a, save::model::Tile b);

} // namespace rct3::scene::park
