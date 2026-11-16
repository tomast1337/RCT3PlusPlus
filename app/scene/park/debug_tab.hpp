// SPDX-License-Identifier: GPL-3.0-or-later
//
// The developer windows' Debug tab: one-click tools that put the park in a state at once (money, land,
// research, guests, rides, animals, litter, water), switches for the weather, the time of day and what is drawn,
// and a person's motions stepped through one by one. Scripts use the same tools by name.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rct3::render
{
    class Device;
}
namespace rct3::ui
{
    struct InterfaceAssets;
}

namespace rct3::scene::park
{

    struct ParkScene;

    enum class DebugTool : std::uint8_t
    {
        AddMoney,
        BuyAllLand,
        FinishResearch,
        AddGuests,
        RemoveGuests,
        RefreshGuests,
        DropLitter,
        ClearLitter,
    };

    /// Each tool by the name scripts use for it, in DebugTool's order.
    inline constexpr std::array<std::string_view, 8> kDebugToolNames {
        "add_money", "buy_all_land", "finish_research", "add_guests", "remove_guests", "refresh_guests", "drop_litter", "clear_litter",
    };

    /// Uses `tool` on the park; `amount` is the money to add, in hundredths, or how many guests or pieces of
    /// litter. Returns how many things it changed: tiles, inventions, guests, rides, animals, pieces, falls or
    /// jets, or 1 for money added.
    std::size_t use_debug_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, std::int64_t amount);

    /// The tab's contents, drawn into the developer window open now.
    void draw_debug_tab(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

} // namespace rct3::scene::park
