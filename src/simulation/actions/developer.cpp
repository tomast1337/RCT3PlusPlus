// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/developer.hpp"

#include "park/boundary_fence.hpp"
#include "park/land_buying.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/debug_tools.hpp"
#include "simulation/fence_wear.hpp"
#include "simulation/guest_edits.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_park.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rct3::simulation::actions
{

    namespace
    {
        /// How much older AgeAnimals makes the animals when no amount is given.
        constexpr units::Seconds kAgeByDefault { save::model::GameClock::kUnitsPerYear / save::model::GameClock::kUnitsPerMonth * kAnimalMonth.value };
    } // namespace

    Outcome check(const World& world, const UseDeveloperTool& action)
    {
        using enum DeveloperTool;
        if ((action.tool == AddMoney && !world.park.cash) || (action.tool == FinishResearch && !world.research_items) || (action.tool == AdmitParties && world.sim.registry.view<const ParkGate>().empty()))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    usize apply(World& world, const UseDeveloperTool& action)
    {
        const auto count = static_cast<usize>(std::max<i64>(action.amount, 0));
        // The park's own walkable tiles, not its queues nor the road out to the gate: where guests and litter go.
        const auto park_paths = [&] {
            std::vector<NodeId> spots;
            const auto& nodes = world.sim.paths.nodes();
            for (u32 n = 0; n < nodes.size(); ++n)
                if (const auto& t = nodes[n].tile; !nodes[n].queue && !nodes[n].removed && world.grid.contains(t.col, t.row) && rct3::park::owned_outright(world.grid.at(t.col, t.row)))
                    spots.push_back(NodeId { n });
            return spots;
        };
        world.park.cheats_used = true;
        auto& r = world.sim.registry;
        switch (action.tool)
        {
            using enum DeveloperTool;
            case AddMoney:
                *world.park.cash += units::Money { action.amount };
                return 1;
            case BuyAllLand:
            {
                std::vector<save::model::Tile> tiles;
                for (i32 row = 0; row < world.grid.height; ++row)
                    for (i32 col = 0; col < world.grid.width; ++col)
                        tiles.push_back({ col, row });
                // Free: the tool is for setting a park up, not for its books.
                const usize bought = rct3::park::buy_land(world.grid, tiles, rct3::park::LandBuy::Land, { }, std::nullopt).tiles + rct3::park::buy_land(world.grid, tiles, rct3::park::LandBuy::Rights, { }, std::nullopt).tiles;
                if (bought > 0)
                {
                    keep_staff_in(world.sim, world.grid);
                    world.changed.land_owned = true;
                }
                return bought;
            }
            case FinishResearch:
            {
                const usize had = world.research.invented.size();
                invent_everything(world.research, *world.research_items, world.park.time);
                for (usize i = had; i < world.research.invented.size(); ++i)
                    if (const auto entry = std::ranges::find(world.model.content.entries, world.research.invented[i], &save::model::AssetRequirement::id); entry != world.model.content.entries.end())
                        entry->invented = true;
                world.changed.invented = true;
                return world.research.invented.size() - had;
            }
            case AddGuests:
                return add_guests(world.sim, world.sim.gameplay, park_paths(), count);
            case RemoveGuests:
                return send_guests_home(world.sim);
            case RefreshGuests:
                return refresh_guests(world.sim);
            case BreakRides:
                return break_down_rides(world.sim);
            case ResetRides:
                return reset_rides(world.sim);
            case RemoveAnimals:
                return remove_animals(world.sim);
            case RefreshAnimals:
                return refresh_animals(world.sim);
            case DropLitter:
                return scatter_litter(world.sim, world.sim.gameplay, park_paths(), count);
            case ClearLitter:
                return clear_litter(world.sim);
            case ResetItemNames:
            {
                constexpr std::string_view kOwnName = "%ME%"; ///< How a stall item keeps the name of its kind
                usize renamed = 0;
                for (save::model::Stall& stall : world.rides.stalls)
                    for (save::model::StallItem& item : stall.items)
                        if (item.name != kOwnName)
                        {
                            item.name = kOwnName;
                            ++renamed;
                        }
                return renamed;
            }
            case SlowGuests:
            {
                const f32 slowdown = static_cast<f32>(std::max<i64>(action.amount, 1));
                return std::exchange(world.sim.guest_slowdown, slowdown) != slowdown ? 1 : 0;
            }
            case PartiesHuddleOften:
                return std::exchange(world.sim.parties_huddle_often, action.amount != 0) != (action.amount != 0) ? 1 : 0;
            case KnockFences:
            {
                const usize knocked = knock_fences(world.sim, world.enclosures, count);
                world.changed.fences_worn |= knocked > 0;
                return knocked;
            }
            case BreedAnimals:
                return make_animals_pregnant(world.sim, units::Seconds { action.amount > 0 ? static_cast<f32>(action.amount) : kSecondsPerHour });
            case AgeAnimals:
                return age_animals(world.sim, units::Seconds { action.amount > 0 ? static_cast<f32>(action.amount) : kAgeByDefault.value });
            case AdmitParties:
            {
                const NodeId gate = r.get<const ParkGate>(r.view<const ParkGate>().front()).node;
                for (usize k = 0; k < count; ++k)
                    (void)admit_party(world.sim, world.sim.gameplay, gate);
                return count;
            }
            case GiveEveryoneCash:
            {
                usize given = 0;
                for (auto [e, wallet] : r.view<Wallet>(entt::exclude<Left>).each())
                {
                    wallet.cash.value += action.amount;
                    ++given;
                }
                return given;
            }
            case CheerEveryone:
            {
                usize cheered = 0;
                for (const entt::entity e : r.view<Mood, Needs>())
                {
                    make_happy_and_healthy(r, e);
                    ++cheered;
                }
                return cheered;
            }
        }
        return 0;
    }

    Outcome check(const World& world, const SetParkValues& action)
    {
        if ((action.cash && !world.park.cash) || (action.entrance_fee && !world.model.has_finance))
            return std::unexpected(Refusal::Missing);
        return Checked { };
    }

    void apply(World& world, const SetParkValues& action)
    {
        world.park.cheats_used = true;
        if (action.rating)
            world.park.rating = *action.rating;
        if (action.cash)
            world.park.cash = *action.cash;
        if (action.loan)
            world.park.loan.value = std::max<i64>(action.loan->value, 0);
        if (action.entrance_fee)
            world.model.finance.entrance_fee = static_cast<i32>(std::clamp<i64>(action.entrance_fee->value, 0, std::numeric_limits<i32>::max()));
        if (action.hours)
        {
            world.day_clock.park_hours = opening_hours(action.hours->first, action.hours->second);
            change_mode(world.day_clock, world.day_clock.mode);
            world.model.park.opening_minutes = action.hours->first;
            world.model.park.closing_minutes = action.hours->second;
        }
        if (action.weather)
            world.park.weather = world.park.forecast.emplace(*action.weather);
        if (action.time_of_day)
        {
            // The clock runs on from there, into the park's hours if outside.
            world.day_clock.time = world.day_clock.shown = *action.time_of_day;
            world.day_clock.fade = 0;
        }
    }

} // namespace rct3::simulation::actions
