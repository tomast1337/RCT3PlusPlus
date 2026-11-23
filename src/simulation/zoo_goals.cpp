// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/zoo_goals.hpp"

#include "core/types.hpp"
#include "strings/ascii.hpp"

#include <algorithm>

namespace rct3::simulation
{
    bool zoo_goal(save::model::ConditionKind kind) noexcept
    {
        using enum save::model::ConditionKind;
        return kind == AnimalsOfSpecies || kind == AnimalSalesProfit || kind == AnimalsReleased || kind == EscapedAnimals || kind == ViewingGalleries || kind == GalleryVisitors || kind == GalleryIncome;
    }

    GoalReading read_zoo_goal(const save::model::Condition& c, const ParkFigures& f)
    {
        using enum save::model::ConditionKind;
        const auto compare = [&](i64 now) { return GoalReading { now, c.value && (c.greater_than.value_or(true) ? now >= *c.value : now <= *c.value) }; };
        const auto of_species = [&](std::string_view species) { return c.animals && !c.animals->species.empty() && strings::equals_ignoring_case(species, c.animals->species); };
        switch (c.kind)
        {
            case AnimalsOfSpecies:
                return compare(std::ranges::count_if(f.animals, [&](const GoalAnimal& a) { return of_species(a.species) && (a.born_here || c.animals->bought_count); }));
            case AnimalSalesProfit:
            {
                i64 profit = 0;
                for (const save::model::LedgerPeriod* period : { &f.months_before, &f.this_month })
                {
                    for (const save::model::SpeciesSpending& s : period->species_spending)
                        profit -= of_species(s.species) ? s.spending.purchase_sale : 0; // Sales are kept below nought
                }
                return compare(profit);
            }
            case AnimalsReleased:
                return c.animals ? compare(c.animals->done) : GoalReading { };
            case EscapedAnimals:
            {
                if (!c.animals)
                    return { };
                const GoalReading caught = compare(c.animals->done);
                return { caught.now, caught.met && (!c.challenge || c.animals->started) };
            }
            case ViewingGalleries:
                return compare(f.galleries);
            case GalleryVisitors:
                return compare(std::max(f.gallery_visitors, c.gallery ? c.gallery->visitors : 0));
            case GalleryIncome:
                return compare(-i64 { f.last_month.viewing_gallery_tickets.value_or(0) }); // Income is kept below nought
            default:
                return { };
        }
    }

} // namespace rct3::simulation
