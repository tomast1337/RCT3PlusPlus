// SPDX-License-Identifier: GPL-3.0-or-later
//
// Research: a park that sets a monthly budget for it invents, one at a time, the attractions it has not got
// yet. The budget leaves the park's cash in small parts through the month, each booked as research spending
// and counted towards the item being worked on; once what was spent covers the item's cost, the item is
// invented and the next is drawn, by how much the player cares for each research area. What is spent beyond a
// cost carries over to the next item.
//
// A park with no budget does no research: what it may build is what its file says, which in a sandbox is
// everything.
// shortcut: only attractions are researched; scenery and ride improvements stay as the park file has them
// until their categories are read.
#pragma once

#include "park/build_catalogue.hpp"
#include "random/generator.hpp"
#include "save/model/park.hpp"
#include "simulation/park/state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    /// The research areas, in the order a park file keeps their priorities.
    enum class ResearchArea : std::uint8_t
    {
        Coasters,
        JuniorRides,
        ThrillRides,
        WaterRides,
        GentleRides,
        TransportRides,
        OtherRides,
        Shops,
        Scenery,
        Paths,
        ShowElements,
        PoolRides,
    };
    inline constexpr std::size_t kResearchAreas = 12;

    /// The message telling the player an attraction was invented: the game's own text, naming the attraction.
    inline constexpr std::string_view kInventedKey = "Invented_Attraction";

    /// Something the park may invent.
    struct ResearchItem
    {
        std::uint64_t id = 0; ///< Of its entry in the park's content (save::model::AssetRequirement::id)
        ResearchArea area = ResearchArea::OtherRides;
        std::int64_t cost = 0; ///< Hundredths
    };

    /// How far the park's research has got.
    struct Research
    {
        std::int64_t budget = 0;                   ///< Hundredths a month; nought when the park does no research
        std::int64_t spent = 0;                    ///< Hundredths counted towards the current item
        std::int64_t boost = 0;                    ///< Hundredths a prize gave, counted with the next part paid
        std::optional<std::uint64_t> current { };  ///< The item being worked on (ResearchItem::id)
        std::optional<std::uint64_t> previous { }; ///< The item invented last
        std::vector<float> priorities { };         ///< A weight for each ResearchArea, in order; areas past its end weigh 1
        float since_part = 0;                      ///< Calendar units since the last part of the budget was paid
        std::vector<std::uint64_t> invented { };   ///< Items invented since the park file was made, oldest first
    };

    /// Research as a park file leaves it.
    [[nodiscard]] Research start_research(const save::model::ResearchState& saved);

    /// What is left to invent: each attraction the park makes available, has not invented and does not hide,
    /// that `catalogue` knows, with the area of the list it is built from and what its kind costs to invent.
    [[nodiscard]] std::vector<ResearchItem> research_items(const save::model::RequiredContent& content, std::span<const rct3::park::BuildItem> catalogue);

    /// Moves research on by `units` of the calendar, a month at most. Once a part of the budget is due, it is
    /// taken from `park`'s cash and booked as research spending, and counts towards the current item with any
    /// boost waiting; a park that cannot pay it skips it, and one whose money never runs out researches for
    /// free. An item whose cost is covered leaves `items` and joins Research::invented, and the next is drawn
    /// from the areas that still have any, by their priorities. With nothing left to invent, nothing is paid.
    void step_research(Research& research, float units, std::vector<ResearchItem>& items, park::ParkState& park, save::model::Finance& finance, std::optional<std::int64_t>& kept, random::RandomState& dice);

    /// How far the item being worked on has got.
    struct ResearchProgress
    {
        std::uint64_t id = 0;                   ///< ResearchItem::id
        float done = 0;                         ///< Of its cost, 0 to 1, with any boost waiting
        std::optional<std::int64_t> months { }; ///< Whole months the budget takes to cover the rest, at least one; none without a budget
    };

    /// The progress of the current item, if it is among `items`.
    [[nodiscard]] std::optional<ResearchProgress> research_progress(const Research& research, std::span<const ResearchItem> items);

} // namespace rct3::simulation
