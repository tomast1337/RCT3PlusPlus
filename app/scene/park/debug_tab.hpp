// SPDX-License-Identifier: GPL-3.0-or-later
//
// The developer windows' Debug tab: one-click tools that put the park in a state at once (money, land,
// research, saved designs, career scenarios, guests, stalls, rides, animals, litter, water, firework displays), switches for the weather, the time of day and what is drawn,
// and the people's motions stepped through one by one or played in turn, slowed down, or their parties standing together often. Scripts use the same tools by name.
// Tools that change the park are developer actions (simulation/actions/developer.hpp), queued from the tab and applied at once
// for scripts; those that change only what is shown, saved designs or career files stay here.
#pragma once

#include "core/types.hpp"

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

    enum class DebugTool : u8
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
        AddDucks,
        AddGulls,
        RemoveWildBirds,
        KnockFences,
        BreedAnimals,
        AgeAnimals,
    };

    /// Each tool by the name scripts use for it, in DebugTool's order.
    inline constexpr std::array<std::string_view, 29> kDebugToolNames {
        "add_money", "buy_all_land", "finish_research", "add_guests", "remove_guests", "refresh_guests", "break_rides", "reset_rides", "remove_animals", "refresh_animals", "drop_litter", "clear_litter", "rebuild_waterfalls", "reset_jets", "unlock_designs", "unlock_scenarios", "reset_item_names", "slow_guests", "parties_huddle_often", "cycle_motions", "cycle_person_motions", "start_display", "stop_display", "add_ducks", "add_gulls", "remove_wild_birds", "knock_fences", "breed_animals", "age_animals",
    };

    /// Uses `tool` on the park at once, which counts as a cheat used there; a tool that is a developer action is applied
    /// as one, without waiting for a tick; `amount` is the money to add, in hundredths, how many guests, pieces of
    /// litter or knocks to the enclosures' fences, the game seconds till animals made pregnant give birth (0 for an hour) or how much older the animals get (0 for a year), how many times slower the guests live (1 for their own pace), or for the other switches 1 on and 0
    /// off, or which of the park's firework displays to start. Returns how many things it changed: tiles, inventions,
    /// guests, rides, animals, birds, pieces, fence panels worn, falls or jets, scenarios opened or stall items renamed, or 1 for money
    /// added, designs unlocked, a display started or stopped or a switch turned.
    usize use_debug_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, i64 amount);

    /// After actions applied: builds again what developer actions changed of the park, as their flags in the
    /// action queue's record say, and lowers the flags.
    void show_changed_park(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    /// The tab's contents, drawn into the developer window open now.
    void draw_debug_tab(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

} // namespace rct3::scene::park
