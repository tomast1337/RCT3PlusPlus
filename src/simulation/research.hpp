// SPDX-License-Identifier: GPL-3.0-or-later
//
// Research: a park that sets a monthly budget for it invents, one at a time, the attractions it has not got
// yet. The budget leaves the park's cash in small parts through the month, each booked as research spending
// and counted towards the item being worked on; once what was spent covers the item's cost, the item is
// invented and the next is drawn, by how much the player cares for each research area. What is spent beyond a
// cost carries over to the next item.
//
// The player may also focus research on one item of their choosing: it is worked on at once, at a higher cost,
// and what it took the place of is taken up again once it is invented, from where it was left.
//
// Besides attractions, research invents the scenery that is put up for its looks (park::researchable) and the
// path and queue styles other than the plain ones.
//
// A park with no budget does no research: what it may build is what its file says, which in a sandbox is
// everything.
// shortcut: ride improvements stay as the park file has them; upgrade when improvements are invented over time.
#pragma once

#include "park/build_catalogue.hpp"
#include "park/path/styles.hpp"
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

    /// The messages telling the player something was invented: the game's own texts, naming what it was.
    inline constexpr std::string_view kInventedKey = "Invented_Attraction";
    inline constexpr std::string_view kInventedSceneryKey = "Invented_SceneryItem";
    inline constexpr std::string_view kInventedPathKey = "Invented_Path";
    inline constexpr std::string_view kInventedQueueKey = "Invented_Queue";

    /// Something the park may invent.
    struct ResearchItem
    {
        std::uint64_t id = 0; ///< Of its entry in the park's content (save::model::AssetRequirement::id)
        ResearchArea area = ResearchArea::OtherRides;
        std::int64_t cost = 0; ///< Hundredths
    };

    /// An item research was taken off for a focus, with what had been spent on it.
    struct SetAside
    {
        std::uint64_t id = 0; ///< ResearchItem::id
        std::int64_t spent = 0;
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
        std::vector<float> invented_at { };        ///< The calendar's time each of `invented` was invented (park::ParkState::time)
        /// The item the player asked to research next, at its cost times tuning::Research::focus_cost; the current
        /// item while set.
        std::optional<std::uint64_t> focus { };
        std::optional<std::uint64_t> unfocused { }; ///< The item worked on when the focus began, taken up again once it ends
        /// Items research was taken off for a focus, oldest first; each starts from what was spent on it when it is
        /// worked on again.
        std::vector<SetAside> set_aside { };
    };

    /// Research as a park file leaves it.
    [[nodiscard]] Research start_research(const save::model::ResearchState& saved);

    /// What is left to invent, among what the park makes available and has not invented: each attraction it does
    /// not hide that `catalogue` knows, with the area of the list it is built from and what its kind costs; each
    /// researchable scenery item `catalogue` knows, costing by its price; and each style of `paths` that names
    /// no research category.
    [[nodiscard]] std::vector<ResearchItem> research_items(const save::model::RequiredContent& content, std::span<const rct3::park::BuildItem> catalogue, std::span<const rct3::park::path::PathStyle> paths);

    /// Moves research on by `units` of the calendar, a month at most. Once a part of the budget is due, it is
    /// taken from `park`'s cash and booked as research spending, and counts towards the current item with any
    /// boost waiting; a park that cannot pay it skips it, and one whose money never runs out researches for
    /// free. An item whose cost is covered leaves `items` and joins Research::invented, and the next is drawn
    /// from the areas that still have any, by their priorities. With nothing left to invent, nothing is paid.
    void step_research(Research& research, float units, std::vector<ResearchItem>& items, park::ParkState& park, save::model::Finance& finance, std::optional<std::int64_t>& kept, random::RandomState& dice);

    /// What `item` costs to invent: its cost, more when research is `focused` on it.
    [[nodiscard]] std::int64_t research_cost(const ResearchItem& item, bool focused);

    /// Focuses research on the item `id` of `items`, at once and at a higher cost, setting aside the item worked
    /// on with what was spent on it; with no `id`, ends the focus and takes up again the item worked on when it
    /// began. False, changing nothing, when `id` is not among `items`.
    bool focus_research(Research& research, std::optional<std::uint64_t> id, std::span<const ResearchItem> items);

    /// What may come next in each area, in the areas' order: the `per_area` cheapest of `items` there, cheapest
    /// first, leaving out the item being worked on.
    [[nodiscard]] std::vector<ResearchItem> research_preview(const Research& research, std::span<const ResearchItem> items, std::size_t per_area);

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
