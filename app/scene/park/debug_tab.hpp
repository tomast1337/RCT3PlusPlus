// SPDX-License-Identifier: GPL-3.0-or-later
//
// The developer windows' Debug tab: one-click tools that put the park in a state at once (money, land,
// research, saved designs, career scenarios, guests, stalls, rides, animals, litter, water, firework displays), switches for the weather, the time of day and what is drawn,
// and the people's motions stepped through one by one or played in turn, slowed down, or their parties standing together often. Scripts use the same tools by name.
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
        BreakRides,
        ResetRides,
        RemoveAnimals,
        RefreshAnimals,
        DropLitter,
        ClearLitter,
        RebuildWaterfalls,
        ResetJets,
        UnlockDesigns,
        UnlockScenarios,
        ResetItemNames,
        SlowGuests,
        PartiesHuddleOften,
        CycleMotions,
        CyclePersonMotions,
        StartDisplay,
        StopDisplay,
    };

    /// Each tool by the name scripts use for it, in DebugTool's order.
    inline constexpr std::array<std::string_view, 23> kDebugToolNames {
        "add_money", "buy_all_land", "finish_research", "add_guests", "remove_guests", "refresh_guests", "break_rides", "reset_rides", "remove_animals", "refresh_animals", "drop_litter", "clear_litter", "rebuild_waterfalls", "reset_jets", "unlock_designs", "unlock_scenarios", "reset_item_names", "slow_guests", "parties_huddle_often", "cycle_motions", "cycle_person_motions", "start_display", "stop_display",
    };

    /// Uses `tool` on the park; `amount` is the money to add, in hundredths, how many guests or pieces of
    /// litter, how many times slower the guests live (1 for their own pace), or for the other switches 1 on and 0
    /// off, or which of the park's firework displays to start. Returns how many things it changed: tiles, inventions,
    /// guests, rides, animals, pieces, falls or jets, scenarios opened or stall items renamed, or 1 for money added,
    /// designs unlocked, a display started or stopped or a switch turned.
    std::size_t use_debug_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, std::int64_t amount);

    /// The tab's contents, drawn into the developer window open now.
    void draw_debug_tab(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

} // namespace rct3::scene::park
