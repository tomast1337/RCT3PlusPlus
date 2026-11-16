// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/debug_tools.hpp"

#include "simulation/animals.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <ranges>

namespace rct3::simulation
{

    void invent_everything(Research& research, std::vector<ResearchItem>& items, float time)
    {
        for (const ResearchItem& item : items)
        {
            research.invented.push_back(item.id);
            research.invented_at.push_back(time);
        }
        if (!items.empty())
            research.previous = items.back().id;
        items.clear();
        research.spent = { };
        research.current.reset();
        research.focus.reset();
        research.unfocused.reset();
        research.set_aside.clear();
    }

} // namespace rct3::simulation
