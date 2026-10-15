// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_state.hpp"

#include <cmath>

namespace rct3::simulation
{

    ParkState park_state(const save::ParkModel* park, const save::WorldModel& world)
    {
        ParkState s;
        if (park != nullptr)
        {
            if (park->has_clock)
                s.date = park->clock.date();
            if (park->has_finance)
            {
                if (!park->finance.unlimited())
                    s.cash = park->finance.cash;
                s.loan = park->finance.loan;
            }
            if (park->has_park)
            {
                if (const auto guests = park->park.guests_in_park.latest())
                    s.guests = static_cast<std::int32_t>(std::lround(*guests));
                if (const auto rating = park->park.rating())
                    s.rating = *rating / save::kMaxParkRating;
            }
            s.messages = park_messages(park->messages);
        }
        if (world.weather)
        {
            s.weather = world.weather->type;
            s.forecast = world.weather->forecast;
            s.temperature = world.weather->temperature;
        }
        return s;
    }

} // namespace rct3::simulation
