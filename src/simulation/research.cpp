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

    namespace
    {
        /// Makes `id` the item worked on: the one before is set aside with what was spent on it, and `id` starts from
        /// what was spent on it when it was set aside.
        void work_on(Research& research, std::uint64_t id)
        {
            if (research.current == id)
                return;
            if (research.current)
                research.set_aside.push_back({ .id = *research.current, .spent = std::exchange(research.spent, { }) });
            research.current = id;
            if (const auto kept = std::ranges::find(research.set_aside, id, &SetAside::id); kept != research.set_aside.end())
            {
                research.spent += kept->spent;
                research.set_aside.erase(kept);
            }
        }
    } // namespace

    Research start_research(const save::model::ResearchState& saved)
    {
        return {
            .budget = { std::max(saved.monthly_budget, 0) },
            .spent = { std::max(saved.money_spent_on_current, 0) },
            .current = saved.current_item,
            .previous = saved.previous_item,
            .priorities = saved.priorities,
            .since_part = std::isfinite(saved.time_since_last_spend) ? std::max(saved.time_since_last_spend, 0.0f) : 0.0f,
        };
    }

    std::vector<ResearchItem> research_items(const save::model::RequiredContent& content, std::span<const rct3::park::BuildItem> catalogue, std::span<const rct3::park::path::PathStyle> paths)
    {
        using rct3::park::BuildKind;
        using rct3::park::BuildList;
        using save::model::AssetKind;
        const tuning::Research& t = kTuning.research;
        std::vector<ResearchItem> items;
        for (const save::model::AssetRequirement& e : content.entries)
        {
            if (!e.available || e.invented)
                continue;
            if (e.kind == AssetKind::PathType || e.kind == AssetKind::QueueType)
            {
                const bool queue = e.kind == AssetKind::QueueType;
                const auto style = std::ranges::find_if(paths, [&](const rct3::park::path::PathStyle& s) { return s.queue == queue && strings::equals_ignoring_case(s.name, e.symbol); });
                if (style != paths.end() && !style->names_category)
                    items.push_back({ .id = e.id, .area = ResearchArea::Paths, .cost = t.other_cost });
                continue;
            }
            if (e.kind == AssetKind::Scenery)
            {
                const auto item = std::ranges::find_if(catalogue, [&](const rct3::park::BuildItem& i) { return i.kind == BuildKind::Scenery && strings::equals_ignoring_case(i.symbol, e.symbol) && strings::equals_ignoring_case(i.overlay, e.overlay_stem); });
                if (item != catalogue.end() && rct3::park::researchable(*item))
                    items.push_back({ .id = e.id, .area = ResearchArea::Scenery, .cost = { std::min<std::int64_t>(std::llround(static_cast<double>(item->price.value) * t.scenery_cost_per_price), t.scenery_cost_cap.value) } });
                continue;
            }
            if (e.kind != AssetKind::Attraction || e.hidden)
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
            const units::Money cost = item->kind == BuildKind::TrackedRide ? t.tracked_ride_cost : item->kind == BuildKind::FlatRide ? t.flat_ride_cost : item->kind == BuildKind::Shop ? t.shop_cost : t.other_cost;
            items.push_back({ .id = e.id, .area = area, .cost = cost });
        }
        return items;
    }

    void step_research(Research& research, float units, std::vector<ResearchItem>& items, park::ParkState& park, save::model::Finance& finance, std::optional<std::int64_t>& kept, random::RandomState& dice)
    {
        constexpr float kMonth = save::model::GameClock::kUnitsPerMonth;
        if (research.budget <= units::Money { } || !(units > 0))
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
            // A focus done, what it took the place of is taken up again.
            research.focus.reset();
            if (const auto back = std::exchange(research.unfocused, std::nullopt); back && std::ranges::contains(items, *back, &ResearchItem::id))
            {
                work_on(research, *back);
                return std::ranges::find(items, *back, &ResearchItem::id);
            }
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
            work_on(research, found->id);
            return found;
        };

        auto item = current();
        if (item == items.end())
        {
            research.since_part = 0;
            return;
        }
        const units::Money amount { std::llround(static_cast<double>(research.budget.value) * research.since_part / kMonth) };
        if (park.cash && *park.cash < amount)
        {
            // The part is skipped, not owed: research stands still until the park can pay again.
            research.since_part = 0;
            return;
        }
        research.since_part = std::max(research.since_part - static_cast<float>(static_cast<double>(amount.value) * kMonth / static_cast<double>(research.budget.value)), 0.0f);
        park::book(park, finance, kept, amount, park::LedgerLine::Research, false);
        research.spent += amount + std::exchange(research.boost, { });
        while (item != items.end() && research.spent >= research_cost(*item, research.focus == item->id))
        {
            research.spent -= research_cost(*item, research.focus == item->id);
            research.invented.push_back(item->id);
            research.invented_at.push_back(park.time);
            research.previous = item->id;
            research.current.reset();
            items.erase(item);
            item = current();
        }
    }

    units::Money research_cost(const ResearchItem& item, bool focused)
    {
        return focused ? units::Money { std::llround(static_cast<double>(item.cost.value) * kTuning.research.focus_cost) } : item.cost;
    }

    bool focus_research(Research& research, std::optional<std::uint64_t> id, std::span<const ResearchItem> items)
    {
        if (!id)
        {
            // Ended, the focused item is set aside for what it took the place of, or without that goes on at its
            // usual cost.
            if (const auto back = std::exchange(research.unfocused, std::nullopt); research.focus && back && std::ranges::contains(items, *back, &ResearchItem::id))
                work_on(research, *back);
            research.focus.reset();
            return true;
        }
        if (!std::ranges::contains(items, *id, &ResearchItem::id))
            return false;
        if (!research.focus)
            research.unfocused = research.current;
        work_on(research, *id);
        research.focus = id;
        return true;
    }

    std::vector<ResearchItem> research_preview(const Research& research, std::span<const ResearchItem> items, std::size_t per_area)
    {
        std::vector<ResearchItem> sorted;
        for (const ResearchItem& i : items)
        {
            if (research.current != i.id)
                sorted.push_back(i);
        }
        std::ranges::stable_sort(sorted, [](const ResearchItem& a, const ResearchItem& b) { return std::pair(a.area, a.cost) < std::pair(b.area, b.cost); });
        std::vector<ResearchItem> next;
        for (const ResearchItem& i : sorted)
        {
            if (std::ranges::count(next, i.area, &ResearchItem::area) < static_cast<std::ptrdiff_t>(per_area))
                next.push_back(i);
        }
        return next;
    }

    std::optional<ResearchProgress> research_progress(const Research& research, std::span<const ResearchItem> items)
    {
        const auto item = std::ranges::find(items, research.current, [](const ResearchItem& i) { return std::optional(i.id); });
        if (item == items.end())
            return std::nullopt;
        const units::Money got = research.spent + research.boost;
        const units::Money cost = research_cost(*item, research.focus == item->id);
        const std::int64_t left = std::max(cost - got, units::Money { }).value;
        const std::int64_t budget = research.budget.value;
        return ResearchProgress {
            .id = item->id,
            .done = cost > units::Money { } ? std::clamp(static_cast<float>(got.value) / static_cast<float>(cost.value), 0.0f, 1.0f) : 1.0f,
            .months = budget > 0 ? std::optional(std::max<std::int64_t>((left + budget - 1) / budget, 1)) : std::nullopt,
        };
    }

} // namespace rct3::simulation
