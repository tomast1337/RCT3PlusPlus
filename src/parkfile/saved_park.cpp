// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/saved_park.hpp"

#include "core/types.hpp"
#include "parkfile/binary.hpp"
#include "simulation/enjoyment.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <map>
#include <unordered_set>

namespace rct3::parkfile
{

    using namespace detail;

    namespace
    {
        /// Every chunk, by tag and the member that holds it; all at version 1.
        template<typename F> void each_chunk(auto& park, F&& f)
        {
            f("BASE", park.base);
            f("SUMM", park.summary);
            f("TERR", park.grid);
            f("PATH", park.paths);
            f("OBJS", park.objects);
            f("BINS", park.bins);
            f("SIGN", park.signs);
            f("RIDE", park.rides);
            f("PARK", park.progress);
            f("VIEW", park.camera);
        }
        constexpr u32 kChunks = 10;
    } // namespace

    std::vector<save::model::PlacedObject> kept_objects(std::span<const save::model::PlacedObject> objects, usize own, const std::unordered_set<u64>& placed, std::span<const save::model::PathTile> paths, const save::model::RideModel& rides)
    {
        std::unordered_set<u64> built = placed;
        // A station piece's platform model is owned by its platform record, which the ride keeps.
        std::unordered_set<u64> platforms;
        for (const save::model::PathTile& p : paths)
        {
            if (p.elevation)
                built.insert(p.elevation->scenery_ref);
        }
        for (const save::model::Ride& ride : rides.rides)
        {
            if (ride.track)
                for (const save::model::TrackPiece& piece : ride.track->pieces)
                {
                    built.insert(piece.scenery);
                    if (piece.platform)
                        platforms.insert(piece.platform->id);
                }
            if (ride.base.spot)
                built.insert(ride.base.spot->object);
            for (const save::model::Station& station : ride.stations)
                for (const auto& spot : { station.entrance_spot, station.exit_spot })
                    if (spot)
                        built.insert(spot->object);
        }
        for (const save::model::Stall& stall : rides.stalls)
        {
            if (stall.base.spot)
                built.insert(stall.base.spot->object);
        }
        std::vector<save::model::PlacedObject> kept;
        for (usize i = 0; i < objects.size(); ++i)
        {
            if (i < own || built.contains(objects[i].id) || platforms.contains(objects[i].owner_ref))
                kept.push_back(objects[i]);
        }
        return kept;
    }

    save::model::Cash kept_cash(const simulation::Wallet& wallet)
    {
        const auto clamp_i32 = [](units::Money m) { return static_cast<i32>(std::clamp<i64>(m.value, std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max())); };
        return { .bank = clamp_i32(wallet.bank), .current = clamp_i32(wallet.cash), .withdrawn = clamp_i32(wallet.withdrawn), .spent_drink = clamp_i32(wallet.spent_drink), .spent_food = clamp_i32(wallet.spent_food), .spent_park_entry = clamp_i32(wallet.spent_entry), .spent_rides = clamp_i32(wallet.spent_rides), .spent_souvenirs = clamp_i32(wallet.spent_souvenirs), .total_spent = clamp_i32(wallet.spent) };
    }

