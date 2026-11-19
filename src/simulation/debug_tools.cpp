// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/debug_tools.hpp"

#include "core/types.hpp"
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

    void invent_everything(Research& research, std::vector<ResearchItem>& items, f32 time)
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

    usize add_guests(SimWorld& world, random::RandomState& dice, std::span<const NodeId> spots, usize count)
    {
        usize came = 0;
        while (!spots.empty() && came < count)
            came += admit_party(world, dice, spots[random::below(dice, spots.size())]).size();
        return came;
    }

    usize send_guests_home(SimWorld& world)
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
            ++r.ctx().emplace<Turnstile>().out[static_cast<usize>(age_of(r, guest))];
            // What a guest who walked out of the gate has lost, and what only one in the middle of something has.
            r.remove<Needs, Mood, Wallet, Errand, Stride, Dice, Member, Lost, Linger, Gesture, Glance, Watching, Carried, RideWalk, Crossing, Swimmer, PoolRoute, Floating, Climbing, Outside, Thrill, Held, Dropped>(guest);
            r.emplace<Left>(guest);
        }
        return guests.size();
    }

    usize refresh_guests(SimWorld& world)
    {
        usize count = 0;
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

    usize break_down_rides(SimWorld& world)
    {
        entt::registry& r = world.registry;
        usize broke = 0;
        for (const entt::entity e : r.view<Attraction, CycleRide>())
        {
            Attraction& attraction = r.get<Attraction>(e);
            Upkeep& upkeep = r.get_or_emplace<Upkeep>(e);
            if (upkeep.broken)
                continue;
            // A failure any ride, flat or with trains, can have.
            upkeep.kind = upkeep.last = Breakdown::ControlFailure;
            upkeep.broken = true;
            ++upkeep.breakdowns;
            attraction.open = false;
            ++broke;
        }
        return broke;
    }

    usize reset_rides(SimWorld& world)
    {
        entt::registry& r = world.registry;
        usize count = 0;
        for (auto [e, attraction, ride] : r.view<Attraction, CycleRide>().each())
        {
            ++count;
            if (Upkeep* upkeep = r.try_get<Upkeep>(e))
            {
                if (upkeep->broken)
                    attraction.open = true;
                *upkeep = Upkeep { .inspect_every = upkeep->inspect_every, .mechanic = upkeep->mechanic, .break_in = upkeep->break_in };
            }
            attraction.customers = 0;
            attraction.income = { };
            ride.queue.clear();
            for (Stop& stop : ride.stops)
                stop.queue.clear();
            ride.per_hour = 0;
            ride.since_departure.reset();
        }
        for (auto [guest, errand, stride, pose] : r.view<Errand, Stride, Pose>().each())
        {
            if (errand.kind != ErrandKind::Queueing || !r.valid(errand.target) || !r.all_of<CycleRide>(errand.target))
                continue;
            const std::optional<NodeId> entrance = entrance_of(r, errand.target, errand.station);
            // A party goes on together behind its leader, who picks somewhere else to go.
            const auto* member = r.try_get<Member>(guest);
            const bool follows = member != nullptr && r.valid(member->party) && r.get<Party>(member->party).leader != guest;
            errand = follows ? Errand { ErrandKind::Following, entt::null, 0 } : Errand { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
            r.remove<RideWalk>(guest);
            if (entrance)
                stop_at(world.paths, *entrance, stride, pose);
            else
                stride.route.clear();
        }
        return count;
    }

    usize remove_animals(SimWorld& world)
    {
        entt::registry& r = world.registry;
        const auto animals = r.view<Animal>();
        const usize count = animals.size();
        for (auto [keeper, duty] : r.view<KeeperDuty>().each())
            if (duty.job == KeeperJob::Catching)
                duty = KeeperDuty { .house = duty.house };
        r.destroy(animals.begin(), animals.end());
        return count;
    }

    usize refresh_animals(SimWorld& world)
    {
        usize count = 0;
        for (auto [e, animal] : world.registry.view<Animal>().each())
        {
            ++count;
            animal.hunger = { };
            animal.thirst = { };
            animal.tiredness = { };
            animal.loneliness = { };
            animal.health = units::Share { 1 };
            animal.empty_troughs = 0;
        }
        return count;
    }

    usize scatter_litter(SimWorld& world, random::RandomState& dice, std::span<const NodeId> spots, usize count)
    {
        if (spots.empty())
            return 0;
        for (usize i = 0; i < count; ++i)
            world.registry.emplace<Litter>(world.registry.create(), Litter { .node = spots[random::below(dice, spots.size())], .kind = static_cast<u8>(random::below(dice, 2)) });
        return count;
    }

    usize clear_litter(SimWorld& world)
    {
        const auto litter = world.registry.view<Litter>();
        const usize count = litter.size();
        world.registry.destroy(litter.begin(), litter.end());
        return count;
    }

} // namespace rct3::simulation
