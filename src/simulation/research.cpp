// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/research.hpp"

#include "simulation/park/finance.hpp"
#include "simulation/tuning.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rct3::simulation
{

    Research start_research(const save::model::ResearchState& saved)
    {
        return {
            .budget = std::max(saved.monthly_budget, 0),
            .spent = std::max(saved.money_spent_on_current, 0),
            .current = saved.current_item,
            .previous = saved.previous_item,
            .priorities = saved.priorities,
            .since_part = std::isfinite(saved.time_since_last_spend) ? std::max(saved.time_since_last_spend, 0.0f) : 0.0f,
        };
    }

    std::vector<ResearchItem> research_items(const save::model::RequiredContent& content, std::span<const rct3::park::BuildItem> catalogue)
    {
        using rct3::park::BuildKind;
        using rct3::park::BuildList;
        std::vector<ResearchItem> items;
        for (const save::model::AssetRequirement& e : content.entries)
        {
            if (e.kind != save::model::AssetKind::Attraction || !e.available || e.invented || e.hidden)
                continue;
            const auto item = std::ranges::find_if(catalogue, [&](const rct3::park::BuildItem& i) { return strings::equals_ignoring_case(i.symbol, e.symbol) && strings::equals_ignoring_case(i.overlay, e.overlay_stem); });
            if (item == catalogue.end())
                continue;
            ResearchArea area = ResearchArea::Scenery;
            switch (item->list)
            {
                case BuildList::Coasters:
                    area = ResearchArea::Coasters;
                    break;
                case BuildList::JuniorRides:
                    area = ResearchArea::JuniorRides;
                    break;
                case BuildList::ThrillRides:
                    area = ResearchArea::ThrillRides;
                    break;
                case BuildList::WaterRides:
                    area = ResearchArea::WaterRides;
                    break;
                case BuildList::GentleRides:
                    area = ResearchArea::GentleRides;
                    break;
                case BuildList::TransportRides:
                    area = ResearchArea::TransportRides;
                    break;
                case BuildList::OtherRides:
                    area = ResearchArea::OtherRides;
                    break;
                case BuildList::PoolRides:
                    area = ResearchArea::PoolRides;
                    break;
                case BuildList::FoodShops:
                case BuildList::DrinkShops:
                case BuildList::Stalls:
                case BuildList::Facilities:
                    area = ResearchArea::Shops;
                    break;
                case BuildList::RideEvents:
                case BuildList::Fireworks:
                    area = ResearchArea::ShowElements;
                    break;
                default:
                    break;
            }
            const tuning::Research& t = kTuning.research;
            const Money cost = item->kind == BuildKind::TrackedRide ? t.tracked_ride_cost : item->kind == BuildKind::FlatRide ? t.flat_ride_cost : item->kind == BuildKind::Shop ? t.shop_cost : t.other_cost;
            items.push_back({ .id = e.id, .area = area, .cost = cost.value });
        }
        return items;
    }

    std::uint64_t improvement_item(std::string_view id)
    {
        // FNV-1a.
        std::uint64_t hash = 14695981039346656037ull;
        for (const char c : id)
            hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        return hash | (std::uint64_t { 1 } << 63);
    }

    std::vector<ResearchItem> improvement_items(std::span<const rct3::park::track::Improvement> table, std::span<const std::string> locked)
    {
        std::vector<ResearchItem> items;
        for (const rct3::park::track::Improvement& i : table)
            if (std::ranges::contains(locked, i.id))
                items.push_back({ .id = improvement_item(i.id), .area = ResearchArea::RideImprovements, .cost = i.cost });
        return items;
    }

    void step_research(Research& research, float units, std::vector<ResearchItem>& items, park::ParkState& park, save::model::Finance& finance, std::optional<std::int64_t>& kept, random::RandomState& dice)
    {
        constexpr float kMonth = save::model::GameClock::kUnitsPerMonth;
        if (research.budget <= 0 || !(units > 0))
            return;
        research.since_part = std::min(research.since_part + units, kMonth);
        if (research.since_part < kMonth / static_cast<float>(kTuning.research.parts_per_month))
            return;

        // The item worked on, drawn anew when there is none or the one kept is no longer to be invented: an area
        // by its weight among those with items left, then any one of its items.
        const auto current = [&] {
            auto found = std::ranges::find(items, research.current, [](const ResearchItem& i) { return std::optional(i.id); });
            if (found != items.end())
                return found;
            research.current.reset();
            if (items.empty())
                return found;
            std::array<double, kResearchAreas> weights { };
            std::array<std::uint64_t, kResearchAreas> counts { };
            for (const ResearchItem& i : items)
            {
                const auto at = static_cast<std::size_t>(i.area);
                ++counts[at];
                weights[at] = at < research.priorities.size() ? std::max(research.priorities[at], 0.0f) : 1.0f;
            }
            double total = 0;
            for (const double w : weights)
                total += w;
            // Every area left weighing nothing, they weigh the same.
            if (!(total > 0))
            {
                for (std::size_t at = 0; at < kResearchAreas; ++at)
                    total += weights[at] = counts[at] > 0 ? 1 : 0;
            }
            double draw = random::unit(dice) * total;
            std::size_t area = 0;
            for (std::size_t at = 0; at < kResearchAreas; ++at)
            {
                if (weights[at] <= 0)
                    continue;
                area = at;
                draw -= weights[at];
                if (draw < 0)
                    break;
            }
            std::uint64_t nth = random::below(dice, counts[area]);
            found = std::ranges::find_if(items, [&](const ResearchItem& i) { return static_cast<std::size_t>(i.area) == area && nth-- == 0; });
            research.current = found->id;
            return found;
        };

        auto item = current();
        if (item == items.end())
        {
            research.since_part = 0;
            return;
        }
        const auto amount = static_cast<std::int64_t>(std::llround(static_cast<double>(research.budget) * research.since_part / kMonth));
        if (park.cash && *park.cash < amount)
        {
            // The part is skipped, not owed: research stands still until the park can pay again.
            research.since_part = 0;
            return;
        }
        research.since_part = std::max(research.since_part - static_cast<float>(static_cast<double>(amount) * kMonth / static_cast<double>(research.budget)), 0.0f);
        park::book(park, finance, kept, amount, park::LedgerLine::Research, false);
        research.spent += amount + std::exchange(research.boost, 0);
        while (item != items.end() && research.spent >= item->cost)
        {
            research.spent -= item->cost;
            research.invented.push_back(item->id);
            research.previous = item->id;
            research.current.reset();
            items.erase(item);
            item = current();
        }
    }

    std::optional<ResearchProgress> research_progress(const Research& research, std::span<const ResearchItem> items)
    {
        const auto item = std::ranges::find(items, research.current, [](const ResearchItem& i) { return std::optional(i.id); });
        if (item == items.end())
            return std::nullopt;
        const std::int64_t got = research.spent + research.boost;
        const std::int64_t left = std::max<std::int64_t>(item->cost - got, 0);
        return ResearchProgress {
            .id = item->id,
            .done = item->cost > 0 ? std::clamp(static_cast<float>(got) / static_cast<float>(item->cost), 0.0f, 1.0f) : 1.0f,
            .months = research.budget > 0 ? std::optional(std::max<std::int64_t>((left + research.budget - 1) / research.budget, 1)) : std::nullopt,
        };
    }

} // namespace rct3::simulation
