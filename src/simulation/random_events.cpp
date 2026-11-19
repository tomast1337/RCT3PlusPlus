// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/random_events.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    std::string_view event_name(RandomEvent event)
    {
        switch (event)
        {
            case RandomEvent::RideBreakdown:
                return "ride_breakdown";
            case RandomEvent::WeatherChange:
                return "weather_change";
            case RandomEvent::Vandalism:
                return "vandalism";
            case RandomEvent::GuestArrival:
                return "guest_arrival";
            case RandomEvent::ResearchFinding:
                return "research_finding";
            case RandomEvent::AnimalEscape:
                return "animal_escape";
            case RandomEvent::Count_:
                break;
        }
        return "unknown";
    }

    f64 chance_in(f64 rate, f64 hours)
    {
        if (!(rate > 0) || !(hours > 0))
            return 0;
        return 1 - std::exp(-rate * hours);
    }

    std::vector<RandomEvent> roll_events(random::RandomState& gameplay, const EventRates& rates, f64 hours)
    {
        std::vector<RandomEvent> out;
        for (usize i = 0; i < kRandomEventCount; ++i)
        {
            const f64 p = chance_in(rates.per_hour[i], hours);
            // No draw for an event that cannot happen, so turning one on later does not shift the others'
            // rolls in parks that never use it.
            if (p > 0 && random::chance(gameplay, p))
                out.push_back(static_cast<RandomEvent>(i));
        }
        return out;
    }

    std::optional<usize> pick_weighted(random::RandomState& state, std::span<const f64> weights)
    {
        f64 total = 0;
        for (const f64 w : weights)
            total += std::max(w, 0.0);
        if (!(total > 0))
            return std::nullopt;
        f64 at = random::between(state, 0.0, total);
        std::optional<usize> last;
        for (usize i = 0; i < weights.size(); ++i)
        {
            if (!(weights[i] > 0))
                continue;
            last = i;
            if (at < weights[i])
                return i;
            at -= weights[i];
        }
        return last; // Rounding left `at` just past the end
    }

} // namespace rct3::simulation
