// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff/work.hpp"

#include "save/model/park.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/guests.hpp"
#include "simulation/holding.hpp"
#include "simulation/park/state.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rct3::simulation::staff
{

    void step_work(SimWorld& world)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
        const float now = static_cast<float>(world.tick) * kTickSeconds;
        const Weather weather = weather_in(r);
        auto& litter_at = world.buffers.litter_at;
        const auto& items_at = world.buffers.items_at;
        const auto items = [&](NodeId node) -> std::span<const entt::entity> {
            const auto found = items_at.find(node);
            return found != items_at.end() ? std::span<const entt::entity>(found->second) : std::span<const entt::entity> { };
        };
        const auto litter_on = [&](NodeId node) {
            const auto found = litter_at.find(node);
            return found != litter_at.end() ? found->second.size() : 0;
        };

        // Staff are paid as each week ends, a quarter of their monthly wage, those at a post as well as those who walk.
        // shortcut: the payroll's week runs from when the park was opened, not from the calendar's week; count from
        // the calendar if the day wages go out ever shows.
        {
            constexpr float kSecondsPerWeek = kSecondsPerHour * save::model::GameClock::kUnitsPerWeek / park::kCalendarUnitsPerHour;
            auto& payroll = r.ctx().emplace<Payroll>();
            for (payroll.week.value += dt; payroll.week.value >= kSecondsPerWeek; payroll.week.value -= kSecondsPerWeek)
                for (const auto [e, staff] : r.view<const Staff>().each())
                    payroll.paid.value += staff.wage.value / 4;
        }
        const auto guests_near = [&](glm::vec3 at) { return std::ranges::any_of(r.view<const Visitor, const Pose>(entt::exclude<Left>).each(), [&](const auto& v) { return glm::distance(std::get<2>(v).position, at) <= kTuning.shows.reach.value; }); };
        // Vendors stay at their post and lifeguards in their chair, at work while guests are near; keepers in their
        // enclosure, at work while at a job. Content or not as other staff are; keepers may quit, the others never do.
        for (const auto [e, staff, pose] : r.view<Staff, const Pose>(entt::exclude<Stride>).each())
        {
            const auto* duty = r.try_get<KeeperDuty>(e);
            if (staff.role != StaffRole::Vendor && staff.role != StaffRole::Other && duty == nullptr)
                continue;
            staff::care_for(staff, duty != nullptr ? duty->job != KeeperJob::Idle && duty->job != KeeperJob::Strolling : guests_near(pose.position), dt);
            if (staff.think.value -= dt; staff.think.value <= 0)
            {
                staff.think = kTuning.staff.think;
                if (!r.all_of<Dice>(e))
                    r.emplace<Dice>(e, dice_for(world, e));
                for (const ThoughtKind kind : staff::staff_thoughts(staff))
                {
                    if (kind == ThoughtKind::StaffWantToQuit && duty == nullptr)
                        continue;
                    think(r, e, kind, entt::null, now);
                    if (kind == ThoughtKind::StaffWantToQuit)
                        r.ctx().emplace<Findings>().found.emplace_back(Finding::WantsToQuit, e);
                }
                if (duty != nullptr && staff::quits(staff))
                    r.ctx().emplace<Findings>().found.emplace_back(Finding::Quit, e);
            }
        }

        // Staff walk the paths. Handymen sweep, empty bins and mend; mechanics mend; each job keeps them a while.
        if (paths.nodes().empty())
            return;
        const auto staff_view = r.view<Staff, Stride, Pose, Dice>();
        // Flower beds dry out; a claim on one lasts while its handyman is there to water it.
        const auto beds = r.view<FlowerBed>();
        for (const auto [b, bed] : beds.each())
        {
            bed.water = std::max(bed.water - bed.capacity * dt / kTuning.handymen.dry.value, 0.0f);
            if (bed.handyman != entt::null && (!r.valid(bed.handyman) || !r.all_of<Staff>(bed.handyman) || (r.get<Staff>(bed.handyman).tasks & kWaterGardensTask) == 0))
                bed.handyman = entt::null;
        }

        // Free mechanics: the nearest by the paths goes to a broken ride, else to one due an inspection.
        // A pool ride has no doors: it is seen to from the path door of a changing room of its complex, the
        // mechanic going no further in.
        const auto spots = [&](entt::entity ride) {
            const auto* cycle = r.try_get<CycleRide>(ride);
            const NodeId exit = cycle != nullptr ? cycle->exit : kNoNode;
            if (const NodeId at = exit != kNoNode ? exit : r.get<Attraction>(ride).entrance; at != kNoNode || r.all_of<PoolDoor>(ride))
                return std::vector { at };
            std::vector<NodeId> rooms;
            if (const auto pool = pool_ride_pool(world, ride))
                for (const entt::entity door : sorted(r.view<const PoolDoor, const Attraction>()))
                    if (r.get<PoolDoor>(door).pool == *pool && r.get<Attraction>(door).entrance != kNoNode)
                        rooms.push_back(r.get<Attraction>(door).entrance);
            return rooms.empty() ? std::vector { kNoNode } : rooms;
        };
        std::vector<entt::entity> free;
        for (const entt::entity e : sorted(staff_view))
        {
            const auto [staff, stride] = staff_view.get<Staff, Stride>(e);
            if (staff.role == StaffRole::Mechanic && staff.ride == entt::null && stride.to < paths.nodes().size())
                free.push_back(e);
        }
        // What a mechanic could not be sent to is told by the park's inspector, if it has one.
        const auto inspectors = r.view<const Staff, const Rounds>();
        const auto inspector = std::ranges::find_if(inspectors, [&](entt::entity i) { return inspectors.get<const Staff>(i).role == StaffRole::Inspector; });
        const auto send = [&](entt::entity ride, Upkeep& upkeep, std::int32_t task) {
            // Those whose patrol takes the ride in, the nearest first, then the nearest without one; one with
            // a patrol keeps to it.
            const auto patrolled = [&](entt::entity m) {
                const auto* patrol = r.try_get<Patrol>(m);
                return patrol != nullptr && !patrol->tiles.empty();
            };
            const auto at = spots(ride);
            const auto ours = [&](entt::entity m, NodeId spot) { return patrolled(m) && spot != kNoNode && r.get<Patrol>(m).covers(paths.nodes()[spot].tile); };
            std::optional<std::vector<NodeId>> best;
            auto who = free.end();
            bool best_ours = false;
            bool asked = false;    // Any free to do it
            bool zoned_out = true; // Every one of them by their patrol
            for (auto m = free.begin(); m != free.end(); ++m)
            {
                if ((r.get<Staff>(*m).tasks & task) == 0)
                    continue; // Told not to
                asked = true;
                zoned_out = zoned_out && patrolled(*m) && std::ranges::none_of(at, [&](NodeId spot) { return ours(*m, spot); });
                for (const NodeId spot : at)
                {
                    if (patrolled(*m) && !ours(*m, spot))
                        continue;
                    if (auto way = paths.route(r.get<Stride>(*m).to, spot, Walker::Staff); way && (!best || std::pair(!ours(*m, spot), way->size()) < std::pair(!best_ours, best->size())))
                    {
                        best = std::move(way);
                        best_ours = ours(*m, spot);
                        who = m;
                    }
                }
            }
            if (who == free.end())
            {
                if (asked && at.front() != kNoNode && inspector != inspectors.end())
                    tell(r, *inspector, zoned_out ? Finding::BadZones : Finding::NoMechanicCanReach, ride, now);
                return;
            }
            r.get<Staff>(*who).ride = ride;
            r.get<Stride>(*who).route = std::move(*best);
            upkeep.mechanic = *who;
            free.erase(who);
        };
        const auto rides = sorted(r.view<const Upkeep, const Attraction>()); // Rides, and pools' changing rooms
        for (const bool broken : { true, false })
            for (const entt::entity ride : rides)
            {
                auto& upkeep = r.get<Upkeep>(ride);
                const bool due = broken ? upkeep.broken : upkeep.called || (upkeep.inspect_every.value >= 0 && upkeep.since_inspection.value >= upkeep.inspect_every.value);
                if (!free.empty() && due && upkeep.mechanic == entt::null)
                    send(ride, upkeep, broken ? kFixRidesTask : kInspectRidesTask);
            }

        for (const entt::entity e : sorted(staff_view))
        {
            auto [staff, stride, pose, dice] = staff_view.get(e);
            // An inspector on a visit walks, queues and rides as a guest (above); let go, back on their rounds.
            if (r.all_of<Needs>(e))
            {
                if (r.get<Errand>(e).kind != ErrandKind::Wandering)
                    continue;
                r.remove<Needs, Mood, Wallet, Thrill, Crossing>(e);
                r.get<Errand>(e) = { };
                stride.route.clear();
            }
            if (stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                continue;
            // Their path taken up from under them: off the job in hand, they stand lost a while, then make
            // straight for the nearest path.
            if (paths.nodes()[stride.from].removed && stride.to == stride.from)
            {
                auto* lost = r.try_get<Lost>(e);
                if (lost == nullptr)
                {
                    step_out(r, e);
                    r.emplace<Lost>(e, ticks(kTuning.walking.lost_wait.value));
                }
                else if (lost->left == 0 || --lost->left == 0)
                {
                    r.remove<Lost>(e);
                    if (const auto node = path_to_rejoin(paths, pose.position))
                    {
                        r.remove<Errand, Stride>(e);
                        r.emplace<Dropped>(e, Dropped { .ground = pose.position.y, .node = *node });
                    }
                }
                continue;
            }
            Rounds* rounds = staff.role == StaffRole::Inspector ? &r.get_or_emplace<Rounds>(e) : nullptr;
            if (rounds != nullptr)
                rounds->gap.value -= dt;
            if (staff.ride != entt::null && !r.valid(staff.ride))
                staff.ride = entt::null;
            // An entertainer is at work while there are guests near to entertain.
            staff::care_for(staff, staff.role == StaffRole::Entertainer ? guests_near(pose.position) : (staff.busy.value > 0 && staff.job != StaffJob::Rest) || staff.ride != entt::null, dt);
            if (staff.think.value -= dt; staff.think.value <= 0)
            {
                staff.think = kTuning.staff.think;
                for (const ThoughtKind kind : staff::staff_thoughts(staff))
                {
                    think(r, e, kind, entt::null, now);
                    if (kind == ThoughtKind::StaffWantToQuit)
                        r.ctx().emplace<Findings>().found.emplace_back(Finding::WantsToQuit, e);
                }
                if (staff::quits(staff))
                    r.ctx().emplace<Findings>().found.emplace_back(Finding::Quit, e);
            }
            // A lazy one, with nothing to do, stops where they are now and then.
            if (staff.busy.value == 0 && staff.ride == entt::null && random::chance(dice.state, staff::rest_chance(staff) * dt))
            {
                staff.busy.value = staff.job_length.value = staff::kRestSeconds;
                staff.job = StaffJob::Rest;
            }
            if (staff.busy.value > 0)
            {
                staff.busy.value = std::max(staff.busy.value - dt, 0.0f);
                if (staff.busy.value == 0)
                    staff.job = StaffJob::None;
                if (staff.busy.value == 0 && staff.ride != entt::null)
                {
                    // Done at the ride: repaired and open again, or inspected.
                    auto& upkeep = r.get<Upkeep>(staff.ride);
                    ++(upkeep.broken ? staff.fixed : staff.inspected);
                    if (upkeep.broken)
                        r.get<Attraction>(staff.ride).open = true;
                    upkeep.reliability.value = std::min(upkeep.reliability.value + (upkeep.broken ? kTuning.wear.repair_gain.value : kTuning.wear.inspect_gain.value), 1.0f);
                    upkeep.broken = false;
                    upkeep.kind = Breakdown::None;
                    upkeep.since_inspection.value = 0;
                    upkeep.called = false;
                    upkeep.mechanic = entt::null;
                    // A changing room's tank filled up again, the park paying for the fluid.
                    if (auto* tank = r.try_get<PoolTank>(staff.ride))
                    {
                        const auto price = kTuning.pools.refill[std::to_underlying(tank->fluid)].value;
                        r.ctx().emplace<Supplies>().paid[std::to_underlying(park::LedgerLine::Other)].value += std::llround(static_cast<double>(price) * (1 - tank->left.value));
                        tank->left.value = 1;
                    }
                    staff.ride = entt::null;
                }
                continue;
            }
            float left = 0;
            if (stride.from != stride.to)
            {
                const float length = step_length(paths, stride);
                stride.progress.value += length > 0 ? stride.speed.value * dt / length : 1;
                if (stride.progress.value < 1)
                {
                    place(paths, stride, pose);
                    continue;
                }
                left = (stride.progress.value - 1) * length;
            }
            const NodeId came = stride.from;
            const NodeId at = stride.to;
            const Patrol* patrol = r.try_get<Patrol>(e);
            if (patrol != nullptr && patrol->tiles.empty())
                patrol = nullptr; // On a zone not painted yet: the whole park
            // Outside their patrol, after a job elsewhere: a way back to its nearest tile, kept in the route.
            // shortcut: nearest as the crow flies, not by the paths; fine while areas are compact.
            const auto back_to = [&](const Patrol& area, NodeId from) {
                if (!stride.route.empty())
                    return true;
                NodeId home = kNoNode;
                float best = std::numeric_limits<float>::infinity();
                for (NodeId n = 0; n < paths.nodes().size(); ++n)
                    if (!paths.nodes()[n].queue && !paths.nodes()[n].removed && area.covers(paths.nodes()[n].tile))
                        if (const float d = glm::distance(paths.centre(n), paths.centre(from)); d < best)
                        {
                            best = d;
                            home = n;
                        }
                if (auto way = home != kNoNode ? paths.route(from, home, Walker::Staff) : std::nullopt; way && !way->empty())
                    stride.route = std::move(*way);
                return !stride.route.empty();
            };
            const bool inside = patrol == nullptr || patrol->covers(paths.nodes()[at].tile);
            const bool tidies = staff.role == StaffRole::Handyman && inside;
            const bool sweeps = tidies && (staff.tasks & kSweepTask) != 0;
            const bool mends = tidies && (staff.tasks & kLeaveBrokenTask) == 0;
            const bool empties = tidies && (staff.tasks & kEmptyBinsTask) != 0;
            const bool waters = tidies && (staff.tasks & kWaterGardensTask) != 0;
            const auto job = [&] {
                if (sweeps && litter_on(at) > 0)
                {
                    auto& here = litter_at[at];
                    r.destroy(here.back());
                    here.pop_back();
                    ++staff.swept;
                    staff.job = StaffJob::Sweep;
                    return kTuning.handymen.sweep.value;
                }
                for (const entt::entity i : items(at))
                {
                    auto& item = r.get<PathItem>(i);
                    if (mends && item.broken)
                    {
                        item.broken = false;
                        ++staff.mended;
                        staff.job = StaffJob::Mend;
                        return kTuning.handymen.mend.value;
                    }
                    if (auto* bin = r.try_get<Bin>(i); empties && bin != nullptr && bin->contents > 0 && bin->contents >= kTuning.handymen.empty_at.value * bin->capacity)
                    {
                        bin->contents = 0;
                        ++staff.emptied;
                        staff.job = StaffJob::EmptyBin;
                        return kTuning.handymen.empty.value;
                    }
                }
                // Only a bed gone dry is watered, and it is filled to the brim.
                for (auto [b, bed] : beds.each())
                    if (waters && bed.node == at && bed.water <= 0 && (bed.handyman == entt::null || bed.handyman == e))
                    {
                        bed.water = bed.capacity;
                        bed.handyman = entt::null;
                        ++staff.watered;
                        staff.job = StaffJob::Water;
                        return kTuning.handymen.water.value;
                    }
                return 0.0f;
            };
            stride.from = at;
            stride.lane_from = stride.lane_to;
            // A free inspector takes their next task. For a kind of attraction, the first in the park's order
            // that is open, has a way to its entrance and was not looked at lately: a stall's prices they judge
            // as they set off, against what guests pay for each thing today, then they visit it as a guest.
            // For staff, the first worker they can reach, whom they go after and watch.
            if (rounds != nullptr && rounds->gap.value <= 0 && rounds->watching == entt::null)
            {
                rounds->gap = kTuning.inspector.gap;
                std::erase_if(rounds->seen, [&](const auto& seen) { return !r.valid(seen.first) || now - seen.second >= kTuning.inspector.again.value; });
                constexpr int kTasks = 8;
                std::int32_t task = 0;
                for (int k = 0; k < kTasks && task == 0; ++k)
                    if (const std::int32_t bit = 1 << ((rounds->task + k) % kTasks); (staff.tasks & bit) != 0)
                        task = bit;
                if (task != 0)
                    rounds->task = static_cast<std::uint8_t>((std::countr_zero(static_cast<std::uint32_t>(task)) + 1) % kTasks);
                const auto been = [&](entt::entity x) { return std::ranges::contains(rounds->seen, x, &std::pair<entt::entity, float>::first); };
                // With the food and drink, whatever many of the guests want most.
                if (task == kRefreshmentsTask)
                {
                    std::array<std::size_t, 4> wanting { }; // Hungry, thirsty, tired, bursting
                    std::size_t guests = 0;
                    for (const auto [g, needs, mood, visitor] : r.view<const Needs, const Mood, const Visitor>(entt::exclude<Left>).each())
                    {
                        ++guests;
                        wanting[0] += needs.hunger.value >= kTuning.needs.want.value ? 1 : 0;
                        wanting[1] += needs.thirst.value >= kTuning.needs.want.value ? 1 : 0;
                        wanting[2] += mood.energy.value < kTuning.facilities.tired.value ? 1 : 0;
                        wanting[3] += needs.bladder.value >= kTuning.needs.want.value ? 1 : 0;
                    }
                    const auto most = std::ranges::max_element(wanting);
                    if (static_cast<float>(*most) > kTuning.inspector.crowd.value * static_cast<float>(guests) && std::ranges::count(wanting, *most) == 1)
                        tell(r, e, static_cast<Finding>(std::to_underlying(Finding::Hungry) + (most - wanting.begin())), entt::null, now);
                }
                // With the rides, whether many of the attractions that can break are broken down.
                if (task == kTrackedRidesTask || task == kFlatRidesTask)
                {
                    std::size_t breakable = 0;
                    std::size_t broken = 0;
                    for (const auto [a, upkeep] : r.view<const Upkeep>().each())
                        if (r.all_of<CycleRide>(a) || upkeep.break_in.value >= 0)
                        {
                            ++breakable;
                            broken += upkeep.broken ? 1 : 0;
                        }
                    if (broken > 1 && static_cast<float>(broken) >= kTuning.inspector.breakdowns.value * static_cast<float>(breakable))
                        tell(r, e, Finding::Breakdowns, entt::null, now);
                }
                bool visiting = false;
                for (const entt::entity w : task == kStaffTask ? sorted(staff_view) : std::vector<entt::entity> { })
                {
                    const StaffRole role = r.get<Staff>(w).role;
                    // Not other inspectors, nor those at posts off the paths or held by the player.
                    if (w == e || role == StaffRole::Inspector || role == StaffRole::Vendor || role == StaffRole::Keeper || r.any_of<Held, Dropped>(w) || been(w))
                        continue;
                    auto way = paths.route(at, r.get<Stride>(w).to, Walker::Staff);
                    if (!way)
                    {
                        tell(r, e, Finding::NoPathToStaff, w, now);
                        continue;
                    }
                    rounds->seen.emplace_back(w, now);
                    rounds->watching = w;
                    rounds->chased_from = now;
                    rounds->watched_from = -1;
                    stride.route = std::move(*way);
                    break;
                }
                for (const entt::entity a : task != 0 && task != kStaffTask ? sorted(r.view<const Attraction>()) : std::vector<entt::entity> { })
                {
                    const Attraction& attraction = r.get<Attraction>(a);
                    const auto* shop = r.try_get<Shop>(a);
                    const bool refreshments = shop != nullptr && (shop->product == Product::Food || shop->product == Product::Drink);
                    const bool rides = r.all_of<CycleRide>(a) && task == (r.all_of<Trains>(a) ? kTrackedRidesTask : kFlatRidesTask);
                    const bool of_task = task == kRefreshmentsTask ? refreshments : task == kSouvenirsTask ? shop != nullptr && shop->product == Product::Umbrella : task == kPoolsTask ? r.all_of<PoolDoor>(a) : task == kVistasTask ? attraction.off_theme : rides;
                    if (!of_task || been(a))
                        continue;
                    // A broken ride: whether a mechanic is near.
                    if (const auto* upkeep = r.try_get<Upkeep>(a); upkeep != nullptr && upkeep->broken)
                    {
                        bool near = attraction.entrance == kNoNode;
                        for (const auto [m, worker, walk, at_m, _] : staff_view.each())
                            near = near || (worker.role == StaffRole::Mechanic && glm::distance(at_m.position, paths.centre(attraction.entrance)) <= kTuning.inspector.mechanic_near.value);
                        if (!near)
                            tell(r, e, Finding::NoMechanic, a, now);
                        continue;
                    }
                    if (!attraction.open)
                        continue;
                    // Rides and stalls that do not fit their scenery are told from afar.
                    if (task == kVistasTask)
                    {
                        rounds->seen.emplace_back(a, now);
                        tell(r, e, Finding::OffTheme, a, now);
                        break;
                    }
                    // A lazy river or pool slide has no entrance on the paths: swimmers get on from its pools.
                    if (attraction.entrance == kNoNode)
                    {
                        if (r.any_of<River, ClimbSlide>(a) || pool_ride_pool(world, a))
                            continue;
                        rounds->seen.emplace_back(a, now);
                        tell(r, e, Finding::NoPath, a, now);
                        continue;
                    }
                    auto way = paths.route(at, attraction.entrance, Walker::Guest);
                    if (!way)
                        continue;
                    rounds->seen.emplace_back(a, now);
                    if (shop != nullptr)
                    {
                        bool dear = false;
                        bool cheap = false;
                        const auto judge = [&](Money price, Money worth) {
                            if (worth.value <= 0)
                                return;                               // Nothing to judge it by
                            dear = dear || price.value > worth.value; // Guests would not pay it
                            cheap = cheap || static_cast<float>(price.value) <= kTuning.eating.good_value.value * static_cast<float>(worth.value);
                        };
                        const auto flat = [](Product p) { return p == Product::Food ? kTuning.eating.food_worth : p == Product::Drink ? kTuning.eating.drink_worth : Money { }; };
                        if (shop->menu.empty())
                            judge(attraction.price, flat(shop->product));
                        for (const Ware& w : shop->menu)
                            judge(w.price, worth_of(w, flat(w.product), weather.temperature.value));
                        if (dear)
                            tell(r, e, Finding::TooDear, a, now);
                        if (cheap)
                            tell(r, e, Finding::TooCheap, a, now);
                    }
                    r.emplace_or_replace<Needs>(e);
                    r.emplace_or_replace<Mood>(e);
                    r.emplace_or_replace<Wallet>(e);
                    r.get<Errand>(e) = { ErrandKind::Heading, a, 0 };
                    stride.route = std::move(*way);
                    stride.to = at;
                    stride.progress.value = 1;
                    place(paths, stride, pose);
                    visiting = true;
                    break;
                }
                if (visiting)
                    continue;
            }
            if (rounds != nullptr && rounds->watching != entt::null)
            {
                // After a worker along the paths, then a while beside them; what they saw told as they go back to
                // their rounds. They give up on one they cannot catch.
                const entt::entity w = rounds->watching;
                const bool there = r.valid(w) && r.all_of<Staff, Pose, Stride>(w);
                const bool near = there && (glm::distance(pose.position, r.get<Pose>(w).position) <= kTuning.inspector.watch_near.value || r.get<Stride>(w).to == at);
                if (near && rounds->watched_from < 0)
                    rounds->watched_from = now;
                const bool watched = rounds->watched_from >= 0 && now - rounds->watched_from >= kTuning.inspector.watch.value;
                if (watched && there)
                {
                    const Staff& worker = r.get<Staff>(w);
                    if (worker.laziness.value > kTuning.inspector.lazy.value)
                        tell(r, e, Finding::Lazy, w, now);
                    if (worker.happiness.value < kTuning.inspector.unhappy.value)
                        tell(r, e, Finding::Unhappy, w, now);
                }
                if (!near && there && !watched && stride.route.empty())
                    if (auto way = paths.route(at, r.get<Stride>(w).to, Walker::Staff))
                        stride.route = std::move(*way);
                const bool chasing = there && !watched && !near && !stride.route.empty() && (rounds->watched_from >= 0 || now - rounds->chased_from < kTuning.inspector.chase.value);
                if (chasing)
                {
                    stride.to = stride.route.front();
                    stride.route.erase(stride.route.begin());
                    stride.progress.value = 0;
                    stride.lane_to = new_lane(dice, stride.lane_from);
                }
                else
                {
                    if (!near || watched || !there)
                        rounds->watching = entt::null;
                    stride.route.clear();
                    stride.to = at;
                    stride.progress.value = 1;
                }
            }
            else if (staff.ride != entt::null)
            {
                // On the way to a ride, or there and setting to work.
                if (!stride.route.empty())
                {
                    stride.to = stride.route.front();
                    stride.route.erase(stride.route.begin());
                    stride.progress.value = 0;
                    stride.lane_to = new_lane(dice, stride.lane_from);
                }
                else
                {
                    const bool broken = r.get<Upkeep>(staff.ride).broken;
                    staff.busy.value = staff.job_length.value = (broken ? kTuning.wear.repair.value : kTuning.wear.inspect.value) * staff::work_slowdown(staff);
                    staff.job = broken ? StaffJob::Fix : StaffJob::Inspect;
                    stride.to = at;
                    stride.progress.value = 1;
                }
            }
            else if (const float busy = job(); busy > 0)
            {
                staff.busy.value = staff.job_length.value = busy * staff::work_slowdown(staff);
                stride.to = at;
                stride.progress.value = 1;
            }
            else if (staff.role == StaffRole::Guard && random::chance(dice.state, staff::kWatchChance))
            {
                staff.busy.value = staff.job_length.value = staff::kWatchSeconds;
                staff.job = StaffJob::Watch;
                stride.to = at;
                stride.progress.value = 1;
            }
            else if ([&] {
                         // A free handyman makes for the nearest dry bed in reach no one else is going to, and keeps
                         // their claim on it until they water it.
                         if (!waters)
                             return false;
                         entt::entity mine = entt::null;
                         for (const auto [b, bed] : beds.each())
                             if (bed.handyman == e)
                                 mine = b;
                         if (mine == entt::null)
                         {
                             float best = kTuning.handymen.garden_reach;
                             for (const auto [b, bed] : beds.each())
                                 if (bed.water <= 0 && bed.handyman == entt::null && bed.node < paths.nodes().size() && !paths.nodes()[bed.node].removed && (patrol == nullptr || patrol->covers(paths.nodes()[bed.node].tile)))
                                     if (const float d = glm::distance(paths.centre(bed.node), paths.centre(at)); d <= best)
                                     {
                                         best = d;
                                         mine = b;
                                     }
                             if (mine == entt::null)
                                 return false;
                             stride.route.clear();
                         }
                         auto& bed = r.get<FlowerBed>(mine);
                         // shortcut: a bed no path reaches is routed to again at each step; keep a failed one aside
                         // for a while if parks with many show up slow.
                         if (stride.route.empty() || stride.route.back() != bed.node)
                         {
                             auto way = paths.route(at, bed.node, Walker::Staff);
                             stride.route = way ? std::move(*way) : std::vector<NodeId> { };
                         }
                         bed.handyman = stride.route.empty() ? entt::null : e;
                         return !stride.route.empty();
                     }())
            {
                stride.to = stride.route.front();
                stride.route.erase(stride.route.begin());
                stride.progress.value = 0;
                stride.lane_to = new_lane(dice, stride.lane_from);
            }
            else if (!inside && back_to(*patrol, at))
            {
                stride.to = stride.route.front();
                stride.route.erase(stride.route.begin());
                stride.progress.value = 0;
                stride.lane_to = new_lane(dice, stride.lane_from);
            }
            else
            {
                stride.route.clear();
                // A handyman makes for the tile beside them with the most to do, litter or broken things; the
                // rest of the time staff wander. No way back to a patrol off the paths: wander the park as if
                // they had none.
                const Patrol* keep_to = inside ? patrol : nullptr;
                NodeId next = wander(paths, at, came != at ? came : kNoNode, dice, Walker::Staff, keep_to);
                const auto work_on = [&](NodeId n) { return (sweeps ? litter_on(n) : 0) + (mends ? static_cast<std::size_t>(std::ranges::count_if(items(n), [&](entt::entity i) { return r.get<PathItem>(i).broken; })) : 0); };
                if (sweeps || mends)
                    for (const NodeId n : paths.nodes()[at].next)
                        if (n != kNoNode && !paths.nodes()[n].queue && paths.may_step(at, n, Walker::Staff) && (keep_to == nullptr || keep_to->covers(paths.nodes()[n].tile)) && work_on(n) > work_on(next))
                            next = n;
                stride.to = next;
                stride.progress.value = 0;
                stride.lane_to = next != at ? new_lane(dice, stride.lane_from) : stride.lane_from;
            }
            walk_on(paths, stride, left);
            place(paths, stride, pose);
        }
        // They walk after everyone else has turned this tick; those let go face as they are put down.
        for (const entt::entity e : staff_view)
            if (!r.any_of<Held, Dropped>(e))
                face_moved(staff_view.get<Pose>(e), dt);
    }

} // namespace rct3::simulation::staff
