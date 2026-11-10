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
#include "simulation/park/state.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
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

    namespace
    {
        // Downtime windows: a game hour each, sampled 20 times, so each counts 0..20 as saved parks' do.
        constexpr std::uint8_t kDowntimeSamples = 20;
        constexpr float kDowntimeSampleSeconds = kSecondsPerHour / kDowntimeSamples;

        /// A share, 0 to 1, fixed for each guest, so a queue looks the same every time it is shown.
        float fixed_share(entt::entity e, std::uint64_t salt)
        {
            std::uint64_t mix = (std::uint64_t { entt::to_integral(e) } << 32) ^ salt;
            return static_cast<float>(random::split_mix(mix) >> 40) / static_cast<float>(1u << 24);
        }
    } // namespace

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

    float Upkeep::downtime_share() const
    {
        int sum = 0;
        for (const std::uint8_t w : downtime)
            sum += w;
        return static_cast<float>(sum) / static_cast<float>(downtime.size() * kDowntimeSamples);
    }

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

    namespace
    {
        /// Throws `person`, on foot or just let out of a crashed ride, from `at` at `velocity`: they fly, land
        /// knocked over, lie there a while, then walk to `node` and go on from there.
        void throw_person(entt::registry& r, entt::entity person, glm::vec3 at, glm::vec3 velocity, NodeId node)
        {
            if (!r.valid(person) || r.any_of<Left, Held, Dropped>(person) || !r.all_of<Errand, Stride, Pose>(person) || node == kNoNode)
                return;
            step_out(r, person);
            r.remove<Errand, Stride, Linger, Gesture, Watching, RideWalk>(person);
            auto& pose = r.get<Pose>(person);
            pose.position = pose.was = at;
            const auto* surroundings = r.ctx().find<WreckSurroundings>();
            const float ground = surroundings != nullptr && surroundings->ground ? surroundings->ground({ at.x, at.z }) : 0.0f;
            r.emplace<Dropped>(person, Dropped { .ground = ground, .fall = { -velocity.y }, .drift = { velocity.x, velocity.z }, .thrown = true, .node = node });
        }
    } // namespace

    void step_out(entt::registry& r, entt::entity person)
    {
        const Errand& errand = r.get<Errand>(person);
        if (errand.kind == ErrandKind::Queueing && r.valid(errand.target))
            if (auto* ride = r.try_get<CycleRide>(errand.target))
                std::erase(ride->queue, person);
        // The next member leads meanwhile; the others wait for them (5.).
        if (const auto* member = r.try_get<Member>(person); member != nullptr && r.valid(member->party))
        {
            Party& party = r.get<Party>(member->party);
            std::erase(party.members, person);
            party.away.push_back(person);
            if (party.leader == person)
            {
                party.leader = party.members.empty() ? entt::null : party.members.front();
                if (party.leader != entt::null)
                    if (Errand& next = r.get<Errand>(party.leader); next.kind == ErrandKind::Following)
                        next = { ErrandKind::Wandering, entt::null, 0 };
            }
        }
        r.remove<Lost>(person);
        r.remove<Member>(person);
        // A mechanic drops the job in hand, and another is sent.
        if (auto* staff = r.try_get<Staff>(person))
        {
            if (auto* upkeep = r.valid(staff->ride) ? r.try_get<Upkeep>(staff->ride) : nullptr; upkeep != nullptr && upkeep->mechanic == person)
                upkeep->mechanic = entt::null;
            staff->ride = entt::null;
            staff->busy = { };
            staff->job = StaffJob::None;
        }
    }

    std::optional<NodeId> path_to_rejoin(const PathNetwork& paths, glm::vec3 ground)
    {
        std::optional<NodeId> node = paths.nearest(ground);
        const auto& nodes = paths.nodes();
        if (node && !nodes[*node].queue && !nodes[*node].removed && std::abs(nodes[*node].height - ground.y) <= 1)
            return node;
        node.reset();
        float best = std::numeric_limits<float>::max();
        for (NodeId n = 0; n < nodes.size(); ++n)
            if (const float d = glm::distance(paths.centre(n), ground); !nodes[n].queue && !nodes[n].removed && d < best)
            {
                best = d;
                node = n;
            }
        return node;
    }

    bool pick_up(SimWorld& world, entt::entity person)
    {
        entt::registry& r = world.registry;
        if (!r.valid(person) || r.any_of<Left, Held, Dropped, RideWalk>(person) || !r.all_of<Errand, Stride, Pose, Dice>(person))
            return false;
        Errand& errand = r.get<Errand>(person);
        const auto* swimmer = r.try_get<Swimmer>(person);
        if (errand.kind == ErrandKind::Riding || (errand.kind == ErrandKind::Swimming && (swimmer == nullptr || swimmer->changing.value > 0)))
            return false;
        step_out(r, person);
        auto& dice = r.get<Dice>(person).state;
        const auto& holding = kTuning.holding;
        const Held held { .height = { static_cast<float>(random::between(dice, holding.lowest.value, holding.highest.value)) }, .happy = random::chance(dice, holding.happy.value), .swimwear = in_swimwear(r, person) };
        if (r.all_of<Visitor>(person))
        {
            constexpr std::array kSay { ThoughtKind::PickedUpHappy, ThoughtKind::PickedUpNeutral, ThoughtKind::PickedUpSad };
            think(r, person, kSay[random::below(dice, kSay.size())], entt::null, static_cast<float>(world.tick) * kTickSeconds);
        }
        r.remove<Errand, Stride, Linger, Gesture, Watching, Swimmer, PoolRoute>(person);
        r.emplace<Held>(person, held);
        return true;
    }

    bool let_go(SimWorld& world, entt::entity person, glm::vec3 ground)
    {
        entt::registry& r = world.registry;
        if (!r.valid(person) || !r.all_of<Held, Pose>(person))
            return false;
        const std::optional<NodeId> node = path_to_rejoin(world.paths, ground);
        if (!node)
            return false;
        // Only guests swim; staff let go over a pool walk off it to the paths.
        const auto door = r.all_of<Visitor>(person) ? pool_door_at(world, ground) : std::nullopt;
        const bool swimwear = r.get<Held>(person).swimwear;
        r.remove<Held>(person);
        r.emplace<Dropped>(person, Dropped { .ground = ground.y, .node = *node, .swimwear = swimwear, .pool = door.value_or(entt::null) });
        return true;
    }

    std::optional<glm::vec3> entrance_drop(const SimWorld& world)
    {
        const auto gates = world.registry.view<const ParkGate>();
        if (gates.empty())
            return std::nullopt;
        const glm::vec3 gate = world.paths.centre(gates.get<const ParkGate>(gates.front()).node);
        std::optional<glm::vec3> best;
        for (NodeId n = 0; n < world.paths.nodes().size(); ++n)
        {
            const PathNode& node = world.paths.nodes()[n];
            if (node.removed || node.queue || node.staff_kept_off)
                continue;
            const glm::vec3 at = world.paths.centre(n);
            if (!best || glm::distance(at, gate) < glm::distance(*best, gate))
                best = at;
        }
        return best;
    }

    bool slide(SimWorld& world, entt::entity person)
    {
        entt::registry& r = world.registry;
        if (!r.valid(person) || r.any_of<Linger, Swimmer>(person) || !r.all_of<Errand>(person))
            return false;
        if (const ErrandKind kind = r.get<Errand>(person).kind; kind == ErrandKind::Riding || kind == ErrandKind::Swimming)
            return false;
        if (const auto* staff = r.try_get<Staff>(person); staff != nullptr && staff->role == StaffRole::Vendor)
            return false;
        const Pose& pose = r.get<Pose>(person);
        throw_person(r, person, pose.position, { }, r.get<Stride>(person).to);
        auto* drop = r.try_get<Dropped>(person);
        if (drop == nullptr)
            return false;
        drop->thrown = false;
        drop->sliding = true;
        return true;
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

    bool departs(const Departure& departure, std::size_t riders, std::size_t capacity, float waited)
    {
        if (departure.min_wait >= 0 && waited < static_cast<float>(departure.min_wait))
            return false;
        if (departure.max_wait >= 0 && waited >= static_cast<float>(departure.max_wait))
            return true;
        // Quarters of the seats it waits for, the odd seat left over; any load is one rider.
        const auto quarters = 4 - static_cast<std::size_t>(departure.wait_for);
        return riders >= std::max<std::size_t>(capacity * quarters / 4, 1);
    }

    float cycle_seconds(const CycleRide& ride)
    {
        return ride.start.value + static_cast<float>(ride.loops) * ride.run.value + ride.stop.value;
    }

    std::optional<float> seconds_into_cycle(const CycleRide& ride)
    {
        switch (ride.phase)
        {
            case RidePhase::Loading:
                return std::nullopt;
            case RidePhase::Starting:
                return ride.in_phase.value;
            case RidePhase::Running:
                return ride.start.value + static_cast<float>(ride.loop) * ride.run.value + ride.in_phase.value;
            case RidePhase::Stopping:
                return ride.start.value + static_cast<float>(ride.loops) * ride.run.value + std::min(ride.in_phase.value, ride.stop.value);
        }
        return std::nullopt;
    }

    bool advance_cycle(CycleRide& ride, float dt, bool held)
    {
        if (ride.phase == RidePhase::Loading)
            return false;
        ride.in_phase.value += dt;
        // Time left over from one phase goes on into the next, so a cycle lasts as long as its parts add up to.
        if (ride.phase == RidePhase::Starting && ride.in_phase.value >= ride.start.value)
        {
            ride.in_phase.value -= ride.start.value;
            ride.phase = RidePhase::Running;
            ride.loop = 0;
        }
        if (ride.phase == RidePhase::Running)
        {
            const auto passed = ride.run.value > 0 ? static_cast<std::uint32_t>(ride.in_phase.value / ride.run.value) : ride.loops;
            const std::uint32_t done = std::min(passed, ride.loops - std::min(ride.loop, ride.loops));
            ride.loop += done;
            ride.in_phase.value -= static_cast<float>(done) * ride.run.value;
            if (ride.loop >= ride.loops)
                ride.phase = RidePhase::Stopping;
        }
        if (ride.phase == RidePhase::Stopping && ride.in_phase.value >= ride.stop.value && !held)
        {
            ride.phase = RidePhase::Loading;
            ride.in_phase.value = 0;
            ride.loop = 0;
            return true;
        }
        return false;
    }

    float room_ahead(std::span<const Coaster> trains, std::size_t t)
    {
        const Coaster& train = trains[t];
        const float length = train.length();
        float room = std::numeric_limits<float>::infinity();
        for (std::size_t k = 0; k < trains.size(); ++k)
        {
            // Gone off a pool slide's end, a train is in no one's way; on a track with two ends, nor is one behind.
            if (k == t || length <= 0 || trains[k].splashed || (!train.closed && trains[k].at < train.at))
                continue;
            // How far ahead the back of train k is, round the track.
            const float ahead = std::fmod(std::fmod(trains[k].at - train.at, length) + length, length);
            room = std::min(room, std::max(ahead - trains[k].train_length - kBlockGap, 0.0f));
        }
        if (!train.blocks || length <= 0)
            return room;
        // Block ends: where a run of station, lift or block brake (not plain brakes) gives way to other track. A train stopped on one
        // (within kArrived) still counts it as ahead.
        const auto ahead = [&](float d) { return std::fmod(std::fmod(d - train.at + kArrived, length) + length, length) - kArrived; };
        // Only the nearest two ends ahead matter: the next, and the one closing the block beyond it.
        std::array<float, 2> ends { };
        std::array<float, 2> ends_ahead { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
        const auto end_at = [&](float d) {
            const float a = ahead(d);
            if (a < ends_ahead[0])
            {
                ends = { d, ends[0] };
                ends_ahead = { a, ends_ahead[0] };
            }
            else if (a < ends_ahead[1])
            {
                ends[1] = d;
                ends_ahead[1] = a;
            }
        };
        for (std::size_t i = 0; i + 2 < train.kinds.size(); ++i) // The last kind is past the track's end
            if (const Stretch k = train.kinds[i]; k != Stretch::Plain && k != Stretch::Launch && k != Stretch::Brake && k != Stretch::Booster && train.kinds[i + 1] != k)
                end_at(train.distance[i + 1]);
        if (const Stretch last = train.kinds[train.kinds.size() - 2]; last != Stretch::Plain && last != Stretch::Launch && last != Stretch::Brake && last != Stretch::Booster && train.kinds.front() != last)
            end_at(0); // The track's end runs into its start
        if (std::isinf(ends_ahead[1]))
            return room;
        // The block beyond the next end must hold no other train, not even a car of one.
        const float end = ends[0];
        const float block = std::fmod(std::fmod(ends[1] - end, length) + length, length);
        for (std::size_t k = 0; k < trains.size(); ++k)
            if (k != t && std::fmod(std::fmod(trains[k].at - end, length) + length, length) - trains[k].train_length < block)
                return std::min(room, std::max(ahead(end), 0.0f));
        return room;
    }

    void step(SimWorld& world, std::optional<Workers&> workers)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
        const float now = static_cast<float>(world.tick) * kTickSeconds;

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

        phase.emplace("Let go");
        // Those the player let go of fall to the ground, then walk straight to the path they go on from. Those
        // a crash threw fly on as they fall, unslowed, and lie where they land till they get up.
        const auto* surroundings = r.ctx().find<WreckSurroundings>();
        for (const entt::entity e : sorted(r.view<Dropped>()))
        {
            auto& drop = r.get<Dropped>(e);
            auto& pose = r.get<Pose>(e);
            // Sliding, the slope pulls them down it and the ground's rub slows them, till they stop on gentler
            // ground, fall over and look for the nearest path.
            if (drop.sliding)
            {
                const auto& slides = kTuning.slides;
                const float gravity = kTuning.holding.gravity;
                const glm::vec2 rise = surroundings != nullptr ? slope(surroundings->ground, { pose.position.x, pose.position.z }) : glm::vec2 { };
                const float lean = 1 + glm::dot(rise, rise); // The steeper, the less of their weight presses on the ground
                drop.drift -= rise * (gravity / lean * dt);
                const float speed = glm::length(drop.drift);
                const float rub = slides.grip * gravity / lean * dt;
                drop.drift = speed > rub ? drop.drift * (1 - rub / speed) : glm::vec2 { };
                pose.position += glm::vec3 { drop.drift.x, 0, drop.drift.y } * dt;
                drop.ground = surroundings != nullptr && surroundings->ground ? surroundings->ground({ pose.position.x, pose.position.z }) : pose.position.y;
                pose.position.y = drop.ground;
                if ((glm::length(drop.drift) < slides.settled.value && glm::length(rise) <= slides.steep) || (drop.slid.value += dt) >= slides.longest.value)
                {
                    drop.sliding = false;
                    drop.drift = { };
                    drop.node = path_to_rejoin(paths, pose.position).value_or(drop.node);
                    // Staff have no motion for lying down, so they only stand up again.
                    if (r.all_of<Visitor>(e))
                        r.emplace_or_replace<Gesture>(e, GestureKind::Down, kTuning.crashes.knocked);
                }
                continue;
            }
            if (drop.thrown || pose.position.y > drop.ground)
            {
                drop.fall.value += kTuning.holding.gravity * dt;
                if (!drop.thrown)
                    drop.fall.value = std::min(drop.fall.value, kTuning.holding.fastest_fall.value);
                glm::vec2 across = glm::vec2 { pose.position.x, pose.position.z } + drop.drift * dt;
                // Off a slide's end into a pool, its far wall stops them, and they go on only down.
                const auto* door = r.valid(drop.pool) ? r.try_get<PoolDoor>(drop.pool) : nullptr;
                if (const auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<SwimPool>(door->pool) : nullptr; pool != nullptr && r.valid(drop.slide))
                    if (const glm::vec2 stop = pool_stop(*pool, { pose.position.x, pose.position.z }, across, paths.tile_size); stop != across)
                    {
                        across = stop;
                        drop.drift = { };
                    }
                pose.position.x = across.x;
                pose.position.z = across.y;
                if (drop.thrown && surroundings != nullptr && surroundings->ground)
                    drop.ground = surroundings->ground({ pose.position.x, pose.position.z });
                pose.position.y = std::max(pose.position.y - drop.fall.value * dt, drop.ground);
                if (pose.position.y <= drop.ground && drop.fall.value >= 0 && drop.thrown)
                {
                    drop.thrown = false;
                    drop.drift = { };
                    r.emplace_or_replace<Gesture>(e, GestureKind::Down, kTuning.crashes.knocked);
                }
                continue;
            }
            if (r.all_of<Gesture>(e))
                continue;
            // Landed in a pool complex: they swim there, in their swimwear, as if they had paid and changed at
            // its room, and leave by it. Away from their party, they go on alone after.
            if (const auto* door = r.valid(drop.pool) ? r.try_get<PoolDoor>(drop.pool) : nullptr; door != nullptr && r.all_of<Attraction>(drop.pool))
            {
                const entt::entity room = drop.pool;
                const NodeId door_node = r.get<Attraction>(room).entrance != kNoNode ? r.get<Attraction>(room).entrance : drop.node;
                const glm::vec3 landed = pose.position;
                const Dice dice = r.get<Dice>(e);
                const auto* swam = r.try_get<Swimmer>(e); // Off one of its slides, with the time they had left
                const Seconds left = swam != nullptr ? swam->left : kTuning.pools.stay;
                r.remove<Dropped>(e);
                start_walking(world, e, door_node);
                r.get<Dice>(e) = dice;
                pose.position = pose.was = landed;
                r.get<Errand>(e) = { .kind = ErrandKind::Swimming, .target = room };
                // In the water, or on the paving round it, as they came down.
                const auto* pool = r.valid(door->pool) ? r.try_get<SwimPool>(door->pool) : nullptr;
                const bool wet = pool != nullptr && std::ranges::any_of(pool->water, [&](const glm::vec3& w) { return std::abs(w.x - landed.x) <= paths.tile_size / 2 && std::abs(w.z - landed.z) <= paths.tile_size / 2; });
                r.emplace_or_replace<Swimmer>(e, Swimmer { .changed = true, .left = left, .goal = landed, .in_water = wet });
                continue;
            }
            const glm::vec3 to = paths.centre(drop.node) - pose.position;
            const float left = glm::length(glm::vec2(to.x, to.z));
            if (left > kLaneReach)
            {
                const auto* visitor = r.try_get<Visitor>(e);
                const float pace = (visitor != nullptr ? kTuning.walking.speed[std::to_underlying(visitor->age)] : Stride { }.speed).value * dt;
                pose.heading.value = std::atan2(to.x, to.z);
                pose.position += glm::vec3 { to.x, 0, to.z } * (std::min(pace, left) / left); // Over the ground they landed on
                continue;
            }
            const NodeId node = drop.node;
            const Dice dice = r.get<Dice>(e); // Going on as they were, not from the start again
            r.remove<Dropped>(e);
            start_walking(world, e, node);
            r.get<Dice>(e) = dice;
            // Back with their party, if it is still in the park: they walk over to where it waits (5.), or lead it.
            for (const auto [p, party] : r.view<Party>().each())
            {
                if (std::erase(party.away, e) == 0)
                    continue;
                party.members.push_back(e);
                r.emplace<Member>(e, p);
                if (party.leader == entt::null)
                    party.leader = e;
                else
                    r.get<Errand>(e) = { .kind = ErrandKind::Waiting };
                break;
            }
        }

        phase.emplace("Rides");
        // 4. Rides, in entity order: queues shuffle forward, riders get off, the next ones board.
        for (const entt::entity e : sorted(r.view<CycleRide>()))
        {
            auto& ride = r.get<CycleRide>(e);
            auto& attraction = r.get<Attraction>(e);
            auto& upkeep = r.get_or_emplace<Upkeep>(e);
            if (!r.all_of<Dice>(e))
                r.emplace<Dice>(e, dice_for(world, e));
            if (!upkeep.broken)
                upkeep.since_inspection.value += dt;
            if ((upkeep.sample_clock.value += dt) >= kDowntimeSampleSeconds)
            {
                upkeep.sample_clock.value -= kDowntimeSampleSeconds;
                upkeep.downtime[0] += upkeep.broken && upkeep.downtime[0] < kDowntimeSamples;
                if (++upkeep.samples >= kDowntimeSamples)
                {
                    std::shift_right(upkeep.downtime.begin(), upkeep.downtime.end(), 1);
                    upkeep.downtime[0] = 0;
                    upkeep.samples = 0;
                }
            }
            // A lazy river's riders float round it on their own (float_on_rivers), and a slide's climbed on foot
            // go up and down it on their own (climb_slides).
            if (r.any_of<River, ClimbSlide>(e))
                continue;
            // Out of the queue at the station's entrance; a swimmer out of a pool slide's swims on from where they stand.
            const auto leave_queue = [&](entt::entity guest, NodeId entrance) {
                Errand& errand = r.get<Errand>(guest);
                errand = after_ride(r, guest, e);
                if (errand.kind != ErrandKind::Swimming)
                    stop_at(paths, entrance, r.get<Stride>(guest), r.get<Pose>(guest));
            };
            // Each station's queue in turn.
            for (std::size_t station = 0; station <= ride.stops.size(); ++station)
            {
                auto& queue = station == 0 ? ride.queue : ride.stops[station - 1].queue;
                const auto& line = station == 0 ? ride.line : ride.stops[station - 1].line;
                const NodeId entrance = entrance_of(r, e, station);
                for (const entt::entity guest : queue)
                    if (r.valid(guest) && (r.get<Errand>(guest).queued.value += dt) >= kLongQueue)
                        think(r, guest, ThoughtKind::QueueingForAges, e, now);
                // Unhappy guests who have waited very long now and then give up and walk off.
                std::erase_if(queue, [&](entt::entity guest) {
                    if (!r.valid(guest) || r.get<Errand>(guest).queued.value < kTuning.queues.give_up_after.value || r.get<Mood>(guest).happiness.value >= kTuning.queues.give_up_mood.value || !random::chance(r.get<Dice>(guest).state, kTuning.queues.give_up.value * dt))
                        return false;
                    think(r, guest, ThoughtKind::GaveUpQueue, e, now);
                    leave_queue(guest, entrance);
                    return true;
                });
                if (!line.empty())
                {
                    const float last = static_cast<float>(line.size() - 1);
                    float side_ahead = 0;
                    float along_ahead = -kTuning.queues.spacing.value; // Where the guest ahead stands now
                    for (std::size_t k = 0; k < queue.size(); ++k)
                    {
                        if (!r.valid(queue[k]))
                            continue;
                        auto& stride = r.get<Stride>(queue[k]);
                        enum : std::uint64_t
                        {
                            Side = 1,
                            Place = 2
                        };
                        // Guests brought in from a save have no side yet: across from the guest ahead too.
                        if (stride.side.value == 0)
                            stride.side.value = (side_ahead > 0 ? -kTuning.queues.reach.value : kTuning.queues.reach.value) * (0.2f + 0.8f * fixed_share(queue[k], Side));
                        side_ahead = stride.side.value;
                        // Off their even place by a little, so the line is not spaced like a ruler.
                        // Never nearer the guest ahead than the closest gap, wherever they stand: one still waiting for
                        // their own gap to open is not walked up past or into.
                        const float even = (static_cast<float>(k) + kTuning.queues.gap_spread.value * (fixed_share(queue[k], Place) - 0.5f)) * kTuning.queues.spacing.value;
                        const float place_in_line = std::clamp(std::max(even, along_ahead + kTuning.queues.spacing.value * (1 - kTuning.queues.gap_spread.value)), 0.0f, last);
                        // Up to the place behind the guest ahead, in shuffles: a guest standing waits for a gap.
                        const bool was_moving = stride.from != stride.to;
                        const bool moving = stride.along.value > place_in_line && (was_moving || stride.along.value - place_in_line > kTuning.queues.shuffle.value);
                        if (moving)
                            stride.along.value = std::max(stride.along.value - stride.speed.value * dt / paths.tile_size, place_in_line);
                        along_ahead = stride.along.value;
                        // Between the tile behind and the one ahead, facing the front.
                        const auto behind = static_cast<std::size_t>(std::ceil(stride.along.value));
                        const auto ahead = static_cast<std::size_t>(std::floor(stride.along.value));
                        stride.from = line[behind];
                        stride.to = moving && stride.along.value > place_in_line ? line[ahead] : line[behind];
                        stride.progress.value = static_cast<float>(behind) - stride.along.value;
                        auto& pose = r.get<Pose>(queue[k]);
                        pose.position = glm::mix(paths.centre(line[behind]), paths.centre(line[ahead]), stride.progress.value);
                        // To one side of the line, across the way it runs here (on a tile's middle, the way on to the front).
                        const std::size_t from = ahead != behind ? behind : std::min(behind + 1, line.size() - 1);
                        const std::size_t to = ahead != behind ? ahead : (behind > 0 ? behind - 1 : 0);
                        const glm::vec3 d = paths.centre(line[to]) - paths.centre(line[from]);
                        if (from != to && glm::length(glm::vec2(d.x, d.z)) > 0)
                        {
                            const glm::vec2 way = glm::normalize(glm::vec2(d.x, d.z));
                            pose.position.x += -way.y * stride.side.value;
                            pose.position.z += way.x * stride.side.value;
                            pose.heading.value = std::atan2(way.x, way.y);
                        }
                        pose.position.y = paths.surface(line[behind], line[ahead], { pose.position.x, pose.position.z });
                    }
                }
            }
            const auto* walks = r.try_get<RideWalks>(e);
            // Those getting off at `station`: where they meant to, or back where they got on.
            // `shift`: from where the train stands at the station's end to where it stands now.
            // Of the step a pool slide's train ran off its end in, the time left after it got there.
            const auto off_for = [&](const Coaster& c) { return std::clamp(dt - (c.at - c.was_at) / std::max(c.speed, 0.1f), 0.0f, dt); };
            const auto unload = [&](std::vector<entt::entity>& riders, std::size_t station, glm::vec3 shift = glm::vec3(0.0f), const Coaster* splashed = nullptr) {
                std::vector<bool> off(riders.size());
                float side_ahead = 0;
                for (std::size_t seat = 0; seat < riders.size(); ++seat)
                {
                    const entt::entity guest = riders[seat];
                    if (!r.valid(guest))
                    {
                        off[seat] = true;
                        continue;
                    }
                    const Errand& riding = r.get<Errand>(guest);
                    if (riding.off != station && riding.station != station)
                        continue;
                    off[seat] = true;
                    auto& mood = r.get<Mood>(guest);
                    const float felt = felt_excitement(ride.excitement.value, r.all_of<Trains>(e), age_of(r, guest), night);
                    mood.happiness.value = clamp01(mood.happiness.value + felt * 0.3f);
                    mood.nausea.value = clamp01(mood.nausea.value + ride.nausea.value * 0.2f * (1.5f - mood.tolerance.value) * (eating(r, guest) ? kTuning.sickness.full_stomach.value : 1));
                    // shortcut: soaked as they get off, not the moment their car splashes (park/car_splashes.hpp);
                    // upgrade when the simulation follows its cars through the water.
                    if (ride.splash)
                    {
                        mood.wet.value = std::max(mood.wet.value, kTuning.weather.splash.value);
                        mood.heat.value = 0;
                    }
                    auto& dice = r.get<Dice>(guest).state;
                    std::optional<GestureKind> gesture;
                    if (felt >= kTuning.thoughts.enjoyed.value)
                    {
                        think(r, guest, ThoughtKind::Enjoyed, e, now);
                        constexpr std::array kGlad { GestureKind::Cheer, GestureKind::Cheer, GestureKind::Laugh };
                        gesture = kGlad[random::below(dice, kGlad.size())];
                    }
                    else if (felt >= kTuning.gestures.liked.value)
                        gesture = GestureKind::Nod;
                    const Taste taste = taste_of(r, guest);
                    if (ride.intensity.value > std::min(mood.tolerance.value + kTuning.sickness.wildest.value, taste.most.value))
                    {
                        think(r, guest, ThoughtKind::TooIntense, e, now);
                        gesture = GestureKind::Dizzy;
                    }
                    else if (ride.intensity.value < taste.least.value)
                    {
                        think(r, guest, ThoughtKind::TooTame, e, now);
                        gesture = GestureKind::Grumble;
                    }
                    // Off the seat and out on foot where the ride has a way out; still riding till on the paths.
                    // shortcut: only the first station has ways off worked out, upgrade when the others' are
                    if (station == 0 && walks != nullptr && seat < walks->off.size() && !walks->off[seat].empty())
                    {
                        RideWalk walk { .points = walks->off[seat], .off = true, .gesture = gesture };
                        walk.points.front() += shift;
                        walk.points.push_back(paths.centre(exit_of(r, e)));
                        // Each to a side of the way out as in a queue, so a load spills out abreast, not in one file:
                        // from past the seat, across the way on from each point, back to the middle on the path.
                        side_ahead = side_after(dice, side_ahead);
                        for (std::size_t k = 1; k + 1 < walk.points.size(); ++k)
                        {
                            const glm::vec3 d = walk.points[k + 1] - walk.points[k];
                            if (glm::length(glm::vec2(d.x, d.z)) > 0)
                            {
                                const glm::vec2 way = glm::normalize(glm::vec2(d.x, d.z));
                                walk.points[k] += glm::vec3(-way.y, 0, way.x) * (side_ahead * kTuning.queues.way_out.value);
                            }
                        }
                        r.get<Pose>(guest).position = walk.points.front();
                        r.emplace_or_replace<RideWalk>(guest, std::move(walk));
                    }
                    else if (splashed != nullptr)
                    {
                        // Off the end of a pool slide they fly on as fast as their car went, splash down into the
                        // water and swim on there; with no pool under the end, they land on the ground.
                        const glm::vec3 at = track_point(*splashed, splashed->at);
                        const glm::vec3 velocity = track_way(*splashed, splashed->at) * splashed->speed;
                        const std::optional<NodeId> node = path_to_rejoin(paths, at);
                        const auto door = pool_door_at(world, at);
                        const auto* pool = door ? r.try_get<SwimPool>(r.get<PoolDoor>(*door).pool) : nullptr;
                        if (pool == nullptr || pool->water.empty() || !node)
                        {
                            throw_person(r, guest, at, velocity, node.value_or(kNoNode));
                            // Thrown onto hard ground, not into water: the slide is shut as dangerous, and the park's
                            // inspector, if it has one, says so.
                            if (attraction.open && (pool == nullptr || pool->water.empty()))
                                for (const auto [i, staff] : r.view<const Staff>().each())
                                    if (staff.role == StaffRole::Inspector)
                                    {
                                        tell(r, i, Finding::DangerousFlume, e, now);
                                        break;
                                    }
                            if (pool == nullptr || pool->water.empty())
                                attraction.open = false;
                            continue;
                        }
                        const auto nearest = std::ranges::min_element(pool->water, { }, [&](const glm::vec3& w) { return glm::distance(glm::vec2(w.x, w.z), glm::vec2(at.x, at.z)); });
                        step_out(r, guest);
                        r.remove<Errand, Stride, Linger, Gesture, Watching, RideWalk>(guest);
                        auto& pose = r.get<Pose>(guest);
                        // From where their car was as the step began, as it is drawn, not still at the end for a step.
                        pose.was = track_point(*splashed, splashed->was_at);
                        pose.position = at + velocity * off_for(*splashed);
                        pose.heading.value = std::atan2(velocity.x, velocity.z);
                        r.emplace<Dropped>(guest, Dropped { .ground = nearest->y, .fall = { -velocity.y }, .drift = { velocity.x, velocity.z }, .node = *node, .swimwear = true, .pool = *door, .slide = e });
                    }
                    else if (auto* swimmer = exit_of(r, e, station) == kNoNode ? r.try_get<Swimmer>(guest) : nullptr; swimmer != nullptr && r.valid(swimmer->room))
                    {
                        // Back round to the station of a slide with no way out to the paths: off onto the paving or
                        // steps they got on from, to swim on.
                        r.get<Errand>(guest) = { .kind = ErrandKind::Swimming, .target = std::exchange(swimmer->room, entt::null) };
                        r.get<Pose>(guest).position = swimmer->goal;
                        swimmer->in_water = false;
                    }
                    else
                        let_out(r, paths, guest, e, gesture);
                }
                std::size_t seat = 0;
                std::erase_if(riders, [&](entt::entity) { return off[seat++]; });
            };
            // Riders an hour over the time since the last departure, smoothed over the last few.
            if (ride.since_departure.value >= 0)
                ride.since_departure.value += dt;
            const auto depart = [&](std::size_t riding, Seconds& timer) {
                if (ride.since_departure.value > 0)
                {
                    const float rate = static_cast<float>(riding) * 3600 / ride.since_departure.value;
                    ride.per_hour = ride.per_hour > 0 ? std::lerp(ride.per_hour, rate, kTuning.wear.flow_smoothing.value) : rate;
                }
                ride.since_departure.value = 0;
                timer.value = 0;
                // Each run with riders wears the ride, and may end in a breakdown that closes it.
                if (riding == 0)
                    return;
                upkeep.reliability.value = std::max(upkeep.reliability.value - kTuning.wear.per_run.value, 0.0f);
                if (random::chance(r.get<Dice>(e).state, kTuning.wear.breakdown.value * (1 - upkeep.reliability.value)))
                {
                    // Any of the kinds a ride of its sort can have, alike.
                    using enum Breakdown;
                    constexpr std::array kCoaster { EmergencyStop, RestraintsFailToOpen, RestraintsFailToClose, TrainStuckLeavingStation, StationBrakesFail, ControlFailure };
                    constexpr std::array kFlat { RestraintsFailToOpen, RestraintsFailToClose, DoorsFailToOpen, DoorsFailToClose, ControlFailure };
                    const bool coaster = r.all_of<Trains>(e);
                    const std::size_t pick = random::below(r.get<Dice>(e).state, coaster ? kCoaster.size() : kFlat.size());
                    upkeep.kind = upkeep.last = coaster ? kCoaster[pick] : kFlat[pick];
                    upkeep.broken = true;
                    ++upkeep.breakdowns;
                    attraction.open = false;
                }
            };
            // The wrecks of crashed trains fly on and blow up, knocking over the guests on foot beside them. The
            // park remembers the last crash a while.
            const auto& crashes = kTuning.crashes;
            auto* wrecks = r.try_get<Wrecks>(e);
            if (wrecks != nullptr)
            {
                if (wrecks->since.value >= 0 && (wrecks->since.value += dt) >= crashes.forgotten.value)
                    wrecks->since.value = -1;
                const auto* surroundings = r.ctx().find<WreckSurroundings>();
                const WreckSurroundings around = surroundings != nullptr ? *surroundings : WreckSurroundings { };
                for (std::vector<Wreck>& cars : wrecks->trains)
                {
                    for (Wreck& wreck : cars)
                        if (fly(wreck, dt, around) == WreckEvent::BlewUp && !wreck.ghost)
                        {
                            // Thrown outwards and up, they land knocked over and walk back to the path they were on.
                            std::vector<entt::entity> blown;
                            for (const auto [g, pose, errand, visitor] : r.view<const Pose, const Errand, const Visitor>().each())
                                if (errand.kind != ErrandKind::Riding && errand.kind != ErrandKind::Queueing && glm::distance(pose.position, wreck.at) < crashes.blast.value)
                                    blown.push_back(g);
                            auto& dice = r.get<Dice>(e).state;
                            for (const entt::entity g : blown)
                            {
                                const glm::vec3 at = r.get<Pose>(g).position;
                                glm::vec2 away(at.x - wreck.at.x, at.z - wreck.at.z);
                                away = glm::length(away) > 0.01f ? glm::normalize(away) : glm::vec2(1, 0);
                                const float push = static_cast<float>(random::between(dice, 0.5, 1.0));
                                const glm::vec3 velocity(away.x * crashes.blown.value * push, crashes.lofted.value * push, away.y * crashes.blown.value * push);
                                throw_person(r, g, at, velocity, r.get<Stride>(g).to);
                                think(r, g, ThoughtKind::BeenInjured, e, now);
                            }
                        }
                    if (std::ranges::all_of(cars, [](const Wreck& w) { return w.state == WreckState::Gone; }))
                        cars.clear();
                }
            }
            // A cycle ends when the ride's time is up, or a train has come round.
            auto* fleet = r.try_get<Trains>(e);
            std::vector<std::optional<std::size_t>> standing(1 + ride.stops.size()); // The train standing at each station, ready to load
            if (fleet != nullptr)
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                {
                    Coaster& train = fleet->trains[t];
                    train.was_at = train.at;
                    if (upkeep.broken && upkeep.kind == Breakdown::EmergencyStop)
                        continue; // Halted on the track till fixed
                    // A crashed train is off the track till its wrecks are gone, and back in the station once the
                    // ride is opened or tested again.
                    // Off a pool slide's end, it is back at the station once nothing stands at its start there.
                    // shortcut: it is back the moment its start is clear, as if carried there; upgrade if a park
                    // shows the way back.
                    // Its car flies on off the end and floats, till it is gone.
                    if (train.flight && !fly(*train.flight, dt))
                        train.flight.reset();
                    const auto in_station = [&](const Coaster& other) { return &other != &train && !other.splashed && !other.crashed && std::abs(other.at - train.start) < train.train_length + kBlockGap; };
                    if (train.splashed && (train.flight || !(attraction.open || attraction.testing) || std::ranges::any_of(fleet->trains, in_station)))
                        continue;
                    if (train.crashed || train.splashed)
                    {
                        if ((wrecks != nullptr && t < wrecks->trains.size() && !wrecks->trains[t].empty()) || !(attraction.open || attraction.testing))
                            continue;
                        train.crashed = false;
                        train.splashed = false;
                        train.at = train.start;
                        train.was_at = train.at;
                        train.travelled = 0;
                        train.speed = 0;
                        train.slow = 0;
                        train.launched = false;
                        train.passed = 0;
                        train.push = 0;
                    }
                    const bool was_running = train.running;
                    const bool round = drive(train, dt, room_ahead(fleet->trains, t));
                    // The first train's laps, while tested or open, make the ride's graphs; a test train's on
                    // unfinished track never comes round.
                    if (t == 0 && (train.running || round))
                        record(fleet->lap, train, dt);
                    if (t == 0 && round)
                        fleet->lap_done = end_lap(fleet->lap);
                    if (t == 0 && train.crashed)
                        fleet->lap = { };
                    // On a slide's way back to its lift, its riders are let off into the pool and the car goes on empty.
                    if (!fleet->riders[t].empty() && std::ranges::any_of(train.drop_offs, [&](float d) { return train.was_at < d && train.at >= d; }))
                        unload(fleet->riders[t], 0, glm::vec3(0.0f), &train);
                    // Its riders react to the ride, seated in its cars front to back.
                    if (const auto& riders = fleet->riders[t]; !riders.empty() && !train.crashed)
                    {
                        enum : std::uint64_t
                        {
                            Hands = 21,
                            Brace = 22,
                            Eyes = 23
                        };
                        const Felt feel = felt(train);
                        const bool lifted = on_chain(train);
                        const bool moving = train.running && std::abs(train.speed) >= kTuning.reactions.still;
                        const std::size_t per_car = std::max<std::size_t>(ride.capacity / static_cast<std::size_t>(std::max(train.cars, 1)), 1);
                        for (std::size_t seat = 0; seat < riders.size(); ++seat)
                        {
                            const entt::entity guest = riders[seat];
                            const auto* mood = r.valid(guest) ? r.try_get<Mood>(guest) : nullptr;
                            if (mood == nullptr)
                                continue;
                            const auto* taste = r.try_get<Taste>(guest);
                            const float over = ride.intensity.value - (taste != nullptr ? taste->most.value : 1.0f);
                            const Leanings leanings { .hands = fixed_share(guest, Hands), .brace = fixed_share(guest, Brace), .eyes = fixed_share(guest, Eyes) };
                            auto& thrill = r.get_or_emplace<Thrill>(guest);
                            thrill.reaction = react(thrill.reaction, feel, moving, lifted, mood->happiness, over, leanings, seat < per_car, r.get<Dice>(guest).state, dt);
                        }
                    }
                    if (round && train.splashed)
                    {
                        // Its car flies on as fast as it went, down onto the pool's water under the end, or the
                        // ground with no pool there.
                        const glm::vec3 at = track_point(train, train.at);
                        const auto door = pool_door_at(world, at);
                        const auto* pool = door ? r.try_get<SwimPool>(r.get<PoolDoor>(*door).pool) : nullptr;
                        const auto* surroundings = r.ctx().find<WreckSurroundings>();
                        float water = surroundings != nullptr && surroundings->ground ? surroundings->ground({ at.x, at.z }) : 0.0f;
                        if (pool != nullptr && !pool->water.empty())
                            water = std::ranges::min(pool->water, { }, [&](const glm::vec3& w) { return glm::distance(glm::vec2(w.x, w.z), glm::vec2(at.x, at.z)); }).y;
                        const glm::vec3 way = track_way(train, train.at);
                        train.flight = Flight { .from = at, .way = way, .at = at, .velocity = way * train.speed, .water = std::min(water, at.y), .speed = train.speed };
                        // A pool's far wall stops it, its middle half a car short of it, however far it would fly.
                        constexpr float kFar = 100;   ///< Metres on it looks for the wall. Chosen by eye: past any slide's throw
                        constexpr float kHalfCar = 1; ///< Metres from a slide car's middle to its nose. Chosen by eye
                        if (const glm::vec2 level { way.x, way.z }; pool != nullptr && glm::length(level) > 1e-3f)
                        {
                            const glm::vec2 from { at.x, at.z };
                            const glm::vec2 far = from + glm::normalize(level) * kFar;
                            if (const glm::vec2 stop = pool_stop(*pool, from, far, world.paths.tile_size); stop != far)
                                train.flight->reach = std::max(glm::distance(from, stop) - kHalfCar, 0.0f);
                        }
                        // It flies the rest of the step it ran off in.
                        fly(*train.flight, off_for(train));
                        unload(fleet->riders[t], 0, glm::vec3(0.0f), &train);
                    }
                    const auto stop = round && !train.splashed ? std::optional<std::size_t> { 0 } : was_running ? standing_at(train) : std::nullopt;
                    // Held up in a station behind the train ahead, its riders get off where it stands, every car at
                    // once; it then moves up to load at the station's end.
                    const auto held = stop ? std::nullopt : held_in_station(train);
                    if ((stop && (round || *stop > 0)) || (held && !fleet->riders[t].empty()))
                    {
                        // Stopped at a station: hands down for getting off.
                        for (const entt::entity guest : fleet->riders[t])
                            if (auto* thrill = r.valid(guest) ? r.try_get<Thrill>(guest) : nullptr)
                                thrill->reaction = RideReaction::Waiting;
                        const float end = !held || *held == 0 ? train.start : train.start + train.stops[*held - 1];
                        unload(fleet->riders[t], stop.value_or(held.value_or(0)), held ? track_point(train, train.at) - track_point(train, end) : glm::vec3(0.0f));
                    }
                    // Run off its track, the train's cars fly on as wrecks, throwing out their riders, and the ride
                    // closes. Guests who see it think it a disaster. A ghost's crash is only shown: it is not the
                    // park's, and closes nothing.
                    if (train.crashed)
                    {
                        wrecks = &r.get_or_emplace<Wrecks>(e);
                        wrecks->trains.resize(fleet->trains.size());
                        std::vector<Wreck>& thrown = wrecks->trains[t];
                        auto& dice = r.get<Dice>(e).state;
                        thrown = thrown_cars(train, dice);
                        if (train.ghost)
                            continue;
                        const auto spread = [&] { return static_cast<float>(random::between(dice, -1.0, 1.0)); };
                        const std::size_t cars = thrown.size();
                        // Riders fill the cars front to back.
                        std::vector<entt::entity>& riders = fleet->riders[t];
                        const std::size_t seats = std::max<std::size_t>((ride.capacity + cars - 1) / cars, 1);
                        wrecks->dangers += (attraction.open ? 1 : 0) + static_cast<std::uint32_t>(std::min(cars, (riders.size() + seats - 1) / seats));
                        ++wrecks->crashes;
                        wrecks->since.value = 0;
                        // Riders fly out of their car as fast as it went, though never thrown downwards, land
                        // knocked over and, once up, walk back to the ride's exit.
                        // shortcut: they fly through what stands in the park and walk straight back over whatever
                        // lies between; upgrade when people walk off the paths.
                        for (std::size_t seat = 0; seat < riders.size(); ++seat)
                        {
                            const entt::entity guest = riders[seat];
                            if (!r.valid(guest))
                                continue;
                            let_out(r, paths, guest, e, std::nullopt);
                            const Wreck& car = thrown[std::min(seat / seats, cars - 1)];
                            glm::vec3 velocity = car.velocity + glm::vec3(spread(), spread(), spread()) * crashes.scatter.value;
                            velocity.y = std::max(velocity.y, 0.0f);
                            throw_person(r, guest, car.at, velocity, r.get<Stride>(guest).to);
                            think(r, guest, ThoughtKind::BeenInjured, e, now);
                            ++wrecks->injured;
                        }
                        riders.clear();
                        for (const auto [g, pose, visitor, dice] : r.view<const Pose, const Visitor, const Dice>().each())
                            if (glm::distance(pose.position, thrown.front().at) < crashes.seen.value)
                                think(r, g, ThoughtKind::Disaster, e, now);
                        attraction.open = false;
                        attraction.testing = false;
                    }
                    if (const auto stop = standing_at(train); stop && *stop < standing.size() && !standing[*stop])
                        standing[*stop] = t;
                }
            }
            else
            {
                // Done, but with its restraints or doors stuck shut, its riders cannot get off till it is fixed.
                const bool held = upkeep.broken && (upkeep.kind == Breakdown::RestraintsFailToOpen || upkeep.kind == Breakdown::DoorsFailToOpen);
                // Boats out on the water keep their riders till the last is back at the dock.
                auto* boats = r.try_get<Boats>(e);
                const auto* water = r.ctx().find<WaterTiles>();
                bool sailing = false;
                if (boats != nullptr && water != nullptr)
                    for (Boat& boat : boats->boats)
                    {
                        sail(boat, *water, boats->dock, boats->handling, boats->top.value_or(kTuning.boats.top_speed.value), ride.run.value, dt, r.get<Dice>(e).state, boats->boats, boats->bumps);
                        sailing = sailing || boat.state != Sailing::Docked;
                    }
                const RidePhase was = ride.phase;
                if (advance_cycle(ride, dt, held || sailing))
                    unload(ride.riders, 0);
                // Its riders react to the ride while it runs.
                for (const entt::entity guest : ride.riders)
                    if (const auto* mood = r.valid(guest) && !r.all_of<RideWalk>(guest) ? r.try_get<Mood>(guest) : nullptr)
                    {
                        enum : std::uint64_t
                        {
                            Hands = 21,
                            Eyes = 23
                        };
                        const auto* taste = r.try_get<Taste>(guest);
                        const float over = ride.intensity.value - (taste != nullptr ? taste->most.value : 1.0f);
                        auto& thrill = r.get_or_emplace<Thrill>(guest);
                        thrill.reaction = react_flat(thrill.reaction, ride.phase == RidePhase::Running, mood->happiness, over, Leanings { .hands = fixed_share(guest, Hands), .eyes = fixed_share(guest, Eyes) }, r.get<Dice>(guest).state, dt);
                    }
                // Its boats cast off one after another, each once the one before is clear of the dock.
                // shortcut: empty boats go too, and a transport boat's riders stay on past its other docks back to
                // the one they boarded at; upgrade when each boat loads on its own and riders get off where they choose.
                if (boats != nullptr && water != nullptr && was != RidePhase::Running && ride.phase == RidePhase::Running)
                    boats->launched = 0;
                if (boats != nullptr && water != nullptr && ride.phase == RidePhase::Running && boats->launched < boats->boats.size())
                    if (boats->launched == 0 || glm::distance(boats->boats[boats->launched - 1].at, boats->dock.leave) > kTuning.boats.clear_of_dock.value)
                        cast_off(boats->boats[boats->launched++], boats->dock, boats->roams, boats->calls);
            }
            // On a ride with ways on, riders walk to their seats one by one past the ticket spot, the next
            // stepping up once the one ahead has handed over their ticket.
            const auto on_their_way = [&](entt::entity g) {
                const auto* walk = r.valid(g) ? r.try_get<RideWalk>(g) : nullptr;
                return walk != nullptr && !walk->off;
            };
            const auto at_ticket = [&](entt::entity g) {
                const auto* walk = r.valid(g) ? r.try_get<RideWalk>(g) : nullptr;
                return on_their_way(g) && (walk->next == 0 || walk->pause.value > 0);
            };
            // Loads once the first in line has reached the front, as many as there are seats left, and leaves when
            // its departure settings say.
            // shortcut: the rest of a load boards from wherever they stand in the line, upgrade when guests are shown boarding.
            // On a ride whose trains stop at more than one station, each station's in turn, riders only walking on at
            // the first.
            for (std::size_t station = 0; station <= ride.stops.size(); ++station)
            {
                auto& queue = station == 0 ? ride.queue : ride.stops[station - 1].queue;
                const auto& line = station == 0 ? ride.line : ride.stops[station - 1].line;
                Seconds& timer = station == 0 ? ride.loading : ride.stops[station - 1].loading;
                const bool walk_on = station == 0 && walks != nullptr && !walks->on.empty();
                const bool at_front = !queue.empty() && (line.empty() || !r.valid(queue.front()) || r.get<Stride>(queue.front()).along.value < 0.5f);
                const bool ready = fleet != nullptr ? standing[station].has_value() : ride.phase == RidePhase::Loading;
                auto* riders = !ready ? nullptr : fleet != nullptr ? &fleet->riders[*standing[station]] : &ride.riders;
                if (riders != nullptr && at_front && riders->size() < ride.capacity && !(walk_on && std::ranges::any_of(*riders, at_ticket)))
                {
                    std::size_t boarding = std::min<std::size_t>(ride.capacity - riders->size(), queue.size());
                    // A party in line rides on one load: cut through by the seats left, it waits for the next when it
                    // fits one.
                    if (boarding < queue.size())
                    {
                        const auto party_of = [&](entt::entity g) {
                            const auto* m = r.valid(g) ? r.try_get<Member>(g) : nullptr;
                            return m != nullptr ? m->party : entt::null;
                        };
                        if (const entt::entity cut = party_of(queue[boarding]); cut != entt::null && boarding > 0 && party_of(queue[boarding - 1]) == cut)
                        {
                            std::size_t first = boarding;
                            while (first > 0 && party_of(queue[first - 1]) == cut)
                                --first;
                            std::size_t last = boarding;
                            while (last < queue.size() && party_of(queue[last]) == cut)
                                ++last;
                            auto& count = r.ctx().emplace<PartyBoarding>();
                            if (last - first <= ride.capacity)
                            {
                                boarding = first;
                                ++count.held;
                            }
                            else
                                ++count.split;
                        }
                    }
                    if (walk_on)
                        boarding = std::min<std::size_t>(boarding, 1);
                    for (std::size_t i = 0; i < boarding; ++i)
                    {
                        const entt::entity guest = queue[i];
                        if (!r.valid(guest))
                            continue;
                        // An inspector rides free, telling the player when they queued for ages; a swimmer rides a pool
                        // slide free, on the complex's ticket.
                        const bool inspector = r.all_of<Staff>(guest);
                        const Money price = r.all_of<Swimmer>(guest) ? Money { } : attraction.price;
                        if (!attraction.open || (!inspector && !pay(r, guest, price, &Wallet::spent_rides)))
                        {
                            if (attraction.open)
                            {
                                think(r, guest, ThoughtKind::CantAfford, e, now);
                                r.get<Wallet>(guest).needs_cash = true;
                            }
                            r.get<Errand>(guest) = after_ride(r, guest, e);
                            continue;
                        }
                        if (!inspector)
                        {
                            attraction.income.value += price.value;
                            ++attraction.customers;
                        }
                        else if (r.get<Errand>(guest).queued.value >= kLongQueue)
                            tell(r, guest, Finding::LongQueue, e, now);
                        r.get<Errand>(guest).kind = ErrandKind::Riding;
                        if (const std::size_t seat = riders->size(); walk_on && seat < walks->on.size())
                        {
                            RideWalk walk { .points = { walks->ticket }, .pause = kTuning.queues.ticket.value };
                            walk.points.insert(walk.points.end(), walks->on[seat].begin(), walks->on[seat].end());
                            r.emplace_or_replace<RideWalk>(guest, std::move(walk));
                        }
                        riders->push_back(guest);
                    }
                    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(boarding));
                }
                if (riders != nullptr && !upkeep.broken)
                {
                    const auto* departure = r.try_get<Departure>(e);
                    // It waits for those still walking to their seats.
                    bool go = std::ranges::none_of(*riders, on_their_way) && (attraction.testing || departs(departure != nullptr ? *departure : Departure { }, riders->size(), ride.capacity, timer.value));
                    // Told to, a ready train waits for one ready at each station beside it told the same; then all go.
                    // shortcut: only the stations right beside count, not those beside them in turn; upgrade if a row of
                    // three or more synced stations turns up in a park.
                    const auto* beside = r.try_get<StationNeighbours>(e);
                    if (go && station == 0 && fleet != nullptr && !attraction.testing && departure != nullptr && departure->sync && beside != nullptr)
                    {
                        const auto told = [&](entt::entity b) {
                            const auto* d = r.valid(b) ? r.try_get<Departure>(b) : nullptr;
                            return d != nullptr && d->sync && r.all_of<CycleRide>(b);
                        };
                        ride.synced = true;
                        go = ride.released || std::ranges::all_of(beside->rides, [&](entt::entity b) { return !told(b) || r.get<CycleRide>(b).synced; });
                        if (go && !ride.released)
                            for (const entt::entity b : beside->rides)
                                if (told(b))
                                    r.get<CycleRide>(b).released = true;
                    }
                    if (go)
                    {
                        ride.synced = false;
                        ride.released = false;
                        depart(static_cast<std::size_t>(std::ranges::count_if(*riders, [&](entt::entity g) { return r.valid(g) && r.get<Errand>(g).station == station; })), timer);
                        if (fleet != nullptr)
                            fleet->trains[*standing[station]].running = true;
                        else
                        {
                            // shortcut: every flat ride loads all at once and runs its loops; wheels that turn a step per
                            // pair seated, shows and rides in sections are left out, upgrade with the ride menu's setups.
                            ride.phase = RidePhase::Starting;
                            ride.in_phase.value = 0;
                        }
                    }
                    else
                        timer.value += dt;
                }
            }
            for (std::size_t station = 0; station <= ride.stops.size(); ++station)
            {
                auto& queue = station == 0 ? ride.queue : ride.stops[station - 1].queue;
                const NodeId entrance = entrance_of(r, e, station);
                // In the rain, guests with nothing over them leave an open-air ride's queue.
                if (weather.rain && ride.cover.value < kTuning.weather.covered.value)
                {
                    std::erase_if(queue, [&](entt::entity guest) {
                        if (!r.valid(guest) || r.all_of<Umbrella>(guest) || paths.nodes()[r.get<Stride>(guest).from].covered || !random::chance(r.get<Dice>(guest).state, kTuning.weather.rain_leave.value * (weather.storm ? 3 : 1) * dt))
                            return false;
                        think(r, guest, ThoughtKind::RainKeepsMeOff, e, now);
                        leave_queue(guest, entrance);
                        return true;
                    });
                }
                // A broken ride's queue gives up, put out at the far end of the line.
                if (upkeep.broken)
                {
                    for (const entt::entity guest : queue)
                    {
                        if (!r.valid(guest))
                            continue;
                        auto& mood = r.get<Mood>(guest);
                        mood.happiness.value = clamp01(mood.happiness.value - kTuning.wear.breakdown_gloom.value);
                        leave_queue(guest, entrance);
                    }
                    queue.clear();
                }
            }
            // When the player asks, a train standing at the start goes, empty if need be, once it has waited its least
            // and another is held up behind it (within a centimetre: positions far round a long track lose that much
            // to rounding).
            const auto* leaving = r.try_get<Departure>(e);
            if (fleet != nullptr && standing[0] && leaving != nullptr && leaving->leave_on_arrival && ride.loading.value >= static_cast<float>(leaving->min_wait) && !fleet->trains[*standing[0]].running && !upkeep.broken && std::ranges::none_of(fleet->riders[*standing[0]], on_their_way))
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                    if (t != *standing[0] && fleet->trains[t].running && room_ahead(fleet->trains, t) < 0.01f)
                    {
                        fleet->trains[*standing[0]].running = true;
                        depart(fleet->riders[*standing[0]].size(), ride.loading);
                    }
            }
        }

        // Parties wait for and gather their members (simulation/parties.cpp).
        step_parties(world, phase);

        phase.emplace("Tidy");
        tidy(world);
        phase.reset();
        ++world.tick;
    }

} // namespace rct3::simulation