    SavedPeople keep_people(const simulation::SimWorld& world, save::model::PeopleModel model, const std::unordered_map<entt::entity, i32>& looks)
    {
        using namespace simulation;
        const entt::registry& r = world.registry;
        SavedPeople out;
        const auto look = [&](entt::entity e, usize count) {
            const auto found = looks.find(e);
            return found != looks.end() && found->second >= 0 && static_cast<usize>(found->second) < count ? std::optional(found->second) : std::nullopt;
        };
        std::unordered_map<entt::entity, i32> parties;
        std::vector<bool> in_park(model.guests.size());
        // shortcut: queueing, riding and errands other than swimming are not kept, guests walk on from where they
        // stand; keep them once the save can name rides and path nodes that last across loading.
        for (const auto [e, visitor, pose, needs, mood, wallet] : r.view<const Visitor, const Pose, const Needs, const Mood, const Wallet>(entt::exclude<Left>).each())
        {
            // Those off the paths (on a ride) where they were going on them, so they walk again on loading.
            const auto* stride = r.try_get<Stride>(e);
            // A swimmer by the room they came in at, which a pool keeps across loading.
            const auto* swimmer = r.try_get<Swimmer>(e);
            const auto* errand = r.try_get<Errand>(e);
            const auto* door = swimmer != nullptr && errand != nullptr && errand->kind == ErrandKind::Swimming && r.valid(errand->target) ? r.try_get<PoolDoor>(errand->target) : nullptr;
            const bool off = door == nullptr && !world.paths.nearest(pose.position) && stride != nullptr && stride->to.value < world.paths.nodes().size();
            SavedGuest& g = out.guests.emplace_back(SavedGuest { .look = look(e, model.guests.size()), .visitor = visitor, .position = off ? world.paths.centre(stride->to) : pose.position, .heading = pose.heading.value, .needs = needs, .mood = mood, .cash = wallet.cash.value });
            if (const auto* taste = r.try_get<Taste>(e))
                g.taste = *taste;
            // Those going home are only told by where they head.
            if (const auto* outside = r.try_get<Outside>(e))
                g.outside = *outside;
            else if (errand != nullptr && errand->kind == ErrandKind::Heading && r.valid(errand->target) && r.all_of<GuestSpawn>(errand->target))
                g.outside = Outside { Outside::Way::Home };
            if (const auto* passport = r.try_get<Passport>(e))
                g.passport = *passport;
            g.map = r.all_of<ParkMap>(e);
            g.umbrella = r.all_of<Umbrella>(e);
            if (door != nullptr)
                g.swim = SavedSwim { .room_col = door->col, .room_row = door->row, .swimmer = *swimmer };
            if (const auto* member = r.try_get<Member>(e); member != nullptr && r.valid(member->party))
            {
                g.party.emplace(parties.try_emplace(member->party, static_cast<i32>(parties.size())).first->second);
                g.leader = r.get<Party>(member->party).leader == e;
            }
            if (g.look)
            {
                in_park[static_cast<usize>(*g.look)] = true;
                // The rest of their money goes with their page, where the original keeps it; so does their favourite.
                save::model::Guest& page = model.guests[static_cast<usize>(*g.look)];
                page.cash = kept_cash(wallet);
                if (const auto* favourite = r.try_get<Favourite>(e); favourite != nullptr && favourite->saved != 0)
                {
                    page.favourite_ride = { favourite->saved };
                    page.favourite_ride_rating = favourite->score;
                }
            }
        }
        for (usize i = 0; i < model.guests.size(); ++i)
            model.guests[i].in_park = in_park[i];
        for (const auto [e, staff, pose] : r.view<const Staff, const Pose>().each())
        {
            SavedWorker& w = out.staff.emplace_back(
                SavedWorker { .look = look(e, model.staff.size()), .role = staff.role, .wage = staff.wage.value, .wage_level = staff.wage_level, .training = staff.training, .warnings = staff.warnings, .happiness = staff.happiness.value, .laziness = staff.laziness.value, .position = pose.position, .heading = pose.heading.value, .mended = staff.mended, .think = staff.think.value, .miserable = staff.miserable, .job = staff.job, .busy = staff.busy, .job_length = staff.job_length, .load = staff.load });
            // Only a mechanic at work on a ride keeps it; one on the way is sent again.
            if (staff.ride != entt::null && staff.busy.value > 0)
                if (const auto* ride = r.try_get<Attraction>(staff.ride))
                    w.ride = ride->saved;
            if (const auto* patrol = r.try_get<staff::Patrol>(e))
                w.patrol = *patrol;
        }
        out.model = std::move(model);
        if (const auto* passport = r.ctx().find<PassportRules>())
            out.passport = *passport;
        return out;
    }

