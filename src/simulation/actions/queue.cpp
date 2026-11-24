// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/queue.hpp"

#include <utility>

namespace rct3::simulation::actions
{

    void ask(ActionQueue& queue, const SimWorld& world, Action action)
    {
        queue.waiting.push_back({ .tick = world.tick, .action = std::move(action) });
    }

    void apply_due(ActionQueue& queue, World& world)
    {
        const auto due = [&](const ActionQueue::Queued& q) { return q.tick <= world.sim.tick; };
        for (const ActionQueue::Queued& q : queue.waiting)
            if (due(q))
                if (const Outcome done = apply(world, q.action); !done && !done.error().told.empty())
                    queue.told.push_back(done.error().told);
        std::erase_if(queue.waiting, due);
    }

} // namespace rct3::simulation::actions
