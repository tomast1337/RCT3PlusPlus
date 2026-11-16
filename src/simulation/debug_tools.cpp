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

    std::size_t add_guests(SimWorld& world, random::RandomState& dice, std::span<const NodeId> spots, std::size_t count)
    {
        std::size_t came = 0;
        while (!spots.empty() && came < count)
            came += admit_party(world, dice, spots[random::below(dice, spots.size())]).size();
        return came;
    }

    std::size_t send_guests_home(SimWorld& world)
    {
        entt::registry& r = world.registry;
        for (auto [e, ride] : r.view<CycleRide>().each())
        {
            ride.queue.clear();
            ride.riders.clear();
            for (Stop& stop : ride.stops)
                stop.queue.clear();
        }
        for (auto [e, trains] : r.view<Trains>().each())
            for (std::vector<entt::entity>& riders : trains.riders)
                riders.clear();
        const auto parties = r.view<Party>();
        r.destroy(parties.begin(), parties.end());
        const auto guests = r.view<Visitor>(entt::exclude<Left, Staff>) | std::ranges::to<std::vector<entt::entity>>();
        for (const entt::entity guest : guests)
        {
            ++r.ctx().emplace<Turnstile>().out[static_cast<std::size_t>(age_of(r, guest))];
            // What a guest who walked out of the gate has lost, and what only one in the middle of something has.
            r.remove<Needs, Mood, Wallet, Errand, Stride, Dice, Member, Lost, Linger, Gesture, Glance, Watching, Carried, RideWalk, Crossing, Swimmer, PoolRoute, Floating, Climbing, Outside, Thrill, Held, Dropped>(guest);
            r.emplace<Left>(guest);
        }
        return guests.size();
    }

    std::size_t refresh_guests(SimWorld& world)
    {
        std::size_t count = 0;
        for (auto [e, needs, mood] : world.registry.view<Needs, Mood>(entt::exclude<Left, Staff>).each())
        {
            ++count;
            needs = { };
            mood.happiness = units::Share { 1 };
            mood.energy = units::Share { 1 };
            mood.nausea = { };
            mood.wet = { };
            mood.heat = { };
        }
        return count;
    }

    std::size_t scatter_litter(SimWorld& world, random::RandomState& dice, std::span<const NodeId> spots, std::size_t count)
    {
        if (spots.empty())
            return 0;
        for (std::size_t i = 0; i < count; ++i)
            world.registry.emplace<Litter>(world.registry.create(), Litter { .node = spots[random::below(dice, spots.size())], .kind = static_cast<std::uint8_t>(random::below(dice, 2)) });
        return count;
    }

    std::size_t clear_litter(SimWorld& world)
    {
        const auto litter = world.registry.view<Litter>();
        const std::size_t count = litter.size();
        world.registry.destroy(litter.begin(), litter.end());
        return count;
    }

} // namespace rct3::simulation
