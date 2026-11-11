// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/systems.hpp"

#include "park/terrain_mesh.hpp"
#include "profiling/profiler.hpp"
#include "simulation/animals.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/crowds.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/guests.hpp"
#include "simulation/holding.hpp"
#include "simulation/park/state.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff_care.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/tuning.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    NodeId entrance_of(const entt::registry& r, entt::entity ride, std::size_t station)
    {
        const CycleRide& cycle = r.get<CycleRide>(ride);
        return station > 0 && station <= cycle.stops.size() ? cycle.stops[station - 1].entrance : r.get<Attraction>(ride).entrance;
    }

    NodeId exit_of(const entt::registry& r, entt::entity ride, std::size_t station)
    {
        const CycleRide& cycle = r.get<CycleRide>(ride);
        if (station > 0 && station <= cycle.stops.size())
            return cycle.stops[station - 1].exit != kNoNode ? cycle.stops[station - 1].exit : cycle.stops[station - 1].entrance;
        return cycle.exit != kNoNode ? cycle.exit : r.get<Attraction>(ride).entrance;
    }

    void let_out(entt::registry& r, const PathNetwork& paths, entt::entity guest, entt::entity ride, std::optional<GestureKind> gesture)
    {
        const NodeId out = exit_of(r, ride, r.get<Errand>(guest).off);
        r.get<Errand>(guest) = after_ride(r, guest, ride);
        stop_at(paths, out, r.get<Stride>(guest), r.get<Pose>(guest));
        // Riding across the park, they walk on where they were going; one in a party waits for the others
        // first (phase 5).
        if (auto* crossing = r.try_get<Crossing>(guest); crossing != nullptr && crossing->ride == ride && !r.all_of<Member>(guest))
        {
            r.get<Errand>(guest) = crossing->errand;
            r.get<Stride>(guest).route = std::move(crossing->route);
            r.remove<Crossing>(guest);
        }
        auto& dice = r.get<Dice>(guest).state;
        if (gesture && random::chance(dice, kTuning.gestures.chance.value))
            r.emplace_or_replace<Gesture>(guest, *gesture, kTuning.gestures.shown.value);
        if (r.get<Mood>(guest).nausea.value >= kTuning.sickness.sick.value && random::chance(dice, kTuning.sickness.sick_after_ride.value))
            be_sick(r, guest, out);
    }

    void think(entt::registry& r, entt::entity guest, ThoughtKind kind, entt::entity about, float now)
    {
        auto& thoughts = r.get_or_emplace<Thoughts>(guest);
        if (std::ranges::any_of(thoughts.recent, [&](const Thought& t) { return t.kind == kind && t.about == about && now - t.time.value < kTuning.thoughts.rethink.value; }))
            return;
        const auto variant = static_cast<std::uint32_t>(random::next(r.get<Dice>(guest).state));
        thoughts.recent.insert(thoughts.recent.begin(), Thought { kind, about, now, variant });
        if (thoughts.recent.size() > kTuning.thoughts.kept)
            thoughts.recent.pop_back();
        ++thoughts.had;
    }

    void stop_at(const PathNetwork& paths, NodeId node, Stride& stride, Pose& pose)
    {
        stride.from = node;
        stride.to = node;
        stride.progress.value = 1;
        stride.route.clear();
        place(paths, stride, pose);
    }

    void tell(entt::registry& r, entt::entity inspector, Finding finding, entt::entity about, float now)
    {
        auto& told = r.get_or_emplace<Rounds>(inspector).told;
        std::erase_if(told, [&](const auto& t) { return now - std::get<2>(t) >= kTuning.inspector.quiet.value; });
        if (std::ranges::any_of(told, [&](const auto& t) { return std::get<0>(t) == about && std::get<1>(t) == finding; }))
            return;
        told.emplace_back(about, finding, now);
        r.ctx().emplace<Findings>().found.emplace_back(finding, about);
    }

    Gait pick_gait(AgeGroup age, const Mood& mood, const Needs& needs, random::RandomState& dice)
    {
        const auto& gaits = kTuning.gaits;
        if (mood.nausea.value >= kTuning.sickness.sick.value)
            return Gait::Queasy;
        if (needs.bladder.value >= gaits.bursting.value)
            return Gait::Bursting;
        if (mood.energy.value < gaits.weary.value)
            return Gait::Weary;
        return random::chance(dice, gaits.run[std::to_underlying(age)].value * mood.energy.value) ? Gait::Run : Gait::Walk;
    }

    Pace gait_pace(Pace walk, Share energy, Gait gait)
    {
        const auto& gaits = kTuning.gaits;
        const double by_gait = gait == Gait::Run ? gaits.run_pace.value : gait == Gait::Weary ? gaits.weary_pace.value : gait == Gait::Queasy ? gaits.queasy_pace.value : gait == Gait::Bursting ? gaits.bursting_pace.value : 1.0;
        const double by_energy = gaits.drained.value + (gaits.fresh.value - gaits.drained.value) * std::clamp(energy.value, 0.0f, 1.0f);
        return { static_cast<float>(walk.value * by_energy * by_gait) };
    }

    RideReaction react(RideReaction now, const Felt& feel, bool running, bool lifted, Share happiness, float over, const Leanings& leanings, bool front, random::RandomState& dice, float dt)
    {
        const auto& k = kTuning.reactions;
        if (!running || lifted)
            return RideReaction::Waiting;
        if (leanings.brace >= k.stoic.value && (feel.vertical > k.strain || std::abs(feel.lateral) > k.strain_across))
            return RideReaction::Strained;
        if (over > k.too_wild.value && leanings.eyes < k.hide_eyes.value && feel.slope < k.diving)
            return RideReaction::CoverEyes;
        bool hands = now == RideReaction::HandsUp && feel.vertical < k.hands_down && std::abs(feel.lateral) < k.hands_down;
        if (!hands && leanings.hands >= (front ? k.timid_front : k.timid).value)
        {
            if (happiness.value > k.thrilled.value)
                hands = random::chance(dice, k.hands_up.value * dt);
            else if (feel.vertical < k.airtime)
                hands = random::chance(dice, k.floating.value * dt);
        }
        return hands ? RideReaction::HandsUp : RideReaction::Calm;
    }

    RideReaction react_flat(RideReaction now, bool running, Share happiness, float over, const Leanings& leanings, random::RandomState& dice, float dt)
    {
        const auto& k = kTuning.reactions;
        if (!running)
            return RideReaction::Waiting;
        if (over > k.too_wild.value && leanings.eyes < k.hide_eyes.value)
            return RideReaction::CoverEyes;
        if (now == RideReaction::HandsUp)
            return now;
        const bool thrilled = happiness.value > k.thrilled.value || over > -k.suits.value;
        return thrilled && leanings.hands >= k.timid.value && random::chance(dice, k.hands_up.value * dt) ? RideReaction::HandsUp : RideReaction::Calm;
    }

    Pose between_steps(const Pose& pose, float along)
    {
        const float turn = std::remainder(pose.heading.value - pose.was_heading.value, 2 * glm::pi<float>());
        const float walked = pose.walked.value + along * glm::distance(glm::vec2(pose.was.x, pose.was.z), glm::vec2(pose.position.x, pose.position.z));
        return { glm::mix(pose.was, pose.position, along), pose.was_heading.value + turn * along, pose.was, pose.was_heading.value, walked };
    }

    std::optional<float> cannon_turn(float heading, glm::vec2 way, float most)
    {
        const float turn = std::remainder(std::atan2(way.x, way.y) - heading, 2 * glm::pi<float>());
        if (std::abs(turn) > most)
            return std::nullopt;
        return turn;
    }

    double arrivals_per_second(const Weather& weather)
    {
        double rate = kTuning.arrivals.rate.value * (weather.storm ? kTuning.arrivals.storm.value : weather.rain ? kTuning.arrivals.rain.value : 1);
        if (weather.temperature.value < kTuning.weather.cold.value)
            rate *= kTuning.arrivals.cold.value;
        else if (weather.sunny && !weather.rain)
            rate *= kTuning.arrivals.sunny.value;
        return rate;
    }

    std::uint32_t ideal_guests(const SimWorld& world)
    {
        const entt::registry& r = world.registry;
        std::uint32_t room = 0;
        for (const auto [e, ride, at] : r.view<const CycleRide, const Attraction>().each())
        {
            const auto* upkeep = r.try_get<Upkeep>(e);
            if (at.open && (upkeep == nullptr || !upkeep->broken))
                room += ride.room > 0 ? ride.room : ride.capacity * kTuning.arrivals.guests_per_seat;
        }
        const auto* draw = r.ctx().find<Draw>();
        room += draw != nullptr ? draw->pool_squares * kTuning.arrivals.guests_per_pool_square : 0;
        if (const auto* elsewhere = r.ctx().find<RoomElsewhere>())
            room += elsewhere->guests;
        return static_cast<std::uint32_t>(std::lround(room * kTuning.arrivals.room_to_spare.value));
    }

    std::int64_t fair_entrance_fee(const SimWorld& world)
    {
        const entt::registry& r = world.registry;
        std::int64_t over = 0;
        for (const auto [e, ride, at] : r.view<const CycleRide, const Attraction>().each())
        {
            if (!at.open)
                continue;
            float worth = kTuning.arrivals.ride_worth.value * (ride.excitement.value + kTuning.arrivals.intensity_worth.value * ride.intensity.value);
            if (ride.intensity.value > kTuning.arrivals.too_intense.value)
                worth /= 2;
            over += std::max<std::int64_t>(0, std::lround(worth) - at.price.value);
        }
        return std::llround(static_cast<double>(over) * kTuning.arrivals.fair_fee.value);
    }

    double drawn(const Draw& draw, std::uint32_t guests, std::uint32_t ideal, std::int64_t fair_fee)
    {
        // Against the plain rate: an unknown park's share, rising in step with the rating to all of it at the plain
        // rating and on past it, up to the famous rating.
        const tuning::Arrivals& a = kTuning.arrivals;
        const float known = std::clamp(draw.rating.value_or(a.plain_rating.value), a.unknown_rating.value, a.famous_rating.value) - a.unknown_rating.value;
        double rate = a.unknown_draw.value + (1 - a.unknown_draw.value) * known / (a.plain_rating.value - a.unknown_rating.value);
        if (guests >= ideal)
            rate *= kTuning.arrivals.crowded.value;
        if (draw.entrance_fee.value > fair_fee)
            rate *= kTuning.arrivals.overpriced.value;
        if (draw.entrance_fee.value > 2 * fair_fee)
            rate *= kTuning.arrivals.overpriced.value;
        return rate;
    }

    bool is_night(float time)
    {
        return time >= kTuning.night.dusk.value || time < kTuning.night.dawn.value;
    }

    float felt_excitement(float excitement, bool coaster, AgeGroup age, bool night)
    {
        if (!coaster || !night)
            return excitement;
        return excitement * (age == AgeGroup::Teen ? 1 + kTuning.night.thrill.value : 1 - kTuning.night.dread.value);
    }

    std::uint32_t ticks(float seconds)
    {
        return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0f) / kTickSeconds));
    }

    std::vector<entt::entity> admit_party(SimWorld& world, random::RandomState& dice, NodeId at)
    {
        entt::registry& r = world.registry;
        const auto weights = party_weights(world, is_night(time_of_day(r)));
        double roll = random::unit(dice) * std::reduce(weights.begin(), weights.end());
        std::size_t pick = 0;
        while (pick + 1 < kPartyKinds && roll >= weights[pick])
            roll -= weights[pick++];
        const auto kind = static_cast<PartyKind>(pick);
        const auto* mix = r.ctx().find<PartyMix>();
        // Who comes, the leader first: an adult wherever there are children.
        std::vector<std::pair<AgeGroup, bool>> who; // Age, and whether female
        const bool female = random::chance(dice, 0.5);
        const auto children = [&] {
            for (auto n = random::between(dice, std::int64_t { 1 }, kTuning.parties.most_children); n > 0; --n)
                who.emplace_back(AgeGroup::Child, random::chance(dice, 0.5));
        };
        switch (kind)
        {
            case PartyKind::LoneTeen:
            case PartyKind::LoneAdult:
                who.emplace_back(kind == PartyKind::LoneTeen ? AgeGroup::Teen : AgeGroup::Adult, female);
                break;
            case PartyKind::TeenCouple:
            case PartyKind::AdultCouple:
            {
                const AgeGroup age = kind == PartyKind::TeenCouple ? AgeGroup::Teen : AgeGroup::Adult;
                who = { { age, female }, { age, !female } };
                break;
            }
            case PartyKind::TeenGang:
            case PartyKind::AdultParty:
                for (auto n = random::between(dice, std::int64_t { 2 }, kTuning.parties.most_friends); n > 0; --n)
                    who.emplace_back(kind == PartyKind::TeenGang ? AgeGroup::Teen : AgeGroup::Adult, random::chance(dice, 0.5));
                break;
            case PartyKind::Family:
                who.emplace_back(AgeGroup::Adult, female);
                if (!random::chance(dice, mix != nullptr ? mix->single_parent.value : kTuning.parties.single_parent.value))
                    who.emplace_back(AgeGroup::Adult, !female);
                children();
                break;
            case PartyKind::DayTrip:
                who.emplace_back(AgeGroup::Adult, female);
                children();
                break;
        }
        const tuning::TasteBand band = kind == PartyKind::LoneTeen || kind == PartyKind::TeenGang ? kTuning.parties.thrill : kind == PartyKind::Family || kind == PartyKind::DayTrip ? kTuning.parties.gentle : kTuning.parties.middle;
        return admit_guests(world, dice, at, who, band);
    }

    std::vector<entt::entity> admit_guests(SimWorld& world, random::RandomState& dice, NodeId at, std::span<const std::pair<AgeGroup, bool>> who, const tuning::TasteBand& band)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        std::vector<entt::entity> members;
        for (const auto [age, is_female] : who)
        {
            const entt::entity guest = members.emplace_back(r.create());
            r.emplace<Visitor>(guest, age, is_female, std::string { }, world.tick);
            r.emplace<Pose>(guest, paths.centre(at), 0.0f);
            // Some come wishing for a swim already, the more the more they like pools.
            const auto liking = static_cast<float>(random::between(dice, double { kTuning.pools.least_liking.value }, double { kTuning.pools.most_liking.value }));
            r.emplace<Needs>(guest, Needs { .swim = { liking * static_cast<float>(random::unit(dice)) } });
            r.emplace<Mood>(guest, Mood { .happiness = kTuning.arrivals.mood, .energy = { 1 }, .tolerance = age == AgeGroup::Teen ? kTuning.arrivals.teen_tolerance : Mood { }.tolerance });
            const auto spread = [&] { return static_cast<float>(random::between(dice, -1.0, 1.0)) * kTuning.parties.taste_spread.value; };
            const float least = std::clamp(band.least.value + spread(), 0.0f, 1.0f);
            r.emplace<Taste>(guest, least, std::clamp(band.most.value + spread(), least, 1.0f), liking);
            // Around the park's averages, each on its own.
            const auto* draw = r.ctx().find<Draw>();
            const auto around = [&](Money average, Money fallback) { return Money { std::llround(static_cast<double>(average.value > 0 ? average.value : fallback.value) * (1 + random::between(dice, -1.0, 1.0) * kTuning.arrivals.cash_spread.value)) }; };
            const Money cash = around(draw != nullptr ? draw->guest_cash : Money { }, kTuning.arrivals.cash);
            r.emplace<Wallet>(guest, cash, around(draw != nullptr ? draw->guest_bank : Money { }, kTuning.arrivals.bank));
            start_walking(world, guest, at);
            ++r.ctx().emplace<Turnstile>().in[static_cast<std::size_t>(age)];
        }
        if (members.size() > 1)
        {
            const entt::entity party = r.create();
            for (const entt::entity m : members)
            {
                r.emplace<Member>(m, party);
                if (m != members.front())
                    r.get<Errand>(m) = { ErrandKind::Following, entt::null, 0 };
            }
            r.emplace<Party>(party, members.front(), members);
        }
        return members;
    }

    void start_walking(SimWorld& world, entt::entity guest, NodeId at)
    {
        auto& r = world.registry;
        // A guest who already stands somewhere keeps to where they stand on the tile.
        glm::vec2 lane { 0.0f };
        if (const auto* pose = r.try_get<Pose>(guest))
        {
            const glm::vec3 off = pose->position - world.paths.centre(at);
            lane = glm::clamp(glm::vec2(off.x, off.z), -kLaneReach, kLaneReach);
        }
        r.emplace_or_replace<Errand>(guest);
        const auto* visitor = r.try_get<Visitor>(guest);
        const float speed = visitor != nullptr ? kTuning.walking.speed[std::to_underlying(visitor->age)].value : Stride { }.speed.value;
        r.emplace_or_replace<Stride>(guest, Stride { .from = at, .to = at, .progress = 1, .speed = speed, .lane_from = lane, .lane_to = lane });
        r.emplace_or_replace<Dice>(guest, dice_for(world, guest));
        auto& pose = r.get_or_emplace<Pose>(guest);
        place(world.paths, r.get<Stride>(guest), pose);
        pose.was = pose.position;
        pose.was_heading.value = pose.heading.value;
    }

    namespace
    {
        /// Litter, path items and staff, for one tick: after the guests have walked and bought.
        void tidy(SimWorld& world)
        {
            entt::registry& r = world.registry;
            const PathNetwork& paths = world.paths;
            constexpr float dt = kTickSeconds;
            const float now = static_cast<float>(world.tick) * kTickSeconds;
            const Weather weather = weather_in(r);
            auto& vandalism = r.ctx().emplace<Vandalism>();
            // Guests outside the park with nowhere to go (just loaded, or put down there by the player) go on to the
            // gate to come in, or off to the nearest spawn point. With no way there they count as in the park.
            for (const entt::entity g : sorted(r.view<const Outside, Errand, Stride>()))
            {
                auto& errand = r.get<Errand>(g);
                auto& stride = r.get<Stride>(g);
                const auto* member = r.try_get<Member>(g);
                if (errand.kind != ErrandKind::Wandering || stride.to >= paths.nodes().size() || (member != nullptr && r.get<Party>(member->party).leader != g))
                    continue;
                std::optional<std::pair<entt::entity, std::vector<NodeId>>> goal;
                if (r.get<Outside>(g).way != Outside::Way::In)
                    goal = way_off(r, paths, stride.to);
                else
                    for (const auto [gate, at_gate] : r.view<const ParkGate>().each())
                        if (auto walk = paths.route(stride.to, at_gate.node))
                        {
                            goal = std::pair { gate, std::move(*walk) };
                            break;
                        }
                if (!goal)
                {
                    if (member != nullptr)
                        for (const entt::entity m : r.get<Party>(member->party).members)
                            r.remove<Outside>(m);
                    r.remove<Outside>(g);
                    continue;
                }
                errand = { ErrandKind::Heading, goal->first, 0 };
                stride.route = std::move(goal->second);
            }

            // Parties come families by day, mostly teens after dark, appearing at one of the park's spawn points
            // to walk to the gate; at the gate itself where no spawn point has a way there.
            double arrivals = arrivals_per_second(weather);
            if (const auto* draw = r.ctx().find<Draw>(); draw != nullptr && !r.view<const ParkGate>().empty())
            {
                const auto guests = static_cast<std::uint32_t>(std::ranges::distance(r.view<const Visitor>(entt::exclude<Left>)));
                const std::uint32_t ideal = ideal_guests(world);
                // A bigger park draws more till it is full: enough to fill its room in a day's opening, never fewer
                // than a small one's. Once full only the few of a small park still come.
                const double fill = guests < ideal ? std::max(1.0, ideal / (kTuning.arrivals.fill.value * kTuning.arrivals.rate.value)) : 1.0;
                arrivals *= fill * drawn(*draw, guests, ideal, fair_entrance_fee(world));
            }
            for (const entt::entity gate : sorted(r.view<const ParkGate>()))
            {
                if (!r.all_of<Dice>(gate))
                    r.emplace<Dice>(gate, dice_for(world, gate));
                auto& dice = r.get<Dice>(gate).state;
                if (!random::chance(dice, arrivals * dt) || r.get<ParkGate>(gate).closed)
                    continue;
                const NodeId entry = r.get<ParkGate>(gate).node;
                const auto spawns = sorted(r.view<const GuestSpawn>());
                std::optional<std::vector<NodeId>> walk;
                NodeId from = entry;
                for (std::size_t k = 0, first = spawns.empty() ? 0 : random::below(dice, spawns.size()); k < spawns.size() && !walk; ++k)
                {
                    from = r.get<GuestSpawn>(spawns[(first + k) % spawns.size()]).node;
                    walk = paths.route(from, entry);
                }
                const std::vector<entt::entity> party = admit_party(world, dice, walk ? from : entry);
                if (walk)
                {
                    for (const entt::entity m : party)
                        r.emplace<Outside>(m);
                    r.get<Errand>(party.front()) = { ErrandKind::Heading, gate, 0 };
                    r.get<Stride>(party.front()).route = std::move(*walk);
                    continue;
                }
                // At the gate they pay at once; a party that cannot is gone again.
                if (pay_entry(r, party))
                    continue;
                if (const auto* member = r.try_get<Member>(party.front()))
                    r.destroy(member->party);
                for (const entt::entity m : party)
                    r.destroy(m);
            }
            vandalism.recent *= std::exp(-dt / kTuning.vandals.fade.value);
            if (vandalism.since_warning.value >= 0)
                vandalism.since_warning.value += dt;

            // What lies and stands on each tile, in entity order. Emptying a tile's list keeps its memory for the next tick.
            StepBuffers& buffers = world.buffers;
            auto& litter_at = buffers.litter_at;
            auto& items_at = buffers.items_at;
            for (auto& [node, list] : litter_at)
                list.clear();
            for (auto& [node, list] : items_at)
                list.clear();
            for (const entt::entity e : sorted(r.view<Litter>()))
            {
                auto& piece = r.get<Litter>(e);
                piece.age.value += dt;
                litter_at[piece.node].push_back(e);
            }
            auto& speakers = buffers.speakers;
            speakers.clear();
            for (const entt::entity e : sorted(r.view<const PathItem>()))
            {
                items_at[r.get<PathItem>(e).node].push_back(e);
                if (r.get<PathItem>(e).kind == PathItemKind::Speaker)
                    speakers.push_back(e);
            }
            const auto items = [&](NodeId node) -> std::span<const entt::entity> {
                const auto found = items_at.find(node);
                return found != items_at.end() ? std::span<const entt::entity>(found->second) : std::span<const entt::entity> { };
            };
            const auto litter_on = [&](NodeId node) {
                const auto found = litter_at.find(node);
                return found != litter_at.end() ? found->second.size() : 0;
            };
            // The tiles guards keep vandals off: theirs and those a few steps along the paths from it.
            auto& guarded = buffers.guarded;
            guarded.assign(paths.nodes().size(), false);
            for (const auto [e, staff, stride] : r.view<const Staff, const Stride>().each())
            {
                if (staff.role != StaffRole::Guard || stride.to >= paths.nodes().size())
                    continue;
                auto& ring = buffers.ring;
                auto& next = buffers.next;
                ring.assign(1, stride.to);
                guarded[stride.to] = true;
                for (int step = 0; step < kTuning.vandals.guard_reach; ++step)
                {
                    next.clear();
                    for (const NodeId n : ring)
                        for (const NodeId m : paths.nodes()[n].next)
                            if (m != kNoNode && !guarded[m])
                            {
                                guarded[m] = true;
                                next.push_back(m);
                            }
                    std::swap(ring, next);
                }
            }

            // Guests: glum on dirty or vandalised paths, binning or dropping their rubbish, and the angriest breaking things.
            const auto guests_view = r.view<const Errand, const Stride, Mood, Dice, const Needs>(entt::exclude<Staff>); // Not an inspector on a visit
            const bool pool_open = std::ranges::any_of(r.view<const PoolDoor, const Attraction>().each(), [](const auto& room) { return std::get<2>(room).open; });
            // Who sits on seat `seat` of bench `bench`, if anyone, and whether they are getting up.
            const auto sitter = [&](entt::entity bench, int seat) -> std::optional<const Linger&> {
                for (const auto [g, linger] : r.view<const Linger>().each())
                    if (linger.bench == bench && linger.seat == seat)
                        return linger;
                return std::nullopt;
            };
            for (const entt::entity g : sorted(guests_view))
            {
                auto [errand, stride, mood, dice, needs] = guests_view.get(g);
                if (!walking(errand.kind) || stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                    continue;
                // Every so often, once the park has run a while, a guest takes in what is round them, walking or
                // stopped, unless taken up with a show: plenty of scenery, or else a tidy, unspoilt path. Each at
                // their own moment, by who they are.
                const float look = kTuning.surroundings.look.value;
                const float phase = static_cast<float>(entt::to_integral(g) % 101) / 101 * look;
                if (now > look && r.all_of<Visitor>(g) && !r.any_of<Watching, Gesture>(g) && std::floor((now + phase) / look) != std::floor((now - dt + phase) / look))
                {
                    const NodeId here = stride.from;
                    const auto* near = r.ctx().find<SceneryNear>();
                    const bool clean = litter_on(here) == 0 && std::ranges::none_of(paths.nodes()[here].next, [&](NodeId m) { return m != kNoNode && litter_on(m) > 0; });
                    const bool whole = std::ranges::none_of(items(here), [&](entt::entity i) { return r.get<PathItem>(i).broken; });
                    if (near != nullptr && here < near->by_node.size() && near->by_node[here] >= kTuning.surroundings.great)
                    {
                        think(r, g, ThoughtKind::GreatScenery, entt::null, now);
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.surroundings.great_cheer.value);
                    }
                    else if (clean && whole)
                    {
                        think(r, g, ThoughtKind::ReallyTidy, entt::null, now);
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.surroundings.tidy_cheer.value);
                    }
                }
                // Sitting rests a guest, watching cheers them; then they walk on.
                if (auto* linger = r.try_get<Linger>(g))
                {
                    linger->left.value -= dt;
                    if (linger->at != PathItemKind::Bench)
                    {
                        mood.happiness.value = clamp01(mood.happiness.value + kTuning.facilities.sight_cheer.value * dt);
                        // Firing a water cannon: now and then they swing it on to another guest in reach, who
                        // gets soaked while it is on them.
                        if (linger->at == PathItemKind::WaterCannon && r.all_of<Pose>(g))
                        {
                            const glm::vec3 from = r.get<Pose>(g).position;
                            const float reach = kTuning.surroundings.cannon_reach.value;
                            // Only those in front of it: it turns so far, and never back on its firer.
                            const auto* cannon = r.valid(linger->bench) ? r.try_get<PathItem>(linger->bench) : nullptr;
                            const auto in_reach = [&](entt::entity t) {
                                if (t == g || cannon == nullptr || !r.valid(t) || !r.all_of<Pose, Mood, Visitor>(t) || r.any_of<Staff>(t))
                                    return false;
                                const glm::vec3 to = r.get<Pose>(t).position - from;
                                return glm::length(to) <= reach && cannon_turn(cannon->heading, { to.x, to.z }, kTuning.surroundings.cannon_swing).has_value();
                            };
                            if (!in_reach(linger->target) || random::chance(dice.state, kTuning.surroundings.retarget.value * dt))
                            {
                                std::vector<entt::entity> near;
                                for (const entt::entity t : r.view<const Pose, const Visitor>())
                                    if (in_reach(t))
                                        near.push_back(t);
                                std::ranges::sort(near);
                                linger->target = near.empty() ? entt::null : near[random::below(dice.state, near.size())];
                            }
                            if (r.valid(linger->target))
                            {
                                Mood& soaked = r.get<Mood>(linger->target);
                                soaked.wet.value = clamp01(soaked.wet.value + kTuning.surroundings.soaking.value * dt);
                            }
                        }
                        if (linger->left.value <= 0)
                            r.remove<Linger>(g);
                        continue;
                    }
                    mood.energy.value = clamp01(mood.energy.value + kTuning.facilities.rest.value * dt);
                    mood.nausea.value = clamp01(mood.nausea.value - kTuning.sickness.rest_settle.value * dt);
                    linger->sat.value += dt;
                    if (linger->rising)
                    {
                        if (linger->left.value <= 0)
                            r.remove<Linger>(g);
                        continue;
                    }
                    // Up once no longer worn out or sick and done eating, the more rested the sooner; someone beside them
                    // keeps them chatting.
                    const bool needy = mood.energy.value < kTuning.facilities.tired.value || mood.nausea.value >= kTuning.needs.want.value || munching(r, g);
                    const auto mate = [&](int seat) { return r.valid(linger->bench) && seat >= 0 && sitter(linger->bench, seat) && !sitter(linger->bench, seat)->rising; };
                    const float chat = mate(linger->seat - 1) || mate(linger->seat + 1) ? kTuning.facilities.chatting : 1;
                    if (linger->left.value <= 0 && !needy && random::chance(dice.state, kTuning.facilities.stand.value * dt * mood.energy.value / chat))
                    {
                        linger->rising = true;
                        linger->left = kTuning.facilities.rise;
                    }
                    continue;
                }
                // What a guest just come to a tile thinks of themselves and of the path.
                if (stride.fresh)
                {
                    const auto about = [&](ThoughtKind kind, bool when) {
                        if (when)
                            think(r, g, kind, entt::null, now);
                    };
                    about(ThoughtKind::Hungry, needs.hunger.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Thirsty, needs.thirst.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Toilet, needs.bladder.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Tired, mood.energy.value < kTuning.facilities.tired.value);
                    about(mood.nausea.value >= kTuning.sickness.sick.value ? ThoughtKind::VerySick : ThoughtKind::Sick, mood.nausea.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Broke, r.get<Wallet>(g).cash.value < kTuning.facilities.low_cash.value);
                    about(ThoughtKind::Litter, litter_on(stride.from) >= kTuning.litter.dirty_path);
                    about(ThoughtKind::Raining, weather.rain && !r.all_of<Umbrella>(g));
                    about(ThoughtKind::Soaked, mood.wet.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Hot, weather.temperature.value >= kTuning.weather.hot.value || mood.heat.value >= kTuning.needs.want.value);
                    about(ThoughtKind::Thunder, weather.storm);
                    about(ThoughtKind::GoHome, errand.kind == ErrandKind::Heading && r.valid(errand.target) && r.all_of<ParkGate>(errand.target));
                    about(ThoughtKind::Cold, weather.temperature.value < kTuning.weather.cold.value);
                    const bool swim = needs.swim.value >= kTuning.needs.want.value;
                    about(ThoughtKind::NoPools, swim && !pool_open);
                    about(ThoughtKind::TooColdForPools, swim && pool_open && needs.swim.value < swim_wish_needed(weather.temperature.value));
                    about(ThoughtKind::Vandalism, std::ranges::any_of(items(stride.from), [&](entt::entity i) { return r.get<PathItem>(i).broken; }));
                    const auto here = litter_at.find(stride.from);
                    about(ThoughtKind::Vomit, here != litter_at.end() && std::ranges::any_of(here->second, [&](entt::entity l) { return r.get<Litter>(l).vomit; }));
                    // Within earshot of a loudspeaker a guest may like its music, once in a while whichever it is.
                    // shortcut: every speaker plays, and its music always pleases; upgrade when speakers keep their
                    // music and on/off state.
                    const PathTileSpot& tile = paths.nodes()[stride.from].tile;
                    const auto heard = std::ranges::find_if(speakers, [&](entt::entity s) {
                        const PathTileSpot& at = paths.nodes()[r.get<PathItem>(s).node].tile;
                        return std::abs(at.col - tile.col) <= kTuning.surroundings.earshot && std::abs(at.row - tile.row) <= kTuning.surroundings.earshot;
                    });
                    const auto* thoughts = r.try_get<Thoughts>(g);
                    const bool lately = thoughts != nullptr && std::ranges::any_of(thoughts->recent, [&](const Thought& t) { return t.kind == ThoughtKind::NiceMusic && now - t.time.value < kTuning.thoughts.rethink.value; });
                    if (heard != speakers.end() && !lately && random::chance(dice.state, kTuning.surroundings.music_thought.value))
                        think(r, g, ThoughtKind::NiceMusic, *heard, now);
                }
                // A wandering guest just come to a tile may sit on a bench there when tired or eating, or stop at a sight.
                if (errand.kind == ErrandKind::Wandering && stride.fresh)
                {
                    const NodeId here = stride.from;
                    const auto sight = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::Sight || r.get<PathItem>(i).kind == PathItemKind::PhotoSpot; });
                    // Jumping fountains catch a passer's eye for a moment, and often please them.
                    if (const auto fountain = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::Fountain; }); fountain != items(here).end() && random::chance(dice.state, kTuning.surroundings.glance.value))
                    {
                        r.emplace_or_replace<Glance>(g, Glance { .at = *fountain, .left = kTuning.surroundings.glancing });
                        if (random::chance(dice.state, kTuning.surroundings.fountain_thought.value))
                        {
                            think(r, g, ThoughtKind::JumpingFountainsGood, entt::null, now);
                            mood.happiness.value = clamp01(mood.happiness.value + kTuning.surroundings.fountain_cheer.value);
                        }
                        continue;
                    }
                    // A free water cannon tempts some passers to fire it a while, one at a time.
                    const auto cannon = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::WaterCannon && !r.get<PathItem>(i).broken; });
                    if (cannon != items(here).end() && std::ranges::none_of(r.view<const Linger>().each(), [&](const auto& other) { return std::get<1>(other).bench == *cannon; }) && random::chance(dice.state, kTuning.surroundings.fire_cannon.value))
                    {
                        r.emplace<Linger>(g, Linger { .at = PathItemKind::WaterCannon, .left = kTuning.surroundings.firing, .bench = *cannon });
                        continue;
                    }
                    // The first free seat of a whole bench on their tile.
                    const auto seat = [&]() -> std::optional<std::pair<entt::entity, std::uint8_t>> {
                        for (const entt::entity i : items(here))
                            if (const auto& item = r.get<PathItem>(i); item.kind == PathItemKind::Bench && !item.broken)
                                for (std::uint8_t s = 0; s < item.seats; ++s)
                                    if (!sitter(i, s))
                                        return std::pair { i, s };
                        return std::nullopt;
                    };
                    // Worn out, sick, or with something to eat or drink in hand.
                    if (mood.energy.value < kTuning.facilities.tired.value || mood.nausea.value >= kTuning.needs.want.value || munching(r, g))
                        if (const auto free = seat(); free && random::chance(dice.state, kTuning.facilities.sit_chance.value))
                        {
                            r.emplace<Linger>(g, Linger { .at = PathItemKind::Bench, .left = kTuning.facilities.sit, .bench = free->first, .seat = free->second });
                            continue;
                        }
                    // Most walk past an effect or animatronic, taking it in as scenery; a view is worth a stop.
                    if (sight != items(here).end() && random::chance(dice.state, r.get<PathItem>(*sight).kind == PathItemKind::Sight ? kTuning.surroundings.stop.value : kTuning.facilities.watch_chance.value))
                    {
                        const PathItemKind kind = r.get<PathItem>(*sight).kind;
                        r.emplace<Linger>(g, Linger { .at = kind, .left = kTuning.facilities.watch, .bench = *sight });
                        // At a view the whole party stops to take it in, in a row along it, and judges it by the scenery
                        // round it: a fine one pleases them, a bare one gets folded arms. The player keeps a photo of each
                        // stop their own guest's party makes, one of the others taking it.
                        if (kind == PathItemKind::PhotoSpot)
                        {
                            const auto* member = r.try_get<Member>(g);
                            const std::vector<entt::entity> party = member != nullptr ? r.get<Party>(member->party).members : std::vector { g };
                            const auto* near = r.ctx().find<SceneryNear>();
                            const bool fine = near != nullptr && here < near->by_node.size() && near->by_node[here] >= kTuning.surroundings.great;
                            entt::entity avatar = entt::null;
                            for (const entt::entity m : party)
                            {
                                if (avatar == entt::null && r.get<Visitor>(m).avatar)
                                    avatar = m;
                                if (!r.all_of<Mood, Stride>(m))
                                    continue;
                                if (m != g && !r.all_of<Linger>(m))
                                    r.emplace<Linger>(m, Linger { .at = kind, .left = kTuning.facilities.watch, .bench = *sight });
                                if (!fine)
                                {
                                    r.emplace_or_replace<Gesture>(m, GestureKind::Disapproval, kTuning.gestures.shown.value);
                                    continue;
                                }
                                think(r, m, ThoughtKind::PhotoSpotGood, entt::null, now);
                                auto& happiness = r.get<Mood>(m).happiness.value;
                                happiness = clamp01(happiness + kTuning.surroundings.view_cheer.value);
                            }
                            if (avatar != entt::null)
                            {
                                r.ctx().emplace<Photos>().taken.push_back({ avatar, here });
                                think(r, avatar, ThoughtKind::Photographee, entt::null, now);
                            }
                            // One of a party films the rest: the player's guest's party has their photo taken.
                            const entt::entity posing = avatar != entt::null ? avatar : g;
                            if (const auto other = std::ranges::find_if(party, [&](entt::entity m) { return m != posing; }); other != party.end())
                            {
                                if (avatar != entt::null)
                                    think(r, *other, ThoughtKind::Photographer, entt::null, now);
                                if (auto* taking = r.try_get<Linger>(*other); taking != nullptr && taking->bench == *sight)
                                    taking->camera = true;
                            }
                        }
                        continue;
                    }
                }
                const NodeId at = stride.progress.value < 0.5f ? stride.from : stride.to;
                if (litter_on(at) >= kTuning.litter.dirty_path)
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.litter.dirt_gloom.value * dt);
                if (const auto here = litter_at.find(at); here != litter_at.end() && std::ranges::any_of(here->second, [&](entt::entity l) { return r.get<Litter>(l).vomit; }))
                {
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.sickness.vomit_gloom.value * dt);
                    mood.nausea.value = clamp01(mood.nausea.value + kTuning.sickness.queasy.value * dt);
                }
                if (mood.nausea.value >= kTuning.sickness.sick.value && random::chance(dice.state, kTuning.sickness.sick_walking.value * dt))
                {
                    litter_at[at].push_back(be_sick(r, g, at));
                }
                if (std::ranges::any_of(items(at), [&](entt::entity i) { return r.get<PathItem>(i).broken; }))
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.vandals.gloom.value * dt);
                // Empties go in a bin with room on their tile, all they have, as many as fit; one held too long goes
                // in a pocket, or on the path once both are full.
                if (auto* rubbish = r.try_get<Carried>(g); rubbish != nullptr && (rubbish->bites == 0 || !rubbish->held))
                {
                    const bool in_hand = rubbish->held && rubbish->bites == 0;
                    if (in_hand)
                        rubbish->since.value += dt;
                    const auto bin = std::ranges::find_if(items(at), [&](entt::entity i) { return r.all_of<Bin>(i) && !r.get<PathItem>(i).broken && r.get<Bin>(i).contents + 1 <= r.get<Bin>(i).capacity; });
                    if (bin != items(at).end())
                    {
                        auto& b = r.get<Bin>(*bin);
                        for (bool hand = in_hand; (hand || rubbish->pocketed > 0) && b.contents + 1 <= b.capacity; hand = false)
                        {
                            b.contents += 1;
                            if (hand)
                                rubbish->held = false;
                            else
                                --rubbish->pocketed;
                        }
                        r.emplace_or_replace<Gesture>(g, Gesture { .kind = GestureKind::HandOver, .left = kTuning.gestures.shown, .at = *bin });
                        if (!rubbish->held && rubbish->pocketed == 0)
                            r.remove<Carried>(g);
                    }
                    else if (in_hand && rubbish->since.value >= kTuning.litter.carry.value)
                        free_hand(r, g, *rubbish, at);
                }
                if (mood.happiness.value < kTuning.vandals.mood.value && !guarded[at] && random::chance(dice.state, kTuning.vandals.breaking.value * dt))
                {
                    const auto whole = std::ranges::find_if(items(at), [&](entt::entity i) { return !r.get<PathItem>(i).broken && breakable(r.get<PathItem>(i).kind); });
                    if (whole != items(at).end())
                    {
                        r.get<PathItem>(*whole).broken = true;
                        ++vandalism.broken;
                        vandalism.recent += 1;
                        if (vandalism.recent >= kTuning.vandals.bad && (vandalism.since_warning.value < 0 || vandalism.since_warning.value >= kTuning.vandals.warning_every.value))
                        {
                            ++vandalism.warnings;
                            vandalism.since_warning.value = 0;
                        }
                    }
                }
            }

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
                care_for(staff, duty != nullptr ? duty->job != KeeperJob::Idle && duty->job != KeeperJob::Strolling : guests_near(pose.position), dt);
                if (staff.think.value -= dt; staff.think.value <= 0)
                {
                    staff.think = kTuning.staff.think;
                    if (!r.all_of<Dice>(e))
                        r.emplace<Dice>(e, dice_for(world, e));
                    for (const ThoughtKind kind : staff_thoughts(staff))
                    {
                        if (kind == ThoughtKind::StaffWantToQuit && duty == nullptr)
                            continue;
                        think(r, e, kind, entt::null, now);
                        if (kind == ThoughtKind::StaffWantToQuit)
                            r.ctx().emplace<Findings>().found.emplace_back(Finding::WantsToQuit, e);
                    }
                    if (duty != nullptr && quits(staff))
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
                care_for(staff, staff.role == StaffRole::Entertainer ? guests_near(pose.position) : (staff.busy.value > 0 && staff.job != StaffJob::Rest) || staff.ride != entt::null, dt);
                if (staff.think.value -= dt; staff.think.value <= 0)
                {
                    staff.think = kTuning.staff.think;
                    for (const ThoughtKind kind : staff_thoughts(staff))
                    {
                        think(r, e, kind, entt::null, now);
                        if (kind == ThoughtKind::StaffWantToQuit)
                            r.ctx().emplace<Findings>().found.emplace_back(Finding::WantsToQuit, e);
                    }
                    if (quits(staff))
                        r.ctx().emplace<Findings>().found.emplace_back(Finding::Quit, e);
                }
                // A lazy one, with nothing to do, stops where they are now and then.
                if (staff.busy.value == 0 && staff.ride == entt::null && random::chance(dice.state, rest_chance(staff) * dt))
                {
                    staff.busy.value = staff.job_length.value = kRestSeconds;
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
                            r.ctx().emplace<Supplies>().paid.value += std::llround(static_cast<double>(price) * (1 - tank->left.value));
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
                        staff.busy.value = staff.job_length.value = (broken ? kTuning.wear.repair.value : kTuning.wear.inspect.value) * work_slowdown(staff);
                        staff.job = broken ? StaffJob::Fix : StaffJob::Inspect;
                        stride.to = at;
                        stride.progress.value = 1;
                    }
                }
                else if (const float busy = job(); busy > 0)
                {
                    staff.busy.value = staff.job_length.value = busy * work_slowdown(staff);
                    stride.to = at;
                    stride.progress.value = 1;
                }
                else if (staff.role == StaffRole::Guard && random::chance(dice.state, kWatchChance))
                {
                    staff.busy.value = staff.job_length.value = kWatchSeconds;
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

            // Crowds: guests stopped round an entertainer's show or a VIP (see crowds.hpp).
            const auto flat = [](glm::vec3 v) { return glm::vec2 { v.x, v.z }; };
            // Guests walking free within `reach` of `star` (alone, or leading their party), each with their party.
            const auto free_near = [&](entt::entity star, float reach) {
                std::vector<entt::entity> found;
                const glm::vec2 at = flat(r.get<Pose>(star).position);
                for (const auto [g, visitor, errand, stride, pose] : r.view<const Visitor, const Errand, const Stride, const Pose>(entt::exclude<Staff, Vip, Watching, Linger, Gesture, Left, Held, Dropped, RideWalk, Crossing>).each())
                {
                    const float d = glm::distance(flat(pose.position), at);
                    if (errand.kind != ErrandKind::Wandering || stride.from >= paths.nodes().size() || d > reach || d < kTuning.shows.near.value)
                        continue;
                    if (const auto* member = r.try_get<Member>(g); member != nullptr && (r.get<Party>(member->party).leader != g || r.get<Party>(member->party).huddle > 0))
                        continue;
                    found.push_back(g);
                }
                std::ranges::sort(found);
                return found;
            };
            // `guest` and their party stop to watch `star`, each on their own side of them. A fan goes alone, the
            // party following them.
            const auto draw_in = [&](entt::entity star, entt::entity guest, bool autograph) {
                const auto* member = r.try_get<Member>(guest);
                const std::vector<entt::entity> who = member != nullptr && !autograph ? r.get<Party>(member->party).members : std::vector { guest };
                const glm::vec2 off = flat(r.get<Pose>(guest).position) - flat(r.get<Pose>(star).position);
                const float angle = std::atan2(off.x, off.y);
                for (std::size_t k = 0; k < who.size(); ++k)
                    if (r.all_of<Stride, Pose, Mood>(who[k]) && !r.any_of<Gesture, Linger, Watching>(who[k]))
                        r.emplace<Watching>(who[k], Watching { .star = star, .angle = { angle + 0.4f * (static_cast<float>(k) - static_cast<float>(who.size() - 1) / 2) }, .autograph = autograph });
            };
            // Done watching `star`: the crowd shows what they made of it, a good show or a poor one.
            const auto show_over = [&](entt::entity star, std::optional<bool> poor) {
                for (const auto [g, watching] : r.view<const Watching>().each())
                {
                    if (watching.star != star || watching.autograph)
                        continue;
                    if (poor && r.all_of<Mood, Dice>(g))
                    {
                        auto& mood = r.get<Mood>(g);
                        auto& dice = r.get<Dice>(g).state;
                        if (*poor)
                        {
                            // Children cry or take fright; the rest shake their heads, look at their watches or fold their arms.
                            constexpr std::array kUpset { GestureKind::Cry, GestureKind::Horror };
                            constexpr std::array kBored { GestureKind::Grumble, GestureKind::LookWatch, GestureKind::Disapproval };
                            const bool child = r.all_of<Visitor>(g) && r.get<Visitor>(g).age == AgeGroup::Child;
                            r.emplace_or_replace<Gesture>(g, child ? kUpset[random::below(dice, kUpset.size())] : kBored[random::below(dice, kBored.size())], kTuning.gestures.shown.value);
                        }
                        else
                        {
                            constexpr std::array kGlad { GestureKind::Clap, GestureKind::Clap, GestureKind::Cheer, GestureKind::Laugh };
                            const GestureKind kind = kGlad[random::below(dice, kGlad.size())];
                            r.emplace_or_replace<Gesture>(g, kind, kind == GestureKind::Clap ? kTuning.shows.clap.value : kTuning.gestures.shown.value);
                            mood.happiness.value = clamp01(mood.happiness.value + kTuning.shows.cheer.value);
                        }
                        think(r, g, *poor ? ThoughtKind::BoringEntertainer : ThoughtKind::GreatEntertainer, star, now);
                    }
                }
                const auto view = r.view<const Watching>();
                for (const entt::entity g : sorted(view))
                    if (view.get<const Watching>(g).star == star)
                        r.remove<Watching>(g);
            };

            // Entertainers: a show under way goes on, drawing guests in, till its moves are through or they are
            // picked up.
            for (const entt::entity e : sorted(r.view<Staff, Show, Dice>()))
            {
                Show& show = r.get<Show>(e);
                Staff& staff = r.get<Staff>(e);
                show.gap.value -= dt;
                if (show.moves.empty())
                    continue;
                show.into.value += dt;
                const bool stopped = r.any_of<Held, Dropped>(e) || !r.all_of<Stride>(e);
                if (!stopped && show.into.value < show_seconds(show.moves))
                {
                    staff.busy.value = std::max(staff.busy.value, dt); // Stands still for it
                    for (const entt::entity g : free_near(e, kTuning.shows.reach.value))
                    {
                        const auto* member = r.try_get<Member>(g);
                        const auto many = member != nullptr ? r.get<Party>(member->party).members.size() : 1;
                        if (random::chance(r.get<Dice>(e).state, kTuning.shows.join.value * dt / static_cast<double>(many)))
                            draw_in(e, g, false);
                    }
                    continue;
                }
                if (show.poor && !stopped)
                    staff.happiness.value = clamp01(staff.happiness.value - kTuning.shows.poor_gloom.value);
                show_over(e, stopped ? std::nullopt : std::optional { show.poor });
                show.moves.clear();
                show.gap = kTuning.shows.gap;
            }
            // A free one, now and then, puts on a show when they find guests near and feel like it.
            for (const entt::entity e : sorted(staff_view))
            {
                auto [staff, stride, pose, dice] = staff_view.get(e);
                if (staff.role != StaffRole::Entertainer)
                    continue;
                Show& show = r.get_or_emplace<Show>(e);
                if (!show.moves.empty() || show.gap.value > 0 || staff.busy.value > 0 || r.any_of<Held, Dropped>(e) || stride.from >= paths.nodes().size())
                    continue;
                show.gap = kTuning.shows.look;
                if (free_near(e, kTuning.shows.reach.value).empty() || !random::chance(dice.state, show_chance(staff)))
                    continue;
                show.poor = poor_show(staff);
                show.moves = show_moves(show_quality(staff), show.poor, dice.state);
                show.into = { };
                staff.busy.value = dt;
            }

            // VIPs: now and then fans gather round them, and they sign for those near enough, one at a time.
            for (const entt::entity v : sorted(r.view<Vip, const Pose, Dice>(entt::exclude<Left>)))
            {
                Vip& vip = r.get<Vip>(v);
                if (vip.leaving)
                    continue;
                auto& dice = r.get<Dice>(v).state;
                if (vip.poll.value -= dt; vip.poll.value <= 0)
                {
                    vip.poll = kTuning.fans.poll;
                    for (const entt::entity g : free_near(v, kTuning.shows.reach.value))
                    {
                        const auto* member = r.try_get<Member>(g);
                        const auto many = member != nullptr ? r.get<Party>(member->party).members.size() : 1;
                        const auto* got = r.try_get<Autographs>(g);
                        if ((got == nullptr || !std::ranges::contains(got->from, v)) && random::chance(dice, kTuning.fans.keen.value / static_cast<double>(many)))
                            draw_in(v, g, true);
                    }
                }
                if (r.all_of<Gesture>(v) || r.any_of<Held, Dropped>(v))
                    continue;
                const glm::vec2 at = flat(r.get<Pose>(v).position);
                const auto view = r.view<const Watching, const Pose>();
                for (const entt::entity g : sorted(view))
                {
                    const auto& watching = view.get<const Watching>(g);
                    if (watching.star != v || !watching.autograph || glm::distance(flat(view.get<const Pose>(g).position), at) > kTuning.fans.arm.value)
                        continue;
                    r.emplace_or_replace<Gesture>(v, GestureKind::HandOver, kTuning.gestures.shown);
                    r.remove<Watching>(g);
                    r.emplace_or_replace<Gesture>(g, GestureKind::HandOver, kTuning.gestures.shown);
                    r.get_or_emplace<Autographs>(g).from.push_back(v);
                    auto& mood = r.get<Mood>(g);
                    mood.happiness.value = clamp01(mood.happiness.value + kTuning.fans.cheer.value);
                    ++vip.signatures;
                    break;
                }
            }

            // Those watching step to their place round the star and face them; a fan gives up after a while.
            for (auto [v, vip] : r.view<Vip>().each())
                vip.fans = 0;
            const auto watchers = r.view<Watching, Stride, Pose>();
            const auto listed = sorted(watchers);
            for (const entt::entity g : listed)
            {
                auto [watching, stride, pose] = watchers.get(g);
                watching.waited.value += dt;
                const bool gone = !r.valid(watching.star) || r.any_of<Left>(watching.star) || !r.all_of<Pose>(watching.star);
                const auto* vip = gone ? nullptr : r.try_get<Vip>(watching.star);
                const float apart = gone ? 0.0f : glm::distance(flat(pose.position), flat(r.get<Pose>(watching.star).position));
                const bool tired = watching.waited.value >= kTuning.fans.give_up.value || apart >= kTuning.fans.far.value;
                if (gone || (watching.autograph && (vip == nullptr || vip->leaving || tired)) || (!watching.autograph && r.get_or_emplace<Show>(watching.star).moves.empty()) || stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                {
                    r.remove<Watching>(g);
                    continue;
                }
                // A fan too far off walks after the VIP over the paths, to the tile they are on now; the walk is
                // planned again whenever the VIP moves to another tile. No way there: they give up.
                watching.chasing = watching.autograph && apart > kTuning.fans.close.value;
                if (watching.chasing)
                {
                    const Stride& at = r.get<Stride>(watching.star);
                    const NodeId target = at.progress.value < 0.5f ? at.from : at.to;
                    if (target == stride.to)
                        stride.route.clear();
                    else if (stride.route.empty() || stride.route.back() != target)
                    {
                        auto way = paths.route(stride.to, target, Walker::Guest, r.get<Wallet>(g).cash.value);
                        if (!way)
                        {
                            r.remove<Watching>(g);
                            continue;
                        }
                        stride.route = std::move(*way);
                    }
                    continue;
                }
                if (watching.autograph)
                {
                    stride.route.clear();
                    ++r.get<Vip>(watching.star).fans;
                }
                const glm::vec2 star = flat(r.get<Pose>(watching.star).position);
                const float ring = watching.autograph ? kTuning.fans.ring.value : kTuning.shows.ring.value;
                const glm::vec2 want = star + ring * glm::vec2 { std::sin(watching.angle.value), std::cos(watching.angle.value) };
                // The spot kept on the path: on whichever of their own tiles or the star's comes nearest to it.
                glm::vec2 spot = want;
                float best = std::numeric_limits<float>::infinity();
                const auto* star_stride = r.try_get<Stride>(watching.star);
                for (const NodeId n : { stride.from, stride.to, star_stride != nullptr ? star_stride->from : kNoNode, star_stride != nullptr ? star_stride->to : kNoNode })
                    if (n < paths.nodes().size())
                    {
                        const glm::vec2 centre = flat(paths.centre(n));
                        const glm::vec2 kept = centre + glm::clamp(want - centre, -kLaneReach, kLaneReach);
                        if (const float d = glm::distance(kept, want); d < best)
                        {
                            best = d;
                            spot = kept;
                        }
                    }
                // Both ends of their step shifted alike, so they stand on the spot and walk on from there after,
                // drifting back across the path over their next step.
                const glm::vec2 middle = glm::mix(flat(paths.centre(stride.from)), flat(paths.centre(stride.to)), stride.progress.value);
                const glm::vec2 lane = spot - middle;
                const glm::vec2 was = glm::mix(stride.lane_from, stride.lane_to, stride.progress.value);
                const glm::vec2 step = lane - was;
                const float most = kTuning.walking.lane_drift.value * 4 * dt;
                const glm::vec2 now_lane = was + (glm::length(step) > most ? step * (most / glm::length(step)) : step);
                stride.lane_from = stride.lane_to = now_lane;
                place(paths, stride, pose);
                const glm::vec2 look = star - flat(pose.position);
                if (glm::dot(look, look) > 0.0001f)
                    pose.heading.value = std::atan2(look.x, look.y);
            }
        }
    } // namespace

    namespace
    {
        /// `staff` and everyone sharing their zone.
        std::vector<entt::entity> zone_of(const entt::registry& r, entt::entity staff)
        {
            const auto* patrol = r.try_get<Patrol>(staff);
            if (patrol == nullptr || patrol->zone == 0)
                return { staff };
            std::vector<entt::entity> all;
            for (const auto [e, p] : r.view<const Patrol>().each())
                if (p.zone == patrol->zone)
                    all.push_back(e);
            return all;
        }

        /// Sets the tiles of `staff`'s zone; with none left, the zone keeps them only as members.
        void set_tiles(entt::registry& r, entt::entity staff, const std::vector<glm::ivec2>& tiles)
        {
            for (const entt::entity e : zone_of(r, staff))
            {
                if (tiles.empty() && r.get_or_emplace<Patrol>(e).zone == 0)
                    r.remove<Patrol>(e);
                else
                    r.get_or_emplace<Patrol>(e).tiles = tiles;
            }
        }
    } // namespace

    void paint_patrol(entt::registry& r, entt::entity staff, glm::ivec2 at, int across, bool add)
    {
        const auto* patrol = r.try_get<Patrol>(staff);
        std::vector<glm::ivec2> tiles = patrol != nullptr ? patrol->tiles : std::vector<glm::ivec2> { };
        for (int dc = -across / 2; dc <= across / 2; ++dc)
            for (int dr = -across / 2; dr <= across / 2; ++dr)
            {
                const glm::ivec2 t = at + glm::ivec2(dc, dr);
                const auto in = std::ranges::find(tiles, t);
                if (add && in == tiles.end())
                    tiles.push_back(t);
                else if (!add && in != tiles.end())
                    tiles.erase(in);
            }
        set_tiles(r, staff, tiles);
    }

    void clear_patrol(entt::registry& r, entt::entity staff)
    {
        set_tiles(r, staff, { });
    }

    void fill_patrol(entt::registry& r, entt::entity staff)
    {
        const auto* patrol = r.try_get<Patrol>(staff);
        if (patrol == nullptr || patrol->tiles.empty())
            return;
        glm::ivec2 low = patrol->tiles.front();
        glm::ivec2 high = low;
        for (const glm::ivec2 t : patrol->tiles)
        {
            low = glm::min(low, t);
            high = glm::max(high, t);
        }
        std::vector<glm::ivec2> tiles;
        for (int col = low.x; col <= high.x; ++col)
            for (int row = low.y; row <= high.y; ++row)
                tiles.emplace_back(col, row);
        set_tiles(r, staff, tiles);
    }

    void join_zone(entt::registry& r, entt::entity staff, std::uint32_t zone)
    {
        if (zone == 0)
        {
            if (auto* patrol = r.try_get<Patrol>(staff))
            {
                patrol->zone = 0;
                if (patrol->tiles.empty())
                    r.remove<Patrol>(staff);
            }
            return;
        }
        std::optional<std::vector<glm::ivec2>> tiles;
        for (const auto [e, p] : r.view<const Patrol>().each())
            if (e != staff && p.zone == zone)
                tiles = p.tiles;
        auto& patrol = r.get_or_emplace<Patrol>(staff);
        patrol.zone = zone;
        if (tiles)
            patrol.tiles = std::move(*tiles);
    }

    Replacement replacement(const SimWorld& world, const std::function<std::int64_t(std::uint64_t object)>& price)
    {
        Replacement out;
        for (const auto [e, item] : world.registry.view<const PathItem>().each())
            if (item.broken)
            {
                ++out.items;
                out.cost += price(item.object);
            }
        return out;
    }

    void replace_broken(SimWorld& world)
    {
        for (const auto [e, item] : world.registry.view<PathItem>().each())
            item.broken = false;
    }

    void pull_down(SimWorld& world, entt::entity attraction)
    {
        entt::registry& r = world.registry;
        if (!r.valid(attraction) || !r.all_of<Attraction>(attraction))
            return;
        const bool ride = r.all_of<CycleRide>(attraction);
        for (const auto [guest, errand, stride, pose] : r.view<Errand, Stride, Pose>().each())
        {
            if (errand.target != attraction)
                continue;
            const ErrandKind was = errand.kind;
            const NodeId entrance = ride ? entrance_of(r, attraction, errand.station) : r.get<Attraction>(attraction).entrance;
            const NodeId exit = ride ? exit_of(r, attraction, errand.off) : entrance;
            // A party goes on together behind its leader, who picks somewhere else to go.
            const auto* member = r.try_get<Member>(guest);
            const bool follows = member != nullptr && r.valid(member->party) && r.get<Party>(member->party).leader != guest;
            errand = follows ? Errand { ErrandKind::Following, entt::null, 0 } : Errand { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
            r.remove<RideWalk, Swimmer, PoolRoute>(guest);
            // Out of the queue at its entrance, riders off at its exit, swimmers out at the door; those on their
            // way walk on from where they are.
            if ((was == ErrandKind::Queueing || was == ErrandKind::Swimming) && entrance != kNoNode)
                stop_at(world.paths, entrance, stride, pose);
            else if (was == ErrandKind::Riding && exit != kNoNode)
                stop_at(world.paths, exit, stride, pose);
            else
                stride.route.clear();
        }
        r.destroy(attraction);
    }

    std::size_t uncovered_paths(const SimWorld& world, StaffRole role)
    {
        std::vector<const Patrol*> areas;
        for (const auto [e, staff] : world.registry.view<const Staff>().each())
        {
            if (staff.role != role)
                continue;
            const auto* patrol = world.registry.try_get<Patrol>(e);
            if (patrol == nullptr || patrol->tiles.empty())
                return 0;
            areas.push_back(patrol);
        }
        if (areas.empty())
            return 0;
        return static_cast<std::size_t>(std::ranges::count_if(world.paths.nodes(), [&](const PathNode& n) { return !n.queue && !n.removed && std::ranges::none_of(areas, [&](const Patrol* a) { return a->covers(n.tile); }); }));
    }

    std::int32_t old_litter(const SimWorld& world)
    {
        const auto view = world.registry.view<const Litter>();
        return static_cast<std::int32_t>(std::ranges::count_if(view, [&](entt::entity e) { return view.get<const Litter>(e).age.value >= kTuning.litter.old.value; }));
    }

    void step(SimWorld& world, std::optional<Workers&> workers)
    {
        entt::registry& r = world.registry;

        std::optional<profiling::Scope> phase; // Each phase timed in turn, for the profiler's table
        const Weather weather = weather_in(r);
        const bool night = is_night(time_of_day(r));

        // Guests: their needs, walking, following, spacing out and arriving (simulation/guests.cpp).
        step_guests(world, workers, weather, night, phase);

        // Stalls and cash machines wear and break down (simulation/stalls.cpp).
        step_stalls(world, phase);

        // Pools, swimmers, lazy rivers and slides climbed on foot (simulation/swimming.cpp).
        step_pools(world, phase);

        // Riders on their feet walk to their seats or off to the paths (simulation/ride_walks.cpp).
        step_ride_walks(world, phase);

        // Those the player let go of, or a crash threw, fall and walk back to the paths (simulation/holding.cpp).
        step_let_go(world, phase);

        // Queues, riders and trains of the rides (simulation/rides.cpp).
        step_rides(world, weather, night, phase);

        // Parties wait for and gather their members (simulation/parties.cpp).
        step_parties(world, phase);

        phase.emplace("Tidy");
        tidy(world);
        phase.reset();
        ++world.tick;
    }

} // namespace rct3::simulation
