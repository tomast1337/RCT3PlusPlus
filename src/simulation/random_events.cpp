// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/random_events.hpp"

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

    double chance_in(double rate, double hours)
    {
        if (!(rate > 0) || !(hours > 0))
            return 0;
        return 1 - std::exp(-rate * hours);
    }

    std::vector<RandomEvent> roll_events(random::RandomState& gameplay, const EventRates& rates, double hours)
    {
        std::vector<RandomEvent> out;
        for (std::size_t i = 0; i < kRandomEventCount; ++i)
        {
            const double p = chance_in(rates.per_hour[i], hours);
            // No draw for an event that cannot happen, so turning one on later does not shift the others'
            // rolls in parks that never use it.
            if (p > 0 && random::chance(gameplay, p))
                out.push_back(static_cast<RandomEvent>(i));
        }
        return out;
    }

    std::optional<std::size_t> pick_weighted(random::RandomState& state, std::span<const double> weights)
    {
        double total = 0;
        for (const double w : weights)
            total += std::max(w, 0.0);
        if (!(total > 0))
            return std::nullopt;
        double at = random::between(state, 0.0, total);
        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < weights.size(); ++i)
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