    std::vector<entt::entity> place_guests(simulation::SimWorld& world, const SavedPeople& people)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::vector<entt::entity> out;
        for (const SavedGuest& g : people.guests)
        {
            const entt::entity e = out.emplace_back(r.create());
            r.emplace<Visitor>(e, g.visitor);
            r.emplace<Pose>(e, g.position, g.heading, g.position, g.heading);
            r.emplace<Needs>(e, g.needs);
            r.emplace<Mood>(e, g.mood);
            Wallet& wallet = r.emplace<Wallet>(e, g.cash);
            if (g.look && static_cast<usize>(*g.look) < people.model.guests.size())
            {
                const save::model::Guest& page = people.model.guests[static_cast<usize>(*g.look)];
                const save::model::Cash& c = page.cash;
                wallet = { .cash = { g.cash }, .bank = { c.bank }, .withdrawn = { c.withdrawn }, .spent = { c.total_spent }, .spent_entry = { c.spent_park_entry }, .spent_rides = { c.spent_rides }, .spent_food = { c.spent_food }, .spent_drink = { c.spent_drink }, .spent_souvenirs = { c.spent_souvenirs } };
                if (page.favourite_ride)
                    r.emplace<Favourite>(e, Favourite { .ride = entt::null, .saved = page.favourite_ride.id, .score = page.favourite_ride_rating });
            }
            if (g.taste)
                r.emplace<Taste>(e, *g.taste);
            if (g.outside)
                r.emplace<Outside>(e, *g.outside);
            if (g.passport)
                r.emplace<Passport>(e, *g.passport);
            if (g.map)
                r.emplace<ParkMap>(e);
            if (g.umbrella)
                r.emplace<Umbrella>(e);
        }
        return out;
    }

    void place_parties(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::map<i32, Party> parties; // In order, so the entities come out the same each load
        for (usize i = 0; i < people.guests.size() && i < guests.size(); ++i)
        {
            const SavedGuest& g = people.guests[i];
            if (!g.party || !r.all_of<Errand>(guests[i]) || r.all_of<Member>(guests[i]))
                continue;
            Party& party = parties[*g.party];
            party.members.push_back(guests[i]);
            if (g.leader)
                party.leader = guests[i];
        }
        for (auto& [_, party] : parties)
        {
            if (party.members.size() < 2)
                continue;
            if (party.leader == entt::null)
                party.leader = party.members.front();
            const entt::entity e = r.create();
            for (const entt::entity m : party.members)
            {
                r.emplace<Member>(m, e);
                if (auto& errand = r.get<Errand>(m); m != party.leader && errand.kind == ErrandKind::Wandering)
                    errand = { ErrandKind::Following, entt::null, 0 };
            }
            r.emplace<Party>(e, std::move(party));
        }
    }

    usize place_swimmers(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        usize placed = 0;
        for (usize i = 0; i < people.guests.size() && i < guests.size(); ++i)
        {
            const SavedGuest& g = people.guests[i];
            if (!g.swim || !r.all_of<Errand, Pose>(guests[i]))
                continue;
            for (const auto [room, door] : r.view<const PoolDoor>().each())
                if (door.col == g.swim->room_col && door.row == g.swim->room_row)
                {
                    r.get<Errand>(guests[i]) = { .kind = ErrandKind::Swimming, .target = room };
                    auto& swimmer = r.emplace_or_replace<Swimmer>(guests[i], g.swim->swimmer);
                    // The slide they were headed for and the room they rode from are this run's entities, others
                    // once loaded; they swim on and pick again.
                    swimmer.slide = swimmer.room = entt::null;
                    auto& pose = r.get<Pose>(guests[i]);
                    pose.position = pose.was = g.position;
                    ++placed;
                    break;
                }
        }
        return placed;
    }

    std::vector<entt::entity> place_staff(simulation::SimWorld& world, const SavedPeople& people)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::vector<entt::entity> out(people.staff.size(), entt::null);
        for (usize i = 0; i < people.staff.size(); ++i)
        {
            const SavedWorker& w = people.staff[i];
            const auto at = world.paths.nearest(w.position);
            // A vendor stays in their booth, a keeper in their enclosure, a lifeguard in their chair.
            const bool walks = staff::walks_paths(w.role);
            if (walks && (!at || world.paths.nodes()[at->value].queue))
                continue;
            const entt::entity e = r.create();
            // The tasks the player set are on the worker's page.
            const bool paged = w.look && static_cast<usize>(*w.look) < people.model.staff.size();
            Staff staff { .role = w.role, .tasks = paged ? people.model.staff[static_cast<usize>(*w.look)].task_flags : kEveryTask, .wage = w.wage, .wage_level = w.wage_level, .training = w.training, .warnings = w.warnings, .happiness = w.happiness, .laziness = w.laziness };
            if (paged)
                staff::take_saved_work(staff, people.model.staff[static_cast<usize>(*w.look)]);
            staff.mended = w.mended;
            staff.think = w.think;
            staff.miserable = w.miserable;
            staff.job = w.job;
            staff.busy = w.busy;
            staff.job_length = w.job_length;
            staff.load = w.load;
            if (w.ride != 0)
                for (const auto [ride, attraction, upkeep] : r.view<const Attraction, Upkeep>().each())
                    if (attraction.saved == w.ride && upkeep.mechanic == entt::null)
                    {
                        staff.ride = ride;
                        upkeep.mechanic = e;
                        break;
                    }
            // A mechanic whose ride is gone has nothing left to work on.
            if (w.ride != 0 && staff.ride == entt::null)
                staff.busy = staff.job_length = { };
            if (staff.busy.value == 0)
                staff.job = StaffJob::None;
            r.emplace<Staff>(e, staff);
            r.emplace<Pose>(e, w.position, w.heading, w.position, w.heading);
            if (w.patrol)
                r.emplace<staff::Patrol>(e, *w.patrol);
            if (walks)
                start_walking(world, e, *at);
            out[i] = e;
        }
        return out;
    }

    SavedAnimals keep_animals(const simulation::SimWorld& world, save::model::EnclosureMap enclosures)
    {
        using namespace simulation;
        const entt::registry& r = world.registry;
        SavedAnimals out { .enclosures = std::move(enclosures) };
        // The staff in keep_people's order.
        std::map<entt::entity, i32> kept;
        for (const entt::entity e : r.view<const Staff, const Pose>())
            kept.emplace(e, static_cast<i32>(kept.size()));
        std::map<entt::entity, std::pair<i32, const KeeperDuty*>> keepers; // Of each house
        for (const auto [e, duty] : r.view<const KeeperDuty>().each())
            if (const auto found = kept.find(e); found != kept.end())
                keepers.emplace(duty.house, std::pair { found->second, &duty });
        // What a keeper is after, by its place in what is saved.
        std::map<entt::entity, i32> places;
        for (const auto [e, animal, pose] : r.view<const Animal, const Pose>().each())
        {
            places.emplace(e, static_cast<i32>(out.animals.size()));
            out.animals.push_back({ .animal = animal, .position = pose.position, .heading = pose.heading.value });
        }
        for (const auto [e, dung, pose] : r.view<const Dung, const Pose>().each())
        {
            places.emplace(e, static_cast<i32>(out.dung.size()));
            out.dung.push_back({ .enclosure = dung.enclosure, .position = pose.position });
        }
        std::map<entt::entity, i32> house_at; // Of what a keeper fills
        for (const entt::entity e : r.view<const AnimalHouse>())
            house_at.emplace(e, static_cast<i32>(house_at.size()));
        for (const auto [e, house] : r.view<const AnimalHouse>().each())
        {
            SavedAnimalHouse& saved = out.houses.emplace_back(SavedAnimalHouse { .house = house });
            const auto keeper = keepers.find(e);
            if (keeper == keepers.end())
                continue;
            const KeeperDuty& duty = *keeper->second.second;
            const auto& at = duty.job == KeeperJob::Filling ? house_at : places;
            const auto target = at.find(duty.target);
            saved.keeper.emplace(keeper->second.first);
            saved.job = duty.job;
            saved.goal = duty.goal;
            saved.target = target != at.end() ? std::optional(target->second) : std::nullopt;
            saved.busy = duty.busy;
        }
        return out;
    }

    void place_animals(simulation::SimWorld& world, const SavedAnimals& animals, std::span<const entt::entity> staff)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::vector<entt::entity> homes;
        for (const SavedAnimalHouse& saved : animals.houses)
        {
            homes.push_back(r.create());
            r.emplace<AnimalHouse>(homes.back(), saved.house);
        }
        std::vector<entt::entity> placed;
        for (const SavedAnimal& saved : animals.animals)
        {
            const entt::entity e = placed.emplace_back(r.create());
            r.emplace<Animal>(e, saved.animal);
            r.emplace<Pose>(e, saved.position, saved.heading, saved.position, saved.heading);
            r.emplace<Dice>(e, dice_for(world, e));
        }
        std::vector<entt::entity> dropped;
        for (const SavedDung& saved : animals.dung)
        {
            const entt::entity e = dropped.emplace_back(r.create());
            r.emplace<Dung>(e, Dung { saved.enclosure });
            r.emplace<Pose>(e, saved.position, 0.0f, saved.position, 0.0f);
        }
        for (usize i = 0; i < animals.houses.size(); ++i)
        {
            const SavedAnimalHouse& saved = animals.houses[i];
            const entt::entity keeper = saved.keeper && static_cast<usize>(*saved.keeper) < staff.size() ? staff[static_cast<usize>(*saved.keeper)] : entt::null;
            if (keeper == entt::null || !r.valid(keeper))
                continue;
            // A job after something no longer there starts over.
            const std::span<const entt::entity> targets = saved.job == KeeperJob::Clearing ? std::span(dropped) : saved.job == KeeperJob::Catching ? std::span(placed) : saved.job == KeeperJob::Filling ? std::span(homes) : std::span<const entt::entity> { };
            const entt::entity target = saved.target && static_cast<usize>(*saved.target) < targets.size() ? targets[static_cast<usize>(*saved.target)] : entt::null;
            const bool lost = target == entt::null && saved.job != KeeperJob::Idle && saved.job != KeeperJob::Strolling;
            r.emplace_or_replace<KeeperDuty>(keeper, lost ? KeeperDuty { .house = homes[i] } : KeeperDuty { .house = homes[i], .job = saved.job, .goal = saved.goal, .target = target, .busy = units::Seconds { saved.busy } });
            r.emplace_or_replace<Dice>(keeper, dice_for(world, keeper));
        }
    }

    std::vector<SavedLitter> keep_litter(const simulation::SimWorld& world)
    {
        std::vector<SavedLitter> out;
        for (const auto [e, litter] : world.registry.view<const simulation::Litter>().each())
            if (litter.node.value < world.paths.nodes().size())
                out.push_back({ .position = world.paths.centre(litter.node), .kind = litter.kind, .age = litter.age, .vomit = litter.vomit });
        return out;
    }

    usize place_litter(simulation::SimWorld& world, std::span<const SavedLitter> litter)
    {
        usize placed = 0;
        for (const SavedLitter& l : litter)
            if (const auto at = world.paths.nearest(l.position))
            {
                world.registry.emplace<simulation::Litter>(world.registry.create(), simulation::Litter { .node = *at, .kind = l.kind, .age = l.age, .vomit = l.vomit });
                ++placed;
            }
        return placed;
    }

    std::vector<SavedQueue> keep_queues(const simulation::SimWorld& world)
    {
        using namespace simulation;
        const entt::registry& r = world.registry;
        std::unordered_map<entt::entity, i32> numbers;
        for (const entt::entity e : r.view<const Visitor, const Pose, const Needs, const Mood, const Wallet>(entt::exclude<Left>)) // As keep_people goes
            numbers.emplace(e, static_cast<i32>(numbers.size()));
        std::vector<SavedQueue> out;
        for (const auto [e, cycle, attraction] : r.view<const CycleRide, const Attraction>().each())
        {
            if (attraction.saved == 0)
                continue;
            for (usize station = 0; station <= cycle.stops.size(); ++station)
            {
                SavedQueue queue { .ride = attraction.saved, .station = static_cast<u8>(std::min<usize>(station, 255)) };
                for (const entt::entity g : station == 0 ? cycle.queue : cycle.stops[station - 1].queue)
                    if (const auto number = numbers.find(g); number != numbers.end())
                        if (const auto* errand = r.try_get<Errand>(g))
                            queue.guests.push_back({ .guest = number->second, .queued = errand->queued });
                if (!queue.guests.empty())
                    out.push_back(std::move(queue));
            }
        }
        return out;
    }

    usize place_queues(simulation::SimWorld& world, std::span<const SavedQueue> queues, std::span<const entt::entity> guests)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        usize placed = 0;
        for (const SavedQueue& saved : queues)
            for (const auto [e, cycle, attraction] : r.view<CycleRide, const Attraction>().each())
            {
                if (attraction.saved != saved.ride || saved.station > cycle.stops.size())
                    continue;
                std::vector<entt::entity>& queue = saved.station == 0 ? cycle.queue : cycle.stops[saved.station - 1].queue;
                std::vector<entt::entity> ordered;
                for (const SavedQueuer& q : saved.guests)
                {
                    const entt::entity g = q.guest >= 0 && static_cast<usize>(q.guest) < guests.size() ? guests[static_cast<usize>(q.guest)] : entt::null;
                    if (g == entt::null || !std::ranges::contains(queue, g) || std::ranges::contains(ordered, g))
                        continue;
                    ordered.push_back(g);
                    r.get<Errand>(g).queued = q.queued;
                    ++placed;
                }
                for (const entt::entity g : queue)
                    if (!std::ranges::contains(ordered, g))
                        ordered.push_back(g);
                queue = std::move(ordered);
                break;
            }
        return placed;
    }

    std::vector<SavedRun> keep_runs(const simulation::SimWorld& world)
    {
        using namespace simulation;
        const entt::registry& r = world.registry;
        std::unordered_map<entt::entity, i32> numbers;
        for (const entt::entity e : r.view<const Visitor, const Pose, const Needs, const Mood, const Wallet>(entt::exclude<Left>)) // As keep_people goes
            numbers.emplace(e, static_cast<i32>(numbers.size()));
        const auto riders_of = [&](const std::vector<entt::entity>& on) {
            std::vector<SavedRider> out;
            for (const entt::entity g : on)
                if (const auto number = numbers.find(g); number != numbers.end())
                    if (const auto* errand = r.try_get<Errand>(g))
                        out.push_back({ .guest = number->second, .station = errand->station, .off = errand->off });
            return out;
        };
        std::vector<SavedRun> out;
        for (const auto [e, cycle, attraction] : r.view<const CycleRide, const Attraction>().each())
        {
            const auto* fleet = r.try_get<Trains>(e);
            // shortcut: a pool's rides keep nothing, as their riders swim on from them; keep them once swimmers'
            // rides are kept.
            const bool pool_ride = r.all_of<River>(e) || (fleet != nullptr && !fleet->trains.empty() && (fleet->trains.front().splash_out || !fleet->trains.front().drop_offs.empty()));
            const bool off_track = fleet != nullptr && std::ranges::any_of(fleet->trains, [](const Coaster& t) { return t.crashed || t.splashed || t.flight || t.lifted; });
            if (attraction.saved == 0 || pool_ride || off_track)
                continue;
            SavedRun run { .ride = attraction.saved, .phase = cycle.phase, .in_phase = cycle.in_phase, .loop = cycle.loop, .loading = cycle.loading, .since_departure = cycle.since_departure, .per_hour = cycle.per_hour, .riders = riders_of(cycle.riders) };
            for (const Stop& stop : cycle.stops)
                run.stops_loading.push_back(stop.loading);
            if (fleet != nullptr)
                for (usize t = 0; t < fleet->trains.size(); ++t)
                {
                    const Coaster& c = fleet->trains[t];
                    run.trains.push_back({ .running = c.running, .at = c.at, .travelled = c.travelled, .next_stop = static_cast<u32>(c.next_stop), .speed = c.speed, .launched = c.launched, .lean = c.lean, .lean_rate = c.lean_rate, .passed = c.passed, .push = c.push, .pushed = c.pushed, .held = c.held, .riders = t < fleet->riders.size() ? riders_of(fleet->riders[t]) : std::vector<SavedRider> { } });
                }
            out.push_back(std::move(run));
        }
        return out;
    }

    usize place_runs(simulation::SimWorld& world, std::span<const SavedRun> runs, std::span<const entt::entity> guests)
    {
        using namespace simulation;
        entt::registry& r = world.registry;
        std::unordered_map<u64, entt::entity> rides;
        for (const auto [e, cycle, attraction] : r.view<const CycleRide, const Attraction>().each())
            if (attraction.saved != 0)
                rides.emplace(attraction.saved, e);
        usize seated = 0;
        for (const SavedRun& run : runs)
        {
            const auto found = rides.find(run.ride);
            if (found == rides.end())
                continue;
            const entt::entity ride = found->second;
            auto& cycle = r.get<CycleRide>(ride);
            auto* fleet = r.try_get<Trains>(ride);
            // A ride rebuilt since, with another number of trains or stations, starts its cycle afresh.
            if ((fleet != nullptr && fleet->trains.size() != run.trains.size()) || cycle.stops.size() != run.stops_loading.size())
                continue;
            cycle.phase = run.phase;
            cycle.in_phase = run.in_phase;
            cycle.loop = run.loop;
            cycle.loading = run.loading;
            cycle.since_departure = run.since_departure;
            cycle.per_hour = run.per_hour;
            for (usize s = 0; s < cycle.stops.size(); ++s)
                cycle.stops[s].loading = run.stops_loading[s];
            const std::optional<NodeId> off = cycle.exit.or_else([&] { return r.get<Attraction>(ride).entrance; });
            // Each rider as board_saved_riders seats them: walking from where riders get off, riding.
            const auto seat = [&](const std::vector<SavedRider>& saved, std::vector<entt::entity>& on) {
                for (const SavedRider& s : saved)
                {
                    const entt::entity g = s.guest >= 0 && static_cast<usize>(s.guest) < guests.size() ? guests[static_cast<usize>(s.guest)] : entt::null;
                    const auto* errand = g == entt::null ? nullptr : r.try_get<Errand>(g);
                    // One start_guests set queueing is in a queue's list; one riding, already on.
                    if (errand == nullptr || errand->kind == ErrandKind::Riding || errand->kind == ErrandKind::Queueing || (!r.all_of<Stride>(g) && !off))
                        continue;
                    if (!r.all_of<Stride>(g))
                        start_walking(world, g, *off);
                    on.push_back(g);
                    r.get<Errand>(g) = { .kind = ErrandKind::Riding, .target = ride, .station = s.station, .off = s.off };
                    ++seated;
                }
            };
            seat(run.riders, cycle.riders);
            if (fleet == nullptr)
                continue;
            for (usize t = 0; t < fleet->trains.size(); ++t)
            {
                Coaster& c = fleet->trains[t];
                const SavedTrain& saved = run.trains[t];
                c.running = saved.running;
                c.at = c.was_at = std::clamp(saved.at, 0.0f, c.length());
                c.travelled = saved.travelled;
                c.next_stop = std::min<usize>(saved.next_stop, c.stops.size());
                c.speed = saved.speed;
                c.launched = saved.launched;
                c.lean = saved.lean;
                c.lean_rate = saved.lean_rate;
                c.passed = saved.passed;
                c.push = saved.push;
                c.pushed = saved.pushed;
                c.held = saved.held;
                fleet->riders.resize(fleet->trains.size());
                seat(saved.riders, fleet->riders[t]);
            }
        }
        return seated;
    }

    std::vector<std::byte> write_saved_park(const SavedPark& park)
    {
        Writer w;
        const bool shows = park.shows && !park.shows->empty();
        write_header(w, kChunks + (park.people ? 1 : 0) + (park.no_queue_signs.empty() ? 0 : 1) + (shows ? 1 : 0) + (park.goals ? 1 : 0) + (park.research ? 1 : 0) + (park.broken_items.empty() ? 0 : 1) + (park.waterfalls ? 1 : 0) + (park.animals ? 1 : 0) + (park.day_mode ? 1 : 0) + (park.pools ? 1 : 0) + (park.flower_beds.empty() ? 0 : 1) + (park.litter ? 1 : 0) + (park.queues.empty() ? 0 : 1) + (park.runs.empty() ? 0 : 1));
        each_chunk(park, [&](std::string_view tag, const auto& value) { put_chunk(w, tag, 1, value); });
        if (park.people)
            put_chunk(w, "PEOP", 7, *park.people);
        if (!park.no_queue_signs.empty())
            put_chunk(w, "QSGN", 1, park.no_queue_signs);
        if (shows)
            put_chunk(w, "SHOW", 1, *park.shows);
        if (park.goals)
            put_chunk(w, "GOAL", 1, *park.goals);
        if (park.research)
            put_chunk(w, "RSCH", 2, *park.research);
        if (!park.broken_items.empty())
            put_chunk(w, "BRKN", 1, park.broken_items);
        if (park.waterfalls)
            put_chunk(w, "FALL", 1, *park.waterfalls);
        if (!park.flower_beds.empty())
            put_chunk(w, "BEDS", 1, park.flower_beds);
        if (park.day_mode)
            put_chunk(w, "DAYM", 1, *park.day_mode);
        if (park.animals)
            put_chunk(w, "ANML", 1, *park.animals);
        if (park.pools)
            put_chunk(w, "POOL", 9, *park.pools);
        if (park.litter)
            put_chunk(w, "LITR", 1, *park.litter);
        if (!park.queues.empty())
            put_chunk(w, "QUEU", 1, park.queues);
        if (!park.runs.empty())
            put_chunk(w, "RUNS", 1, park.runs);
        return w.take();
    }

    std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes)
    {
        SavedPark park;
        u32 found = 0;
        auto read = read_chunks(bytes, [&](std::string_view tag, u32 version, Reader& body) -> std::expected<void, std::string> {
            std::expected<void, std::string> result;
            if (tag == "PEOP")
            {
                if (park.people || version != 7) // 2 adds the swimmers, 3 those outside the park, 4 passports, 5 the gift, 6 maps and umbrellas, 7 jobs
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.people.emplace());
            }
            if (tag == "QSGN")
            {
                if (!park.no_queue_signs.empty() || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.no_queue_signs);
            }
            if (tag == "SHOW")
            {
                if (park.shows || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                auto read = get_chunk(tag, body, park.shows.emplace());
                // A place naming no trick is dropped, so the show's places can be looked up unchecked.
                for (save::model::RideShow& show : *park.shows)
                    std::erase_if(show.order, [&](u32 t) { return t >= show.tricks.size(); });
                return read;
            }
            if (tag == "GOAL")
            {
                if (park.goals || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.goals.emplace());
            }
            if (tag == "RSCH")
            {
                if (park.research || version == 0 || version > 2) // 2 adds when each item was invented, and the focus
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                // A park saved before is opened as one saved before research was kept.
                if (version == 1)
                    return { };
                return get_chunk(tag, body, park.research.emplace());
            }
            if (tag == "BRKN")
            {
                if (!park.broken_items.empty() || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.broken_items);
            }
            if (tag == "BEDS")
            {
                if (!park.flower_beds.empty() || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.flower_beds);
            }
            if (tag == "FALL")
            {
                if (park.waterfalls || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.waterfalls.emplace());
            }
            if (tag == "DAYM")
            {
                if (park.day_mode || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                auto read = get_chunk(tag, body, park.day_mode.emplace());
                // A number with no mode keeps the park's hours.
                park.day_mode = simulation::day_mode(static_cast<i32>(*park.day_mode));
                return read;
            }
            if (tag == "ANML")
            {
                if (park.animals || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.animals.emplace());
            }
            if (tag == "POOL")
            {
                if (park.pools || version != 9) // 9 adds the ground each square dug
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.pools.emplace());
            }
            if (tag == "LITR")
            {
                if (park.litter || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.litter.emplace());
            }
            if (tag == "QUEU")
            {
                if (!park.queues.empty() || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.queues);
            }
            if (tag == "RUNS")
            {
                if (!park.runs.empty() || version != 1)
                    return std::unexpected(std::format("{}: repeated, or a version this build does not read", tag));
                return get_chunk(tag, body, park.runs);
            }
            each_chunk(park, [&](std::string_view name, auto& value) {
                if (name != tag)
                    return;
                ++found;
                if (version != 1)
                    result = std::unexpected(std::format("{}: version {} is not one this build reads", tag, version));
                else
                    result = get_chunk(tag, body, value);
            });
            return result;
        });
        if (!read)
            return std::unexpected(read.error());
        if (found != kChunks)
            return std::unexpected("not a saved park: chunks are missing or repeated");
        return park;
    }

    bool is_park_file(std::span<const std::byte> bytes)
    {
        return bytes.size() >= kMagic.size() && std::ranges::equal(bytes.first(kMagic.size()), kMagic, [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
    }

} // namespace rct3::parkfile
