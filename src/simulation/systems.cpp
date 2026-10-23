// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/systems.hpp"

#include "profiling/profiler.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/staff_care.hpp"
#include "simulation/tuning.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
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

        float clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        /// The entities of `view`, in ascending order, so serial phases go the same way every time.
        template<typename View> std::vector<entt::entity> sorted(const View& view)
        {
            auto out = view | std::ranges::to<std::vector<entt::entity>>();
            std::ranges::sort(out);
            return out;
        }

        void share(std::optional<Workers&> workers, std::size_t count, const std::function<void(std::size_t, std::size_t)>& body)
        {
            if (workers)
                workers->for_ranges(count, body);
            else
                body(0, count);
        }

        struct Offer
        {
            entt::entity entity;
            AttractionKind kind;
            Product product;
            NodeId entrance;
            std::int64_t price;
            float intensity;
            float cover;
            Serve serve;
            bool splash;
        };

        std::vector<Offer> offers(const entt::registry& r)
        {
            std::vector<Offer> out;
            for (const entt::entity e : sorted(r.view<const Attraction>()))
            {
                const Attraction& a = r.get<Attraction>(e);
                if (!a.open || a.entrance == kNoNode)
                    continue;
                if (const auto* shop = r.try_get<Shop>(e))
                    out.push_back({ e, AttractionKind::Shop, shop->product, a.entrance, a.price, 0, 1, shop->serve, false });
                else if (const auto* ride = r.try_get<CycleRide>(e))
                    out.push_back({ e, AttractionKind::Ride, Product::Drink, a.entrance, a.price, ride->intensity, ride->cover, Serve::Any, ride->splash });
            }
            return out;
        }

        /// The nearest offer the guest can pay for that `wanted` picks, with the walk there.
        template<typename Pick> std::optional<std::pair<entt::entity, std::vector<NodeId>>> nearest_offer(const PathNetwork& paths, NodeId at, const std::vector<Offer>& offers, const Wallet& wallet, const Pick& pick)
        {
            std::vector<entt::entity> wanted;
            std::vector<NodeId> entrances;
            for (const Offer& o : offers)
            {
                if (o.price > wallet.cash || !pick(o))
                    continue;
                wanted.push_back(o.entity);
                entrances.push_back(o.entrance);
            }
            if (wanted.empty())
                return std::nullopt;
            auto found = paths.route_nearest(at, entrances);
            if (!found)
                return std::nullopt;
            return std::pair { wanted[found->first], std::move(found->second) };
        }

        /// How many guests the queue line of `ride` holds; no limit for a ride guests walk straight into.
        std::size_t queue_room(const CycleRide& ride)
        {
            if (ride.line.empty())
                return std::numeric_limits<std::size_t>::max();
            // Up to the middle of the line's far tile.
            return static_cast<std::size_t>(static_cast<float>(ride.line.size() - 1) / kTuning.queues.spacing) + 1;
        }

        /// Where the guest goes next: a shop or facility for the most pressing need there is one for, else now
        /// and then a ride. In the rain with no umbrella, a guest getting wet wants one first, and rides only
        /// under cover. On a hot day food and drink is best cold, on a cold day hot; a guest too hot wants
        /// something cold to eat or drink, or a ride that splashes them.
        std::optional<std::pair<entt::entity, std::vector<NodeId>>> choose(const PathNetwork& paths, NodeId at, const std::vector<Offer>& offers, const Needs& needs, const Mood& mood, const Wallet& wallet, Dice& dice, bool rained_on, float daring, const Taste& taste, float temperature)
        {
            struct Want
            {
                float need;
                Product product;
                bool cold = false; ///< Only something cold will do
            };
            const float hot = mood.heat;
            std::array<Want, 8> wants { {
                { rained_on && mood.wet > 0 ? 1.0f : 0.0f, Product::Umbrella },
                { needs.thirst, Product::Drink },
                { needs.hunger, Product::Food },
                { hot, Product::Drink, true },
                { hot, Product::Food, true },
                { needs.bladder, Product::Toilet },
                { mood.nausea, Product::FirstAid },
                { wallet.cash < kTuning.facilities.low_cash ? 1.0f : 0.0f, Product::CashMachine },
            } };
            std::ranges::stable_sort(wants, std::greater { }, &Want::need);
            const Serve suits = hot >= kTuning.needs.want || temperature >= kTuning.weather.hot ? Serve::Cold : temperature < kTuning.weather.cold ? Serve::Hot : Serve::Any;
            const Serve wrong = suits == Serve::Cold ? Serve::Hot : suits == Serve::Hot ? Serve::Cold : Serve::Any;
            // What suits the weather, else anything but what does not.
            const auto shop = [&](Product product, bool only_suits) { return nearest_offer(paths, at, offers, wallet, [&](const Offer& o) { return o.kind == AttractionKind::Shop && o.product == product && (only_suits ? o.serve == suits : o.serve != wrong || wrong == Serve::Any); }); };
            for (const Want& want : wants)
            {
                if (want.need < kTuning.needs.want)
                    break;
                if (want.cold)
                {
                    if (auto found = nearest_offer(paths, at, offers, wallet, [&](const Offer& o) { return o.kind == AttractionKind::Shop && o.product == want.product && o.serve == Serve::Cold; }))
                        return found;
                    continue;
                }
                if (suits != Serve::Any)
                    if (auto found = shop(want.product, true))
                        return found;
                if (auto found = shop(want.product, false))
                    return found;
            }
            if (mood.nausea >= kTuning.needs.max_nausea || !random::chance(dice.state, kTuning.needs.ride_chance))
                return std::nullopt;
            const float wildest = std::min(mood.tolerance + kTuning.sickness.wildest, taste.most) + daring;
            const float least_cover = rained_on ? kTuning.weather.covered : 0;
            const auto ride = [&](bool splash) {
                const auto suits = [&](const Offer& o) { return o.kind == AttractionKind::Ride && o.intensity <= wildest && o.intensity >= taste.least && o.cover >= least_cover && (!splash || o.splash); };
                // The nearest of those that come to mind, so a ride behind another on the same path still gets riders.
                if (auto found = nearest_offer(paths, at, offers, wallet, [&](const Offer& o) { return suits(o) && random::chance(dice.state, kTuning.needs.ride_considered); }))
                    return found;
                return nearest_offer(paths, at, offers, wallet, suits);
            };
            if (hot >= kTuning.needs.want)
                if (auto found = ride(true))
                    return found;
            return ride(false);
        }

        /// The next tile for a wandering guest at `at`, not back where they came from unless it is the only way.
        NodeId wander(const PathNetwork& paths, NodeId at, NodeId came, Dice& dice, const Patrol* patrol = nullptr)
        {
            std::array<NodeId, 4> ways { };
            std::size_t count = 0;
            for (const NodeId n : paths.nodes()[at].next)
                if (n != kNoNode && n != came && !paths.nodes()[n].queue && (patrol == nullptr || patrol->covers(paths.nodes()[n].tile)))
                    ways[count++] = n;
            if (count == 0)
                return came != kNoNode ? came : at;
            return ways[random::below(dice.state, count)];
        }

        glm::vec3 lane_point(const PathNetwork& paths, NodeId node, glm::vec2 lane)
        {
            return paths.centre(node) + glm::vec3(lane.x, 0.0f, lane.y);
        }

        /// A lane near `was`, so walkers drift across the path rather than zigzag from tile to tile.
        glm::vec2 new_lane(Dice& dice, glm::vec2 was)
        {
            const glm::vec2 drift { static_cast<float>(random::between(dice.state, -kTuning.walking.lane_drift, kTuning.walking.lane_drift)), static_cast<float>(random::between(dice.state, -kTuning.walking.lane_drift, kTuning.walking.lane_drift)) };
            return glm::clamp(was + drift, -kLaneReach, kLaneReach);
        }

        float step_length(const PathNetwork& paths, const Stride& s)
        {
            return glm::distance(lane_point(paths, s.from, s.lane_from), lane_point(paths, s.to, s.lane_to));
        }

        /// Takes a step just begun `left` world units along, what the tick's walk had left past the tile, so
        /// walkers keep their pace through tiles.
        void walk_on(const PathNetwork& paths, Stride& s, float left)
        {
            if (s.from == s.to || s.progress != 0 || left <= 0)
                return;
            const float length = step_length(paths, s);
            s.progress = length > 0 ? std::min(left / length, 1.0f) : 1;
        }

        void place(const PathNetwork& paths, const Stride& s, Pose& pose)
        {
            const glm::vec3 a = lane_point(paths, s.from, s.lane_from);
            const glm::vec3 b = lane_point(paths, s.to, s.lane_to);
            pose.position = glm::mix(a, b, s.progress);
            pose.position.y = paths.height_between(s.from, s.to, s.progress);
            if (glm::distance(a, b) > 0.01f)
                pose.heading = std::atan2(b.x - a.x, b.z - a.z);
        }

        /// Where a party waits for those on a ride: a footpath tile beside its exit, off the riders' way; the exit
        /// itself when it has none.
        NodeId waiting_spot(const PathNetwork& paths, NodeId exit)
        {
            for (const NodeId n : paths.nodes()[exit].next)
                if (n != kNoNode && !paths.nodes()[n].queue)
                    return n;
            return exit;
        }

        /// The lane of the `place`th of `of` standing together in a ring on a tile.
        glm::vec2 ring_lane(std::size_t place, std::size_t of)
        {
            const float angle = 2 * glm::pi<float>() * static_cast<float>(place) / static_cast<float>(std::max<std::size_t>(of, 1));
            return kTuning.walking.huddle_radius * glm::vec2(std::sin(angle), std::cos(angle));
        }

        /// The guest's place among their party's members, and how many there are.
        std::pair<std::size_t, std::size_t> place_in_party(const entt::registry& r, entt::entity guest)
        {
            const auto* member = r.try_get<Member>(guest);
            if (member == nullptr)
                return { 0, 1 };
            const auto& members = r.get<Party>(member->party).members;
            return { static_cast<std::size_t>(std::ranges::find(members, guest) - members.begin()), members.size() };
        }

        /// What a guest does once let go by a ride: a party member waits for the rest, others wander on.
        Errand after_ride(const entt::registry& r, entt::entity guest, entt::entity ride)
        {
            if (r.all_of<Member>(guest))
                return { ErrandKind::Waiting, ride, 0 };
            return { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle_seconds) };
        }

        /// Whether a guest is still eating what they bought.
        bool eating(const entt::registry& r, entt::entity guest)
        {
            const auto* rubbish = r.try_get<Rubbish>(guest);
            return rubbish != nullptr && rubbish->kind == 0 && rubbish->held < kConsumeSeconds;
        }

        /// A guest is sick on the path tile `at`, and feels better for it. Returns the mess.
        entt::entity be_sick(entt::registry& r, entt::entity guest, NodeId at)
        {
            const entt::entity mess = r.create();
            r.emplace<Litter>(mess, Litter { .node = at, .vomit = true });
            auto& mood = r.get<Mood>(guest);
            mood.nausea = clamp01(mood.nausea - kTuning.sickness.relief);
            r.emplace_or_replace<Gesture>(guest, GestureKind::Retch, kTuning.gestures.seconds);
            return mess;
        }

        AgeGroup age_of(const entt::registry& r, entt::entity guest)
        {
            const auto* visitor = r.try_get<Visitor>(guest);
            return visitor != nullptr ? visitor->age : AgeGroup::Adult;
        }

        Taste taste_of(const entt::registry& r, entt::entity guest)
        {
            const auto* taste = r.try_get<Taste>(guest);
            return taste != nullptr ? *taste : Taste { };
        }

        float time_of_day(const entt::registry& r)
        {
            const auto* found = r.ctx().find<Daylight>();
            return found != nullptr ? found->time : Daylight { }.time;
        }

        /// A wandering guest (with their party, all walking with them) that may go home now: one done with
        /// the park, by night a family, in a storm anyone. The gate and the way there. A party goes by how done
        /// its members are on average. `cash_machine` is whether the park has one open.
        std::optional<std::pair<entt::entity, std::vector<NodeId>>> going_home(const entt::registry& r, const PathNetwork& paths, entt::entity guest, NodeId at, Dice& dice, bool night, bool storm, bool cash_machine, std::uint64_t tick)
        {
            const auto done = [&](entt::entity g) {
                double chance = 0;
                if (const auto* mood = r.try_get<Mood>(g))
                    chance = mood->happiness < kTuning.leaving.fed_up ? kTuning.leaving.fed_up_per_second : mood->energy < kTuning.leaving.worn_out && mood->happiness < kTuning.leaving.enjoying ? kTuning.leaving.worn_out_per_second : 0;
                // shortcut: guests from a save count their stay from when the park was loaded; read their saved
                // time in the park once its unit is settled.
                const float hours = static_cast<float>(tick - r.get<Visitor>(g).arrived) * kTickSeconds / kSecondsPerHour;
                chance += hours > 2 * kTuning.leaving.long_stay_hours ? 2 * kTuning.leaving.long_stay_per_second : hours > kTuning.leaving.long_stay_hours ? kTuning.leaving.long_stay_per_second : 0;
                if (const auto* wallet = r.try_get<Wallet>(g); wallet != nullptr && wallet->cash < kTuning.facilities.low_cash && !cash_machine)
                    chance += kTuning.leaving.broke_per_second;
                return chance;
            };
            double fed_up = done(guest);
            if (const auto* member = r.try_get<Member>(guest))
            {
                const auto& members = r.get<Party>(member->party).members;
                fed_up = 0;
                for (const entt::entity m : members)
                    fed_up += done(m);
                fed_up /= static_cast<double>(std::max<std::size_t>(members.size(), 1));
            }
            const double chance = fed_up + (night && age_of(r, guest) != AgeGroup::Teen ? kTuning.leaving.night_per_second : 0) + (storm ? kTuning.leaving.storm_per_second : 0);
            if (chance == 0 || !random::chance(dice.state, chance * kTuning.needs.idle_seconds))
                return std::nullopt;
            if (const auto* member = r.try_get<Member>(guest); member != nullptr && std::ranges::any_of(r.get<Party>(member->party).members, [&](entt::entity m) { return m != guest && r.get<Errand>(m).kind != ErrandKind::Following; }))
                return std::nullopt;
            for (const auto [gate, at_gate] : r.view<const ParkGate>().each())
                if (auto walk = paths.route(at, at_gate.node))
                    return std::pair { gate, std::move(*walk) };
            return std::nullopt;
        }

        /// The guest goes home: they stop needing, feeling and walking, and leave their party.
        void leave(entt::registry& r, entt::entity guest)
        {
            ++r.ctx().emplace<Turnstile>().out[static_cast<std::size_t>(age_of(r, guest))];
            r.remove<Needs, Mood, Wallet, Errand, Stride, Dice, Member, Linger, Gesture, Rubbish, RideWalk>(guest);
            r.emplace<Left>(guest);
        }

        Weather weather_in(const entt::registry& r)
        {
            const auto* found = r.ctx().find<Weather>();
            return found != nullptr ? *found : Weather { };
        }

        /// The guest thinks `kind` about `about` at `now` (seconds into the run), unless they thought it lately.
        void think(entt::registry& r, entt::entity guest, ThoughtKind kind, entt::entity about, float now)
        {
            auto& thoughts = r.get_or_emplace<Thoughts>(guest);
            if (std::ranges::any_of(thoughts.recent, [&](const Thought& t) { return t.kind == kind && t.about == about && now - t.time < kTuning.thoughts.rethink_seconds; }))
                return;
            const auto variant = static_cast<std::uint32_t>(random::next(r.get<Dice>(guest).state));
            thoughts.recent.insert(thoughts.recent.begin(), Thought { kind, about, now, variant });
            if (thoughts.recent.size() > kTuning.thoughts.kept)
                thoughts.recent.pop_back();
            ++thoughts.had;
        }

        /// An entity's own random numbers, from the run's seed.
        Dice dice_for(const SimWorld& world, entt::entity e)
        {
            std::uint64_t mix = world.seed.value ^ (std::uint64_t { entt::to_integral(e) } << 32);
            return { random::seeded(random::Seed { random::split_mix(mix) }) };
        }

        /// A share, 0 to 1, fixed for each guest, so a queue looks the same every time it is shown.
        float fixed_share(entt::entity e, std::uint64_t salt)
        {
            std::uint64_t mix = (std::uint64_t { entt::to_integral(e) } << 32) ^ salt;
            return static_cast<float>(random::split_mix(mix) >> 40) / static_cast<float>(1u << 24);
        }

        void stop_at(const PathNetwork& paths, NodeId node, Stride& stride, Pose& pose)
        {
            stride.from = node;
            stride.to = node;
            stride.progress = 1;
            stride.route.clear();
            place(paths, stride, pose);
        }

        /// Where riders come off `ride` on to the paths.
        NodeId exit_of(const entt::registry& r, entt::entity ride)
        {
            const NodeId exit = r.get<CycleRide>(ride).exit;
            return exit != kNoNode ? exit : r.get<Attraction>(ride).entrance;
        }

        /// A rider off `ride` and on the paths at its exit: on to what they do next, showing how the ride left
        /// them, and sick there if it turned their stomach.
        void let_out(entt::registry& r, const PathNetwork& paths, entt::entity guest, entt::entity ride, std::optional<GestureKind> gesture)
        {
            r.get<Errand>(guest) = after_ride(r, guest, ride);
            const NodeId out = exit_of(r, ride);
            stop_at(paths, out, r.get<Stride>(guest), r.get<Pose>(guest));
            auto& dice = r.get<Dice>(guest).state;
            if (gesture && random::chance(dice, kTuning.gestures.chance))
                r.emplace_or_replace<Gesture>(guest, *gesture, kTuning.gestures.seconds);
            if (r.get<Mood>(guest).nausea >= kTuning.sickness.sick && random::chance(dice, kTuning.sickness.sick_after_ride))
                be_sick(r, guest, out);
        }
    } // namespace

    Pose between_steps(const Pose& pose, float along)
    {
        const float turn = std::remainder(pose.heading - pose.was_heading, 2 * glm::pi<float>());
        const float walked = pose.walked + along * glm::distance(glm::vec2(pose.was.x, pose.was.z), glm::vec2(pose.position.x, pose.position.z));
        return { glm::mix(pose.was, pose.position, along), pose.was_heading + turn * along, pose.was, pose.was_heading, walked };
    }

    double arrivals_per_second(const Weather& weather)
    {
        double rate = kTuning.arrivals.per_second * (weather.storm ? kTuning.arrivals.storm : weather.rain ? kTuning.arrivals.rain : 1);
        if (weather.temperature < kTuning.weather.cold)
            rate *= kTuning.arrivals.cold;
        else if (weather.sunny && !weather.rain)
            rate *= kTuning.arrivals.sunny;
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
        return static_cast<std::uint32_t>(std::lround(room * kTuning.arrivals.room_to_spare));
    }

    std::int64_t fair_entrance_fee(const SimWorld& world)
    {
        const entt::registry& r = world.registry;
        std::int64_t over = 0;
        for (const auto [e, ride, at] : r.view<const CycleRide, const Attraction>().each())
        {
            if (!at.open)
                continue;
            float worth = kTuning.arrivals.ride_worth * (ride.excitement + kTuning.arrivals.intensity_worth * ride.intensity);
            if (ride.intensity > kTuning.arrivals.too_intense)
                worth /= 2;
            over += std::max<std::int64_t>(0, std::lround(worth) - at.price);
        }
        return std::llround(static_cast<double>(over) * kTuning.arrivals.fair_fee_share);
    }

    double drawn(const Draw& draw, std::uint32_t guests, std::uint32_t ideal, std::int64_t fair_fee)
    {
        const auto pull = [](float rating) { return kTuning.arrivals.curious + std::clamp(rating, kTuning.arrivals.unknown_rating, kTuning.arrivals.famous_rating) - kTuning.arrivals.unknown_rating; };
        double rate = pull(draw.rating.value_or(kTuning.arrivals.plain_rating)) / pull(kTuning.arrivals.plain_rating);
        if (guests >= ideal)
            rate *= kTuning.arrivals.crowded;
        if (draw.entrance_fee > fair_fee)
            rate *= kTuning.arrivals.overpriced;
        if (draw.entrance_fee > 2 * fair_fee)
            rate *= kTuning.arrivals.overpriced;
        return rate;
    }

    std::array<double, kPartyKinds> party_weights(const SimWorld& world, bool night)
    {
        const entt::registry& r = world.registry;
        const auto* mix = r.ctx().find<PartyMix>();
        std::array<double, kPartyKinds> weights { };
        for (std::size_t k = 0; k < kPartyKinds; ++k)
            weights[k] = mix != nullptr ? mix->weights[k] : kTuning.parties.weights[k];
        // How wild the open rides are, from -1 (all the gentlest) to 1 (all the wildest); none tilt nothing.
        float wild = 0;
        std::size_t rides = 0;
        for (const auto [e, ride, at] : r.view<const CycleRide, const Attraction>().each())
        {
            if (!at.open)
                continue;
            wild += ride.intensity * 2 - 1;
            ++rides;
        }
        const double tilt = std::pow(1 + kTuning.parties.pull, rides > 0 ? wild / static_cast<float>(rides) : 0.0f);
        // shortcut: no campaign aims at a kind of party, as advertising campaigns are not simulated yet (the
        // finances work, #98); when they are, one aimed at families or at teens multiplies those weights here.
        for (std::size_t k = 0; k < kPartyKinds; ++k)
        {
            const auto kind = static_cast<PartyKind>(k);
            const bool teens = kind == PartyKind::LoneTeen || kind == PartyKind::TeenCouple || kind == PartyKind::TeenGang;
            if (teens)
                weights[k] *= tilt;
            else if (kind == PartyKind::Family || kind == PartyKind::DayTrip)
                weights[k] /= tilt;
            if (night && !teens)
                weights[k] *= kTuning.parties.night_others;
        }
        return weights;
    }

    bool is_night(float time)
    {
        return time >= kTuning.night.dusk || time < kTuning.night.dawn;
    }

    float felt_excitement(float excitement, bool coaster, AgeGroup age, bool night)
    {
        if (!coaster || !night)
            return excitement;
        return excitement * (age == AgeGroup::Teen ? 1 + kTuning.night.thrill : 1 - kTuning.night.dread);
    }

    std::uint32_t ticks(float seconds)
    {
        return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0f) / kTickSeconds));
    }

    std::vector<entt::entity> admit_party(SimWorld& world, random::RandomState& dice, NodeId at)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
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
                if (!random::chance(dice, mix != nullptr ? mix->single_parent : kTuning.parties.single_parent))
                    who.emplace_back(AgeGroup::Adult, !female);
                children();
                break;
            case PartyKind::DayTrip:
                who.emplace_back(AgeGroup::Adult, female);
                children();
                break;
        }
        const tuning::TasteBand band = kind == PartyKind::LoneTeen || kind == PartyKind::TeenGang ? kTuning.parties.thrill : kind == PartyKind::Family || kind == PartyKind::DayTrip ? kTuning.parties.gentle : kTuning.parties.middle;
        std::vector<entt::entity> members;
        for (const auto [age, is_female] : who)
        {
            const entt::entity guest = members.emplace_back(r.create());
            r.emplace<Visitor>(guest, age, is_female, std::string { }, world.tick);
            r.emplace<Pose>(guest, paths.centre(at), 0.0f);
            r.emplace<Needs>(guest);
            r.emplace<Mood>(guest, Mood { .happiness = kTuning.arrivals.mood, .energy = 1, .tolerance = age == AgeGroup::Teen ? kTuning.arrivals.teen_tolerance : Mood { }.tolerance });
            const auto spread = [&] { return static_cast<float>(random::between(dice, -1.0, 1.0)) * kTuning.parties.taste_spread; };
            const float least = std::clamp(band.least + spread(), 0.0f, 1.0f);
            r.emplace<Taste>(guest, least, std::clamp(band.most + spread(), least, 1.0f));
            r.emplace<Wallet>(guest, kTuning.arrivals.cash);
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
        const float speed = visitor != nullptr ? kTuning.walking.speed[std::to_underlying(visitor->age)] : Stride { }.speed;
        r.emplace_or_replace<Stride>(guest, Stride { .from = at, .to = at, .progress = 1, .speed = speed, .lane_from = lane, .lane_to = lane });
        r.emplace_or_replace<Dice>(guest, dice_for(world, guest));
        auto& pose = r.get_or_emplace<Pose>(guest);
        place(world.paths, r.get<Stride>(guest), pose);
        pose.was = pose.position;
        pose.was_heading = pose.heading;
    }

    namespace
    {
        bool walking(ErrandKind kind)
        {
            return kind == ErrandKind::Wandering || kind == ErrandKind::Heading || kind == ErrandKind::Following || kind == ErrandKind::Waiting;
        }

        /// Litter, path items and staff, for one tick: after the guests have walked and bought.
        void tidy(SimWorld& world)
        {
            entt::registry& r = world.registry;
            const PathNetwork& paths = world.paths;
            constexpr float dt = kTickSeconds;
            const float now = static_cast<float>(world.tick) * kTickSeconds;
            const Weather weather = weather_in(r);
            auto& vandalism = r.ctx().emplace<Vandalism>();
            // Guests come in at the gate: families by day, mostly teens after dark.
            double arrivals = arrivals_per_second(weather);
            if (const auto* draw = r.ctx().find<Draw>(); draw != nullptr && !r.view<const ParkGate>().empty())
            {
                const auto guests = static_cast<std::uint32_t>(std::ranges::distance(r.view<const Visitor>(entt::exclude<Left>)));
                const std::uint32_t ideal = ideal_guests(world);
                // A bigger park draws more till it is full: enough to fill its room in a day's opening, never fewer
                // than a small one's. Once full only the few of a small park still come.
                const double fill = guests < ideal ? std::max(1.0, ideal / (kTuning.arrivals.fill_seconds * kTuning.arrivals.per_second)) : 1.0;
                arrivals *= fill * drawn(*draw, guests, ideal, fair_entrance_fee(world));
            }
            for (const entt::entity gate : sorted(r.view<const ParkGate>()))
            {
                if (!r.all_of<Dice>(gate))
                    r.emplace<Dice>(gate, dice_for(world, gate));
                auto& dice = r.get<Dice>(gate).state;
                if (!random::chance(dice, arrivals * dt))
                    continue;
                admit_party(world, dice, r.get<ParkGate>(gate).node);
            }
            vandalism.recent *= std::exp(-dt / kTuning.vandals.fade);
            if (vandalism.since_warning >= 0)
                vandalism.since_warning += dt;

            // What lies and stands on each tile, in entity order.
            std::unordered_map<NodeId, std::vector<entt::entity>> litter_at;
            for (const entt::entity e : sorted(r.view<Litter>()))
            {
                auto& piece = r.get<Litter>(e);
                piece.age += dt;
                litter_at[piece.node].push_back(e);
            }
            std::unordered_map<NodeId, std::vector<entt::entity>> items_at;
            for (const entt::entity e : sorted(r.view<const PathItem>()))
                items_at[r.get<PathItem>(e).node].push_back(e);
            const auto items = [&](NodeId node) -> std::span<const entt::entity> {
                const auto found = items_at.find(node);
                return found != items_at.end() ? std::span<const entt::entity>(found->second) : std::span<const entt::entity> { };
            };
            const auto litter_on = [&](NodeId node) {
                const auto found = litter_at.find(node);
                return found != litter_at.end() ? found->second.size() : 0;
            };
            const auto drop = [&](NodeId node, std::uint8_t kind) {
                const entt::entity e = r.create();
                r.emplace<Litter>(e, Litter { node, kind, 0 });
                litter_at[node].push_back(e);
            };
            // The tiles guards keep vandals off: theirs and those a few steps along the paths from it.
            std::unordered_set<NodeId> guarded;
            for (const auto [e, staff, stride] : r.view<const Staff, const Stride>().each())
            {
                if (staff.role != StaffRole::Guard || stride.to >= paths.nodes().size())
                    continue;
                std::vector<NodeId> ring { stride.to };
                guarded.insert(stride.to);
                for (int step = 0; step < kTuning.vandals.guard_reach; ++step)
                {
                    std::vector<NodeId> next;
                    for (const NodeId n : ring)
                        for (const NodeId m : paths.nodes()[n].next)
                            if (m != kNoNode && guarded.insert(m).second)
                                next.push_back(m);
                    ring = std::move(next);
                }
            }

            // Guests: glum on dirty or vandalised paths, binning or dropping their rubbish, and the angriest breaking things.
            const auto guests_view = r.view<const Errand, const Stride, Mood, Dice, const Needs>();
            std::unordered_map<NodeId, std::size_t> seated;
            for (const auto [g, linger, stride] : r.view<const Linger, const Stride>().each())
                seated[stride.from] += linger.at == PathItemKind::Bench;
            for (const entt::entity g : sorted(guests_view))
            {
                auto [errand, stride, mood, dice, needs] = guests_view.get(g);
                if (!walking(errand.kind) || stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                    continue;
                // Sitting rests a guest, watching cheers them; then they walk on.
                if (auto* linger = r.try_get<Linger>(g))
                {
                    if (linger->at == PathItemKind::Bench)
                    {
                        mood.energy = clamp01(mood.energy + kTuning.facilities.rest_per_second * dt);
                        mood.nausea = clamp01(mood.nausea - kTuning.sickness.rest_settle_per_second * dt);
                    }
                    else
                        mood.happiness = clamp01(mood.happiness + kTuning.facilities.sight_cheer_per_second * dt);
                    linger->left -= dt;
                    if (linger->left <= 0)
                        r.remove<Linger>(g);
                    continue;
                }
                // What a guest just come to a tile thinks of themselves and of the path.
                if (stride.fresh)
                {
                    const auto about = [&](ThoughtKind kind, bool when) {
                        if (when)
                            think(r, g, kind, entt::null, now);
                    };
                    about(ThoughtKind::Hungry, needs.hunger >= kTuning.needs.want);
                    about(ThoughtKind::Thirsty, needs.thirst >= kTuning.needs.want);
                    about(ThoughtKind::Toilet, needs.bladder >= kTuning.needs.want);
                    about(ThoughtKind::Tired, mood.energy < kTuning.facilities.tired);
                    about(mood.nausea >= kTuning.sickness.sick ? ThoughtKind::VerySick : ThoughtKind::Sick, mood.nausea >= kTuning.needs.want);
                    about(ThoughtKind::Broke, r.get<Wallet>(g).cash < kTuning.facilities.low_cash);
                    about(ThoughtKind::Litter, litter_on(stride.from) >= kTuning.litter.dirty_path);
                    about(ThoughtKind::Raining, weather.rain && !r.all_of<Umbrella>(g));
                    about(ThoughtKind::Soaked, mood.wet >= kTuning.needs.want);
                    about(ThoughtKind::Hot, weather.temperature >= kTuning.weather.hot || mood.heat >= kTuning.needs.want);
                    about(ThoughtKind::Thunder, weather.storm);
                    about(ThoughtKind::GoHome, errand.kind == ErrandKind::Heading && r.valid(errand.target) && r.all_of<ParkGate>(errand.target));
                    about(ThoughtKind::Cold, weather.temperature < kTuning.weather.cold);
                    about(ThoughtKind::Vandalism, std::ranges::any_of(items(stride.from), [&](entt::entity i) { return r.get<PathItem>(i).broken; }));
                    const auto here = litter_at.find(stride.from);
                    about(ThoughtKind::Vomit, here != litter_at.end() && std::ranges::any_of(here->second, [&](entt::entity l) { return r.get<Litter>(l).vomit; }));
                }
                // A wandering guest just come to a tile may sit on a bench there when tired, or stop at a sight.
                if (errand.kind == ErrandKind::Wandering && stride.fresh)
                {
                    const NodeId here = stride.from;
                    const auto benches = static_cast<std::size_t>(std::ranges::count_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::Bench && !r.get<PathItem>(i).broken; }));
                    const auto sight = std::ranges::find_if(items(here), [&](entt::entity i) { return r.get<PathItem>(i).kind == PathItemKind::Sight || r.get<PathItem>(i).kind == PathItemKind::PhotoSpot; });
                    if ((mood.energy < kTuning.facilities.tired || mood.nausea >= kTuning.needs.want) && seated[here] < benches * kTuning.facilities.bench_seats && random::chance(dice.state, kTuning.facilities.sit_chance))
                    {
                        r.emplace<Linger>(g, PathItemKind::Bench, kTuning.facilities.sit_seconds);
                        ++seated[here];
                        continue;
                    }
                    if (sight != items(here).end() && random::chance(dice.state, kTuning.facilities.watch_chance))
                    {
                        const PathItemKind kind = r.get<PathItem>(*sight).kind;
                        r.emplace<Linger>(g, kind, kTuning.facilities.watch_seconds);
                        // The player keeps a photo of each stop their own guest's party makes at a photo spot.
                        if (kind == PathItemKind::PhotoSpot)
                        {
                            entt::entity avatar = r.get<Visitor>(g).avatar ? g : entt::null;
                            if (const auto* member = r.try_get<Member>(g))
                                for (const entt::entity m : r.get<Party>(member->party).members)
                                    if (avatar == entt::null && r.get<Visitor>(m).avatar)
                                        avatar = m;
                            if (avatar != entt::null)
                                r.ctx().emplace<Photos>().taken.push_back({ avatar, here });
                        }
                        continue;
                    }
                }
                const NodeId at = stride.progress < 0.5f ? stride.from : stride.to;
                if (litter_on(at) >= kTuning.litter.dirty_path)
                    mood.happiness = clamp01(mood.happiness - kTuning.litter.dirt_gloom_per_second * dt);
                if (const auto here = litter_at.find(at); here != litter_at.end() && std::ranges::any_of(here->second, [&](entt::entity l) { return r.get<Litter>(l).vomit; }))
                {
                    mood.happiness = clamp01(mood.happiness - kTuning.sickness.vomit_gloom_per_second * dt);
                    mood.nausea = clamp01(mood.nausea + kTuning.sickness.queasy_per_second * dt);
                }
                if (mood.nausea >= kTuning.sickness.sick && random::chance(dice.state, kTuning.sickness.sick_per_second * dt))
                {
                    litter_at[at].push_back(be_sick(r, g, at));
                }
                if (std::ranges::any_of(items(at), [&](entt::entity i) { return r.get<PathItem>(i).broken; }))
                    mood.happiness = clamp01(mood.happiness - kTuning.vandals.gloom_per_second * dt);
                if (auto* rubbish = r.try_get<Rubbish>(g))
                {
                    const bool finished = rubbish->held < kConsumeSeconds && rubbish->held + dt >= kConsumeSeconds;
                    rubbish->held += dt;
                    if (rubbish->held >= kConsumeSeconds)
                    {
                        const auto bin = std::ranges::find_if(items(at), [&](entt::entity i) { return r.all_of<Bin>(i) && !r.get<PathItem>(i).broken; });
                        bool gone = true;
                        if (finished && random::chance(dice.state, kTuning.litter.litterbug_chance + kTuning.litter.grumpy_chance * (1 - mood.happiness)))
                            drop(at, rubbish->kind);
                        else if (bin != items(at).end())
                        {
                            // A full bin overflows onto the path.
                            auto& b = r.get<Bin>(*bin);
                            if (b.contents + 1 <= b.capacity)
                                b.contents += 1;
                            else
                                drop(at, rubbish->kind);
                        }
                        else if (rubbish->held >= kConsumeSeconds + kTuning.litter.carry_seconds)
                            drop(at, rubbish->kind);
                        else
                            gone = false;
                        if (gone)
                            r.remove<Rubbish>(g);
                    }
                }
                if (mood.happiness < kTuning.vandals.mood && !guarded.contains(at) && random::chance(dice.state, kTuning.vandals.per_second * dt))
                {
                    const auto whole = std::ranges::find_if(items(at), [&](entt::entity i) { return !r.get<PathItem>(i).broken && r.get<PathItem>(i).kind != PathItemKind::Sight && r.get<PathItem>(i).kind != PathItemKind::PhotoSpot; });
                    if (whole != items(at).end())
                    {
                        r.get<PathItem>(*whole).broken = true;
                        ++vandalism.broken;
                        vandalism.recent += 1;
                        if (vandalism.recent >= kTuning.vandals.bad && (vandalism.since_warning < 0 || vandalism.since_warning >= kTuning.vandals.warning_every))
                        {
                            ++vandalism.warnings;
                            vandalism.since_warning = 0;
                        }
                    }
                }
            }

            // Staff walk the paths. Handymen sweep, empty bins and mend; mechanics mend; each job keeps them a while.
            if (paths.nodes().empty())
                return;
            const auto staff_view = r.view<Staff, Stride, Pose, Dice>();
            auto& payroll = r.ctx().emplace<Payroll>();
            for (const entt::entity e : sorted(staff_view))
                payroll.owed += static_cast<double>(staff_view.get<Staff>(e).wage) * dt / kSecondsPerHour;
            payroll.paid += static_cast<std::int64_t>(payroll.owed);
            payroll.owed -= std::floor(payroll.owed);

            // Free mechanics: the nearest by the paths goes to a broken ride, else to one due an inspection.
            const auto spot = [&](entt::entity ride) {
                const NodeId exit = r.get<CycleRide>(ride).exit;
                return exit != kNoNode ? exit : r.get<Attraction>(ride).entrance;
            };
            std::vector<entt::entity> free;
            for (const entt::entity e : sorted(staff_view))
            {
                const auto [staff, stride] = staff_view.get<Staff, Stride>(e);
                if (staff.role == StaffRole::Mechanic && staff.ride == entt::null && stride.to < paths.nodes().size())
                    free.push_back(e);
            }
            const auto send = [&](entt::entity ride, Upkeep& upkeep) {
                // Those whose patrol takes the ride in first, then the nearest.
                const auto ours = [&](entt::entity m) {
                    const auto* patrol = r.try_get<Patrol>(m);
                    return patrol != nullptr && !patrol->tiles.empty() && spot(ride) != kNoNode && patrol->covers(paths.nodes()[spot(ride)].tile);
                };
                std::optional<std::vector<NodeId>> best;
                auto who = free.end();
                for (auto m = free.begin(); m != free.end(); ++m)
                    if (auto way = paths.route(r.get<Stride>(*m).to, spot(ride)); way && (!best || std::pair(!ours(*m), way->size()) < std::pair(!ours(*who), best->size())))
                    {
                        best = std::move(way);
                        who = m;
                    }
                if (who == free.end())
                    return;
                r.get<Staff>(*who).ride = ride;
                r.get<Stride>(*who).route = std::move(*best);
                upkeep.mechanic = *who;
                free.erase(who);
            };
            const auto rides = sorted(r.view<const Upkeep, const CycleRide>());
            for (const bool broken : { true, false })
                for (const entt::entity ride : rides)
                {
                    auto& upkeep = r.get<Upkeep>(ride);
                    const bool due = broken ? upkeep.broken : upkeep.inspect_every >= 0 && upkeep.since_inspection >= upkeep.inspect_every;
                    if (!free.empty() && due && upkeep.mechanic == entt::null)
                        send(ride, upkeep);
                }

            for (const entt::entity e : sorted(staff_view))
            {
                auto [staff, stride, pose, dice] = staff_view.get(e);
                if (stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                    continue;
                if (staff.ride != entt::null && !r.valid(staff.ride))
                    staff.ride = entt::null;
                care_for(staff, (staff.busy > 0 && !staff.resting) || staff.ride != entt::null, dt);
                // A lazy one, with nothing to do, stops where they are now and then.
                if (staff.busy == 0 && staff.ride == entt::null && random::chance(dice.state, rest_chance(staff) * dt))
                {
                    staff.busy = kRestSeconds;
                    staff.resting = true;
                }
                if (staff.busy > 0)
                {
                    staff.busy = std::max(staff.busy - dt, 0.0f);
                    if (staff.busy == 0)
                        staff.resting = false;
                    if (staff.busy == 0 && staff.ride != entt::null)
                    {
                        // Done at the ride: repaired and open again, or inspected.
                        auto& upkeep = r.get<Upkeep>(staff.ride);
                        if (upkeep.broken)
                            r.get<Attraction>(staff.ride).open = true;
                        upkeep.reliability = std::min(upkeep.reliability + (upkeep.broken ? kTuning.wear.repair_gain : kTuning.wear.inspect_gain), 1.0f);
                        upkeep.broken = false;
                        upkeep.kind = Breakdown::None;
                        upkeep.since_inspection = 0;
                        upkeep.mechanic = entt::null;
                        staff.ride = entt::null;
                    }
                    continue;
                }
                float left = 0;
                if (stride.from != stride.to)
                {
                    const float length = step_length(paths, stride);
                    stride.progress += length > 0 ? stride.speed * dt / length : 1;
                    if (stride.progress < 1)
                    {
                        place(paths, stride, pose);
                        continue;
                    }
                    left = (stride.progress - 1) * length;
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
                    if (auto way = home != kNoNode ? paths.route(from, home) : std::nullopt; way && !way->empty())
                        stride.route = std::move(*way);
                    return !stride.route.empty();
                };
                const bool inside = patrol == nullptr || patrol->covers(paths.nodes()[at].tile);
                const bool tidies = staff.role == StaffRole::Handyman && inside;
                const bool mends = tidies || (staff.role == StaffRole::Mechanic && inside);
                const auto job = [&] {
                    if (tidies && litter_on(at) > 0)
                    {
                        auto& here = litter_at[at];
                        r.destroy(here.back());
                        here.pop_back();
                        return kTuning.handymen.sweep_seconds;
                    }
                    for (const entt::entity i : items(at))
                    {
                        auto& item = r.get<PathItem>(i);
                        if (mends && item.broken)
                        {
                            item.broken = false;
                            return kTuning.handymen.mend_seconds;
                        }
                        if (auto* bin = r.try_get<Bin>(i); tidies && bin != nullptr && bin->contents > 0 && bin->contents >= kTuning.handymen.empty_at * bin->capacity)
                        {
                            bin->contents = 0;
                            return kTuning.handymen.empty_seconds;
                        }
                    }
                    return 0.0f;
                };
                stride.from = at;
                stride.lane_from = stride.lane_to;
                if (staff.ride != entt::null)
                {
                    // On the way to a ride, or there and setting to work.
                    if (!stride.route.empty())
                    {
                        stride.to = stride.route.front();
                        stride.route.erase(stride.route.begin());
                        stride.progress = 0;
                        stride.lane_to = new_lane(dice, stride.lane_from);
                    }
                    else
                    {
                        staff.busy = (r.get<Upkeep>(staff.ride).broken ? kTuning.wear.repair_seconds : kTuning.wear.inspect_seconds) * work_slowdown(staff);
                        stride.to = at;
                        stride.progress = 1;
                    }
                }
                else if (const float busy = job(); busy > 0)
                {
                    staff.busy = busy * work_slowdown(staff);
                    stride.to = at;
                    stride.progress = 1;
                }
                else if (!inside && back_to(*patrol, at))
                {
                    stride.to = stride.route.front();
                    stride.route.erase(stride.route.begin());
                    stride.progress = 0;
                    stride.lane_to = new_lane(dice, stride.lane_from);
                }
                else
                {
                    stride.route.clear();
                    // A handyman makes for the dirtiest tile beside them; the rest of the time staff wander.
                    // No way back to a patrol off the paths: wander the park as if they had none.
                    const Patrol* keep_to = inside ? patrol : nullptr;
                    NodeId next = wander(paths, at, came != at ? came : kNoNode, dice, keep_to);
                    if (tidies)
                        for (const NodeId n : paths.nodes()[at].next)
                            if (n != kNoNode && !paths.nodes()[n].queue && (keep_to == nullptr || keep_to->covers(paths.nodes()[n].tile)) && litter_on(n) > litter_on(next))
                                next = n;
                    stride.to = next;
                    stride.progress = 0;
                    stride.lane_to = next != at ? new_lane(dice, stride.lane_from) : stride.lane_from;
                }
                walk_on(paths, stride, left);
                place(paths, stride, pose);
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
        return static_cast<std::int32_t>(std::ranges::count_if(view, [&](entt::entity e) { return view.get<const Litter>(e).age >= kTuning.litter.old_seconds; }));
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
        return ride.start_seconds + static_cast<float>(ride.loops) * ride.run_seconds + ride.stop_seconds;
    }

    std::optional<float> seconds_into_cycle(const CycleRide& ride)
    {
        switch (ride.phase)
        {
            case RidePhase::Loading:
                return std::nullopt;
            case RidePhase::Starting:
                return ride.phase_seconds;
            case RidePhase::Running:
                return ride.start_seconds + static_cast<float>(ride.loop) * ride.run_seconds + ride.phase_seconds;
            case RidePhase::Stopping:
                return ride.start_seconds + static_cast<float>(ride.loops) * ride.run_seconds + std::min(ride.phase_seconds, ride.stop_seconds);
        }
        return std::nullopt;
    }

    bool advance_cycle(CycleRide& ride, float dt, bool held)
    {
        if (ride.phase == RidePhase::Loading)
            return false;
        ride.phase_seconds += dt;
        // Time left over from one phase goes on into the next, so a cycle lasts as long as its parts add up to.
        if (ride.phase == RidePhase::Starting && ride.phase_seconds >= ride.start_seconds)
        {
            ride.phase_seconds -= ride.start_seconds;
            ride.phase = RidePhase::Running;
            ride.loop = 0;
        }
        if (ride.phase == RidePhase::Running)
        {
            const auto passed = ride.run_seconds > 0 ? static_cast<std::uint32_t>(ride.phase_seconds / ride.run_seconds) : ride.loops;
            const std::uint32_t done = std::min(passed, ride.loops - std::min(ride.loop, ride.loops));
            ride.loop += done;
            ride.phase_seconds -= static_cast<float>(done) * ride.run_seconds;
            if (ride.loop >= ride.loops)
                ride.phase = RidePhase::Stopping;
        }
        if (ride.phase == RidePhase::Stopping && ride.phase_seconds >= ride.stop_seconds && !held)
        {
            ride.phase = RidePhase::Loading;
            ride.phase_seconds = 0;
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
            if (k == t || length <= 0)
                continue;
            // How far ahead the back of train k is, round the track.
            const float ahead = std::fmod(std::fmod(trains[k].at - train.at, length) + length, length);
            room = std::min(room, std::max(ahead - trains[k].train_length - kBlockGap, 0.0f));
        }
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
        const auto shops = r.view<const Shop>();
        const bool cash_machine = std::ranges::any_of(shops, [&](entt::entity e) { return shops.get<const Shop>(e).product == Product::CashMachine; });
        // Under cover (roof), or in the shade (roof or trees): on a ride under cover, or on a path tile.
        const auto under = [&](entt::entity guest, bool shade) {
            const auto* errand = r.try_get<Errand>(guest);
            if (errand != nullptr && errand->kind == ErrandKind::Riding)
            {
                const auto* ride = r.valid(errand->target) ? r.try_get<CycleRide>(errand->target) : nullptr;
                return ride != nullptr && ride->cover >= kTuning.weather.covered;
            }
            const auto* stride = r.try_get<Stride>(guest);
            if (stride == nullptr || stride->from >= paths.nodes().size())
                return false;
            const PathNode& node = paths.nodes()[stride->from];
            return shade ? node.shaded : node.covered;
        };
        // Out of the rain: under an umbrella, or under cover.
        const auto sheltered = [&](entt::entity guest) { return r.all_of<Umbrella>(guest) || under(guest, false); };
        const bool sun = weather.sunny && weather.temperature >= kTuning.weather.hot;

        phase.emplace("Needs");
        // 1. Needs and moods.
        {
            const float thirst = 1 + kTuning.weather.heat_per_degree * std::max(weather.temperature - kTuning.weather.hot, 0.0f);
            const float hunger = 1 + kTuning.weather.heat_per_degree * std::max(kTuning.weather.cold - weather.temperature, 0.0f);
            const auto view = r.view<Needs, Mood>();
            const auto guests = sorted(view);
            share(workers, guests.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [needs, mood] = view.get<Needs, Mood>(guests[i]);
                    mood.nausea = clamp01(mood.nausea - kTuning.sickness.settle_per_second * dt / (eating(r, guests[i]) ? kTuning.sickness.full_stomach : 1));
                    needs.thirst = clamp01(needs.thirst + kTuning.needs.thirst_per_second * thirst * dt);
                    needs.hunger = clamp01(needs.hunger + kTuning.needs.hunger_per_second * hunger * dt);
                    if (weather.rain && !sheltered(guests[i]))
                        mood.wet = clamp01(mood.wet + kTuning.weather.wet_per_second * (weather.storm ? 2 : 1) * dt);
                    else
                        mood.wet = clamp01(mood.wet - kTuning.weather.dry_per_second * dt);
                    // Wet is only a misery when it is not hot.
                    if (mood.wet >= kTuning.needs.want && weather.temperature < kTuning.weather.hot)
                        mood.happiness = clamp01(mood.happiness - kTuning.weather.wet_gloom_per_second * (weather.temperature < kTuning.weather.cold ? 2 : 1) * dt);
                    // The sun heats those out in it; shade, a cooler day and getting wet cool them.
                    const float sweat = sun && !under(guests[i], true) ? kTuning.weather.sweat_per_second * (1 + kTuning.weather.heat_per_degree * (weather.temperature - kTuning.weather.hot)) : -kTuning.weather.cool_per_second;
                    mood.heat = clamp01(mood.heat + (sweat - (mood.wet > 0 ? kTuning.weather.cool_per_second : 0)) * dt);
                    if (mood.heat >= kTuning.needs.want)
                        mood.happiness = clamp01(mood.happiness - kTuning.weather.heat_gloom_per_second * dt);
                    needs.bladder = clamp01(needs.bladder + kTuning.needs.bladder_per_second * dt);
                    mood.energy = clamp01(mood.energy - kTuning.needs.energy_per_second * dt);
                    if (std::max({ needs.thirst, needs.hunger, needs.bladder }) >= kTuning.needs.pressing)
                        mood.happiness = clamp01(mood.happiness - kTuning.needs.gloom_per_second * dt);
                }
            });
        }

        for (auto [g, gesture] : r.view<Gesture>().each())
            if ((gesture.left -= dt) <= 0)
                r.remove<Gesture>(g);
        for (auto [_, pose] : r.view<Pose>().each())
        {
            pose.walked += glm::distance(glm::vec2(pose.was.x, pose.was.z), glm::vec2(pose.position.x, pose.position.z));
            pose.was = pose.position;
            pose.was_heading = pose.heading;
        }

        phase.emplace("Walking");
        // 2. Walking and deciding. Arrivals are noted per guest and dealt with after.
        const std::vector<Offer> on_offer = offers(r);
        // Footpath tiles under cover and in the shade, where guests go out of the rain or the sun.
        std::vector<NodeId> covers;
        std::vector<NodeId> shades;
        for (NodeId n = 0; n < paths.nodes().size(); ++n)
        {
            const PathNode& node = paths.nodes()[n];
            if (node.queue || node.removed)
                continue;
            if (node.covered)
                covers.push_back(n);
            if (node.shaded)
                shades.push_back(n);
        }
        const auto movers_view = r.view<Errand, Stride, Pose, Dice, const Needs, const Mood, const Wallet>();
        const auto movers = sorted(movers_view);
        std::vector<entt::entity> arrived(movers.size(), entt::null);
        if (!paths.nodes().empty())
        {
            share(workers, movers.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [errand, stride, pose, dice, needs, mood, wallet] = movers_view.get(movers[i]);
                    stride.fresh = false;
                    if (errand.kind != ErrandKind::Wandering && errand.kind != ErrandKind::Heading && errand.kind != ErrandKind::Waiting)
                        continue;
                    if (stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                        continue; // Off the paths; nothing to walk on
                    if (r.any_of<Linger, Gesture>(movers[i]))
                        continue; // Sitting, watching or showing how they feel
                    if (errand.rest > 0)
                        --errand.rest;
                    // A leader is the only one to write their party, so this is safe beside the other guests.
                    const auto* member = r.try_get<Member>(movers[i]);
                    Party* party = member != nullptr && r.get<Party>(member->party).leader == movers[i] ? &r.get<Party>(member->party) : nullptr;
                    if (party != nullptr && party->huddle > 0)
                    {
                        --party->huddle;
                        continue; // Standing with the others
                    }

                    float left = 0;
                    if (stride.from != stride.to)
                    {
                        const float length = step_length(paths, stride);
                        stride.progress += length > 0 ? stride.speed * dt / length : 1;
                        left = (stride.progress - 1) * length;
                    }
                    if (stride.progress < 1)
                    {
                        place(paths, stride, pose);
                        continue;
                    }

                    // At the tile `to`.
                    const NodeId came = stride.from;
                    const NodeId at = stride.to;
                    stride.from = at;
                    stride.progress = 0;
                    stride.fresh = true;
                    stride.lane_from = stride.lane_to;
                    bool ringed = false;
                    if (!stride.route.empty())
                    {
                        stride.to = stride.route.front();
                        stride.route.erase(stride.route.begin());
                        // Waiting for the others: in a ring with them on the last tile.
                        if (stride.route.empty() && errand.kind == ErrandKind::Waiting)
                        {
                            const auto [place, of] = place_in_party(r, movers[i]);
                            stride.lane_to = ring_lane(place, of);
                            ringed = true;
                        }
                    }
                    else if (errand.kind != ErrandKind::Wandering)
                    {
                        if (errand.kind == ErrandKind::Heading)
                            arrived[i] = errand.target;
                        stride.to = at;
                        stride.progress = 1;
                    }
                    else
                    {
                        if (errand.rest == 0)
                        {
                            errand.rest = ticks(kTuning.needs.idle_seconds);
                            // A party goes where its neediest member needs to; whoever is sickest decides on rides.
                            Needs want = needs;
                            Mood feel = mood;
                            if (const auto* member = r.try_get<Member>(movers[i]))
                            {
                                for (const entt::entity m : r.get<Party>(member->party).members)
                                {
                                    const Needs& n = r.get<Needs>(m);
                                    want = { std::max(want.hunger, n.hunger), std::max(want.thirst, n.thirst), std::max(want.bladder, n.bladder) };
                                    feel.nausea = std::max(feel.nausea, r.get<Mood>(m).nausea);
                                    feel.tolerance = std::min(feel.tolerance, r.get<Mood>(m).tolerance);
                                    feel.wet = std::max(feel.wet, r.get<Mood>(m).wet);
                                    feel.heat = std::max(feel.heat, r.get<Mood>(m).heat);
                                }
                            }
                            auto goal = going_home(r, paths, movers[i], at, dice, night, weather.storm, cash_machine, world.tick);
                            if (!goal)
                                goal = choose(paths, at, on_offer, want, feel, wallet, dice, weather.rain && !r.all_of<Umbrella>(movers[i]), night && age_of(r, movers[i]) == AgeGroup::Teen ? kTuning.night.daring : 0, taste_of(r, movers[i]), weather.temperature);
                            // Nowhere to go: out of the rain under the nearest roof (in a storm even with an
                            // umbrella), or out of the sun into the nearest shade.
                            const bool rained_on = weather.rain && (!r.all_of<Umbrella>(movers[i]) || weather.storm);
                            const bool sweltering = sun && feel.heat >= kTuning.needs.want;
                            if (!goal && (rained_on ? !paths.nodes()[at].covered : sweltering && !paths.nodes()[at].shaded))
                                if (auto refuge = paths.route_nearest(at, rained_on ? covers : shades); refuge && !refuge->second.empty())
                                    stride.route = std::move(refuge->second);
                            if (goal)
                            {
                                errand = { ErrandKind::Heading, goal->first, 0 };
                                stride.route = std::move(goal->second);
                                if (stride.route.empty())
                                {
                                    arrived[i] = errand.target;
                                    stride.to = at;
                                    stride.progress = 1;
                                }
                                else
                                {
                                    stride.to = stride.route.front();
                                    stride.route.erase(stride.route.begin());
                                }
                            }
                        }
                        // Staying under cover while it rains, or in the shade until cooled.
                        const bool refuge = (weather.rain && (!r.all_of<Umbrella>(movers[i]) || weather.storm) && paths.nodes()[at].covered) || (sun && mood.heat >= kTuning.needs.want / 2 && paths.nodes()[at].shaded);
                        if (errand.kind == ErrandKind::Wandering && !stride.route.empty())
                        {
                            stride.to = stride.route.front();
                            stride.route.erase(stride.route.begin());
                        }
                        else if (errand.kind == ErrandKind::Wandering && refuge)
                        {
                            stride.to = at;
                            stride.progress = 1;
                        }
                        else if (errand.kind == ErrandKind::Wandering && party != nullptr && errand.rest == ticks(kTuning.needs.idle_seconds) && random::chance(dice.state, kTuning.walking.huddle_chance))
                        {
                            party->huddle = ticks(static_cast<float>(random::between(dice.state, double { kTuning.walking.shortest_huddle }, double { kTuning.walking.longest_huddle })));
                            stride.to = at;
                            stride.progress = 1;
                        }
                        else if (errand.kind == ErrandKind::Wandering)
                            stride.to = wander(paths, at, came, dice);
                    }
                    if (stride.to == at)
                        stride.lane_to = stride.lane_from;
                    else if (!ringed)
                        stride.lane_to = new_lane(dice, stride.lane_from);
                    walk_on(paths, stride, left);
                    place(paths, stride, pose);
                }
            });
        }

        phase.emplace("Followers");
        // Followers take their leader's step, each in a lane of their own.
        {
            const auto view = r.view<Errand, Stride, Pose, Dice, const Member>();
            const auto followers = sorted(view);
            share(workers, followers.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [errand, stride, pose, dice, member] = view.get(followers[i]);
                    if (errand.kind != ErrandKind::Following)
                        continue;
                    stride.fresh = false;
                    if (r.all_of<Gesture>(followers[i]))
                        continue; // Showing how they feel; they catch up after
                    const Party& party = r.get<Party>(member.party);
                    const Stride& lead = r.get<Stride>(party.leader);
                    if (party.huddle > 0 && lead.from == lead.to && stride.to == lead.to && stride.progress >= 1)
                    {
                        // In a ring round a point just ahead of the leader, facing in, the leader standing on it.
                        const Pose& leader = r.get<Pose>(party.leader);
                        const glm::vec3 middle = paths.centre(lead.to);
                        glm::vec3 centre = leader.position + kTuning.walking.huddle_radius * glm::vec3(std::sin(leader.heading), 0.0f, std::cos(leader.heading));
                        centre.x = std::clamp(centre.x, middle.x - kLaneReach, middle.x + kLaneReach);
                        centre.z = std::clamp(centre.z, middle.z - kLaneReach, middle.z + kLaneReach);
                        const auto place_in_ring = static_cast<float>(std::ranges::find(party.members, followers[i]) - party.members.begin());
                        const auto leader_place = static_cast<float>(std::ranges::find(party.members, party.leader) - party.members.begin());
                        const float angle = std::atan2(leader.position.x - centre.x, leader.position.z - centre.z) + 2 * glm::pi<float>() * (place_in_ring - leader_place) / static_cast<float>(party.members.size());
                        // Stepping across to their place in the ring rather than jumping there.
                        const glm::vec3 spot = centre + kTuning.walking.huddle_radius * glm::vec3(std::sin(angle), 0.0f, std::cos(angle));
                        const glm::vec3 off = spot - pose.position;
                        const float reach = kTuning.walking.catch_up * stride.speed * dt;
                        pose.position = glm::length(off) <= reach ? spot : pose.position + off * (reach / glm::length(off));
                        pose.heading = angle + glm::pi<float>();
                        stride.from = lead.to;
                        stride.to = lead.to;
                        stride.lane_from = stride.lane_to = glm::vec2(pose.position.x - middle.x, pose.position.z - middle.z);
                        continue;
                    }
                    // A tick's walk, hurrying to make up ground: on through tiles until it is spent or they are level
                    // with the leader, never passing them.
                    float left = kTuning.walking.catch_up * stride.speed * dt;
                    for (int tiles = 0; tiles < 3 && left > 0; ++tiles)
                    {
                        const bool in_step = stride.from == lead.from && stride.to == lead.to;
                        if (in_step || (stride.from != stride.to && stride.progress < 1))
                        {
                            // In step, closing in on the leader; else finishing their own step first.
                            const float until = in_step ? lead.progress : 1.0f;
                            if (stride.progress >= until)
                                break;
                            const float length = step_length(paths, stride);
                            const float gain = length > 0 ? std::min(left / length, until - stride.progress) : until - stride.progress;
                            stride.progress += gain;
                            left -= gain * length;
                            if (in_step)
                                break;
                            continue;
                        }
                        // At a tile, behind: the leader's step when it starts here, else the next tile towards them.
                        const NodeId at = stride.to;
                        stride.lane_from = stride.lane_to;
                        stride.from = at;
                        stride.progress = 0;
                        stride.fresh = true;
                        if (at == lead.from)
                            stride.to = lead.to;
                        else if (const auto way = paths.route(at, lead.from); way && !way->empty())
                            stride.to = way->front();
                        else
                        {
                            // No walk to them: put beside the leader, as before.
                            stride.from = lead.from;
                            stride.to = lead.to;
                            stride.progress = lead.progress;
                        }
                        stride.lane_to = stride.to != stride.from ? new_lane(dice, stride.lane_from) : stride.lane_from;
                        if (stride.from == stride.to)
                            break;
                    }
                    place(paths, stride, pose);
                }
            });
        }

        phase.emplace("Arrivals");
        // 3. Arrivals, in entity order. A leader's followers arrive with them.
        const auto buy = [&](entt::entity guest, entt::entity at, const Shop& shop, bool chose) {
            auto& attraction = r.get<Attraction>(at);
            auto& wallet = r.get<Wallet>(guest);
            auto& mood = r.get<Mood>(guest);
            auto& needs = r.get<Needs>(guest);
            float* need = shop.product == Product::Drink ? &needs.thirst : shop.product == Product::Food ? &needs.hunger : shop.product == Product::Toilet ? &needs.bladder : shop.product == Product::FirstAid ? &mood.nausea : nullptr;
            const bool umbrella = shop.product == Product::Umbrella;
            const bool cools = shop.serve == Serve::Cold && mood.heat >= kTuning.needs.want;
            if (!chose && !cools && (umbrella ? !weather.rain || r.all_of<Umbrella>(guest) : need != nullptr ? *need < kTuning.needs.want : wallet.cash >= kTuning.facilities.low_cash))
                return; // Came along without wanting anything
            if (!attraction.open || wallet.cash < attraction.price)
            {
                mood.happiness = clamp01(mood.happiness - kTuning.needs.treat);
                if (wallet.cash < attraction.price)
                    think(r, guest, ThoughtKind::CantAfford, at, now);
                return;
            }
            wallet.cash -= attraction.price;
            attraction.income += attraction.price;
            ++attraction.customers;
            if (need != nullptr)
                *need = 0;
            else if (umbrella)
                r.emplace_or_replace<Umbrella>(guest);
            else
                wallet.cash += kTuning.facilities.withdraw;
            mood.happiness = clamp01(mood.happiness + kTuning.needs.treat);
            // Something cold on a hot day or hot on a cold one pleases the more, and something cold cools.
            if ((shop.serve == Serve::Cold && (weather.temperature >= kTuning.weather.hot || mood.heat >= kTuning.needs.want)) || (shop.serve == Serve::Hot && weather.temperature < kTuning.weather.cold))
                mood.happiness = clamp01(mood.happiness + kTuning.needs.treat);
            if (shop.serve == Serve::Cold)
                mood.heat = 0;
            if (shop.product == Product::Drink || shop.product == Product::Food)
                r.emplace_or_replace<Rubbish>(guest, Rubbish { .kind = std::uint8_t { shop.product == Product::Drink } });
        };
        for (std::size_t i = 0; i < movers.size(); ++i)
        {
            if (arrived[i] == entt::null || !r.valid(arrived[i]))
                continue;
            const entt::entity guest = movers[i];
            std::vector<entt::entity> followers;
            if (const auto* member = r.try_get<Member>(guest))
            {
                for (const entt::entity m : r.get<Party>(member->party).members)
                    if (m != guest && r.get<Errand>(m).kind == ErrandKind::Following)
                        followers.push_back(m);
            }
            auto& errand = r.get<Errand>(guest);
            if (auto* shop = r.try_get<Shop>(arrived[i]))
            {
                errand = { ErrandKind::Wandering, entt::null, ticks(kConsumeSeconds) };
                buy(guest, arrived[i], *shop, !r.all_of<Member>(guest));
                for (const entt::entity m : followers)
                    buy(m, arrived[i], *shop, false);
            }
            else if (r.all_of<ParkGate>(arrived[i]))
            {
                if (const auto* member = r.try_get<Member>(guest))
                    r.destroy(member->party);
                for (const entt::entity m : followers)
                    leave(r, m);
                leave(r, guest);
            }
            else if (auto* ride = r.try_get<CycleRide>(arrived[i]))
            {
                // A full line puts them off: they think so and go elsewhere, and the party with them.
                if (ride->queue.size() >= queue_room(*ride))
                {
                    think(r, guest, ThoughtKind::QueueTooLong, arrived[i], now);
                    errand = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle_seconds) };
                    continue;
                }
                // In at the far end of the line, with those of the party who want to come; the rest wait (phase 5).
                // Each stands to the other side of the line's middle from the guest ahead.
                const auto join = [&](entt::entity g) {
                    auto& dice = r.get<Dice>(g).state;
                    const float ahead = ride->queue.empty() || !r.valid(ride->queue.back()) ? 0 : r.get<Stride>(ride->queue.back()).side;
                    const float side = kTuning.queues.reach * static_cast<float>(random::between(dice, 0.2, 1.0));
                    ride->queue.push_back(g);
                    r.get<Errand>(g) = { ErrandKind::Queueing, arrived[i], 0 };
                    auto& stride = r.get<Stride>(g);
                    stride.along = ride->line.empty() ? 0 : static_cast<float>(ride->line.size() - 1);
                    stride.side = ahead > 0 || (ahead == 0 && random::chance(dice, 0.5)) ? -side : side;
                };
                join(guest);
                for (const entt::entity m : followers)
                    if (r.get<Mood>(m).nausea < kTuning.needs.max_nausea && random::chance(r.get<Dice>(m).state, kTuning.walking.join_chance))
                        join(m);
            }
        }

        phase.emplace("Ride walks");
        // Riders on their feet walk on to their seats, stopping at the ticket spot first, or off to the paths.
        for (const entt::entity g : sorted(r.view<RideWalk>()))
        {
            auto& walk = r.get<RideWalk>(g);
            auto& pose = r.get<Pose>(g);
            const entt::entity ride = r.get<Errand>(g).target;
            if (!walk.off && walk.next == 1 && walk.pause > 0)
            {
                walk.pause -= dt;
                if (const auto* walks = r.valid(ride) ? r.try_get<RideWalks>(ride) : nullptr)
                    pose.heading = walks->ticket_heading;
                continue;
            }
            float stride = r.get<Stride>(g).speed * dt;
            while (stride > 0 && walk.next < walk.points.size())
            {
                const glm::vec3 to = walk.points[walk.next] - pose.position;
                const float left = glm::length(to);
                if (glm::length(glm::vec2(to.x, to.z)) > 0)
                    pose.heading = std::atan2(to.x, to.z);
                if (left > stride)
                {
                    pose.position += to * (stride / left);
                    break;
                }
                pose.position = walk.points[walk.next++];
                stride -= left;
                if (!walk.off && walk.next == 1)
                    break; // At the ticket spot
            }
            if (walk.next < walk.points.size())
                continue;
            const std::optional<GestureKind> gesture = walk.gesture;
            const bool off = walk.off;
            r.remove<RideWalk>(g);
            if (off && r.valid(ride) && r.all_of<CycleRide>(ride))
                let_out(r, paths, g, ride, gesture);
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
                upkeep.since_inspection += dt;
            if ((upkeep.sample_clock += dt) >= kDowntimeSampleSeconds)
            {
                upkeep.sample_clock -= kDowntimeSampleSeconds;
                upkeep.downtime[0] += upkeep.broken && upkeep.downtime[0] < kDowntimeSamples;
                if (++upkeep.samples >= kDowntimeSamples)
                {
                    std::shift_right(upkeep.downtime.begin(), upkeep.downtime.end(), 1);
                    upkeep.downtime[0] = 0;
                    upkeep.samples = 0;
                }
            }
            for (const entt::entity guest : ride.queue)
                if (r.valid(guest) && (r.get<Errand>(guest).queued += dt) >= kLongQueue)
                    think(r, guest, ThoughtKind::QueueingForAges, e, now);
            // Unhappy guests who have waited very long now and then give up and walk off.
            std::erase_if(ride.queue, [&](entt::entity guest) {
                if (!r.valid(guest) || r.get<Errand>(guest).queued < kTuning.queues.give_up_after || r.get<Mood>(guest).happiness >= kTuning.queues.give_up_mood || !random::chance(r.get<Dice>(guest).state, kTuning.queues.give_up_per_second * dt))
                    return false;
                think(r, guest, ThoughtKind::GaveUpQueue, e, now);
                r.get<Errand>(guest) = after_ride(r, guest, e);
                stop_at(paths, attraction.entrance, r.get<Stride>(guest), r.get<Pose>(guest));
                return true;
            });
            if (!ride.line.empty())
            {
                const float last = static_cast<float>(ride.line.size() - 1);
                float side_ahead = 0;
                for (std::size_t k = 0; k < ride.queue.size(); ++k)
                {
                    if (!r.valid(ride.queue[k]))
                        continue;
                    auto& stride = r.get<Stride>(ride.queue[k]);
                    enum : std::uint64_t
                    {
                        Side = 1,
                        Place = 2
                    };
                    // Guests brought in from a save have no side yet: across from the guest ahead too.
                    if (stride.side == 0)
                        stride.side = (side_ahead > 0 ? -kTuning.queues.reach : kTuning.queues.reach) * (0.2f + 0.8f * fixed_share(ride.queue[k], Side));
                    side_ahead = stride.side;
                    // Off their even place by a little, so the line is not spaced like a ruler.
                    const float place_in_line = std::clamp((static_cast<float>(k) + kTuning.queues.gap_spread * (fixed_share(ride.queue[k], Place) - 0.5f)) * kTuning.queues.spacing, 0.0f, last);
                    // Up to the place behind the guest ahead, in shuffles: a guest standing waits for a gap.
                    const bool was_moving = stride.from != stride.to;
                    const bool moving = stride.along > place_in_line && (was_moving || stride.along - place_in_line > kTuning.queues.shuffle);
                    if (moving)
                        stride.along = std::max(stride.along - stride.speed * dt / paths.tile_size, place_in_line);
                    // Between the tile behind and the one ahead, facing the front.
                    const auto behind = static_cast<std::size_t>(std::ceil(stride.along));
                    const auto ahead = static_cast<std::size_t>(std::floor(stride.along));
                    stride.from = ride.line[behind];
                    stride.to = moving && stride.along > place_in_line ? ride.line[ahead] : ride.line[behind];
                    stride.progress = static_cast<float>(behind) - stride.along;
                    auto& pose = r.get<Pose>(ride.queue[k]);
                    pose.position = glm::mix(paths.centre(ride.line[behind]), paths.centre(ride.line[ahead]), stride.progress);
                    pose.position.y = paths.height_between(ride.line[behind], ride.line[ahead], stride.progress);
                    // To one side of the line, across the way it runs here (on a tile's middle, the way on to the front).
                    const std::size_t from = ahead != behind ? behind : std::min(behind + 1, ride.line.size() - 1);
                    const std::size_t to = ahead != behind ? ahead : (behind > 0 ? behind - 1 : 0);
                    const glm::vec3 d = paths.centre(ride.line[to]) - paths.centre(ride.line[from]);
                    if (from != to && glm::length(glm::vec2(d.x, d.z)) > 0)
                    {
                        const glm::vec2 way = glm::normalize(glm::vec2(d.x, d.z));
                        pose.position.x += -way.y * stride.side;
                        pose.position.z += way.x * stride.side;
                        pose.heading = std::atan2(way.x, way.y);
                    }
                }
            }
            const auto* walks = r.try_get<RideWalks>(e);
            const auto unload = [&](std::vector<entt::entity>& riders) {
                for (std::size_t seat = 0; seat < riders.size(); ++seat)
                {
                    const entt::entity guest = riders[seat];
                    if (!r.valid(guest))
                        continue;
                    auto& mood = r.get<Mood>(guest);
                    const float felt = felt_excitement(ride.excitement, r.all_of<Trains>(e), age_of(r, guest), night);
                    mood.happiness = clamp01(mood.happiness + felt * 0.3f);
                    mood.nausea = clamp01(mood.nausea + ride.nausea * 0.2f * (1.5f - mood.tolerance) * (eating(r, guest) ? kTuning.sickness.full_stomach : 1));
                    if (ride.splash)
                    {
                        mood.wet = std::max(mood.wet, kTuning.weather.splash);
                        mood.heat = 0;
                    }
                    auto& dice = r.get<Dice>(guest).state;
                    std::optional<GestureKind> gesture;
                    if (felt >= kTuning.thoughts.enjoyed)
                    {
                        think(r, guest, ThoughtKind::Enjoyed, e, now);
                        constexpr std::array kGlad { GestureKind::Cheer, GestureKind::Cheer, GestureKind::Laugh };
                        gesture = kGlad[random::below(dice, kGlad.size())];
                    }
                    else if (felt >= kTuning.gestures.liked)
                        gesture = GestureKind::Nod;
                    const Taste taste = taste_of(r, guest);
                    if (ride.intensity > std::min(mood.tolerance + kTuning.sickness.wildest, taste.most))
                    {
                        think(r, guest, ThoughtKind::TooIntense, e, now);
                        gesture = GestureKind::Dizzy;
                    }
                    else if (ride.intensity < taste.least)
                    {
                        think(r, guest, ThoughtKind::TooTame, e, now);
                        gesture = GestureKind::Grumble;
                    }
                    // Off the seat and out on foot where the ride has a way out; still riding till on the paths.
                    if (walks != nullptr && seat < walks->off.size() && !walks->off[seat].empty())
                    {
                        RideWalk walk { .points = walks->off[seat], .off = true, .gesture = gesture };
                        walk.points.push_back(paths.centre(exit_of(r, e)));
                        r.get<Pose>(guest).position = walk.points.front();
                        r.emplace_or_replace<RideWalk>(guest, std::move(walk));
                    }
                    else
                        let_out(r, paths, guest, e, gesture);
                }
                riders.clear();
            };
            // Riders an hour over the time since the last departure, smoothed over the last few.
            if (ride.since_departure >= 0)
                ride.since_departure += dt;
            const auto depart = [&](std::size_t riding) {
                if (ride.since_departure > 0)
                {
                    const float rate = static_cast<float>(riding) * 3600 / ride.since_departure;
                    ride.per_hour = ride.per_hour > 0 ? std::lerp(ride.per_hour, rate, kTuning.wear.flow_smoothing) : rate;
                }
                ride.since_departure = 0;
                ride.loading = 0;
                // Each run with riders wears the ride, and may end in a breakdown that closes it.
                if (riding == 0)
                    return;
                upkeep.reliability = std::max(upkeep.reliability - kTuning.wear.per_run, 0.0f);
                if (random::chance(r.get<Dice>(e).state, kTuning.wear.breakdown_per_run * (1 - upkeep.reliability)))
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
            // A cycle ends when the ride's time is up, or a train has come round.
            auto* fleet = r.try_get<Trains>(e);
            std::optional<std::size_t> loading; // The train standing at the start, ready to load
            if (fleet != nullptr)
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                {
                    Coaster& train = fleet->trains[t];
                    if (upkeep.broken && upkeep.kind == Breakdown::EmergencyStop)
                        continue; // Halted on the track till fixed
                    if (drive(train, dt, room_ahead(fleet->trains, t)))
                        unload(fleet->riders[t]);
                    if (!train.running && train.travelled == 0 && !loading)
                        loading = t;
                }
            }
            else
            {
                // Done, but with its restraints or doors stuck shut, its riders cannot get off till it is fixed.
                const bool held = upkeep.broken && (upkeep.kind == Breakdown::RestraintsFailToOpen || upkeep.kind == Breakdown::DoorsFailToOpen);
                if (advance_cycle(ride, dt, held))
                    unload(ride.riders);
            }
            // Loads once the first in line has reached the front, as many as there are seats left, and leaves when
            // its departure settings say.
            // shortcut: the rest of a load boards from wherever they stand in the line, upgrade when guests are shown boarding.
            const bool at_front = !ride.queue.empty() && (ride.line.empty() || !r.valid(ride.queue.front()) || r.get<Stride>(ride.queue.front()).along < 0.5f);
            const bool ready = fleet != nullptr ? loading.has_value() : ride.phase == RidePhase::Loading;
            auto* riders = !ready ? nullptr : fleet != nullptr ? &fleet->riders[*loading] : &ride.riders;
            // On a ride with ways on, riders walk to their seats one by one past the ticket spot, the next
            // stepping up once the one ahead has handed over their ticket.
            const bool walk_on = walks != nullptr && !walks->on.empty();
            const auto on_their_way = [&](entt::entity g) {
                const auto* walk = r.valid(g) ? r.try_get<RideWalk>(g) : nullptr;
                return walk != nullptr && !walk->off;
            };
            const auto at_ticket = [&](entt::entity g) {
                const auto* walk = r.valid(g) ? r.try_get<RideWalk>(g) : nullptr;
                return on_their_way(g) && (walk->next == 0 || walk->pause > 0);
            };
            if (riders != nullptr && at_front && riders->size() < ride.capacity && !(walk_on && std::ranges::any_of(*riders, at_ticket)))
            {
                std::size_t boarding = std::min<std::size_t>(ride.capacity - riders->size(), ride.queue.size());
                // A party in line rides on one load: cut through by the seats left, it waits for the next when it
                // fits one.
                if (boarding < ride.queue.size())
                {
                    const auto party_of = [&](entt::entity g) {
                        const auto* m = r.valid(g) ? r.try_get<Member>(g) : nullptr;
                        return m != nullptr ? m->party : entt::null;
                    };
                    if (const entt::entity cut = party_of(ride.queue[boarding]); cut != entt::null && boarding > 0 && party_of(ride.queue[boarding - 1]) == cut)
                    {
                        std::size_t first = boarding;
                        while (first > 0 && party_of(ride.queue[first - 1]) == cut)
                            --first;
                        std::size_t last = boarding;
                        while (last < ride.queue.size() && party_of(ride.queue[last]) == cut)
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
                    const entt::entity guest = ride.queue[i];
                    if (!r.valid(guest))
                        continue;
                    auto& wallet = r.get<Wallet>(guest);
                    if (!attraction.open || wallet.cash < attraction.price)
                    {
                        if (wallet.cash < attraction.price)
                            think(r, guest, ThoughtKind::CantAfford, e, now);
                        r.get<Errand>(guest) = after_ride(r, guest, e);
                        continue;
                    }
                    wallet.cash -= attraction.price;
                    attraction.income += attraction.price;
                    ++attraction.customers;
                    r.get<Errand>(guest).kind = ErrandKind::Riding;
                    if (const std::size_t seat = riders->size(); walk_on && seat < walks->on.size())
                    {
                        RideWalk walk { .points = { walks->ticket }, .pause = kTuning.queues.ticket_seconds };
                        walk.points.insert(walk.points.end(), walks->on[seat].begin(), walks->on[seat].end());
                        r.emplace_or_replace<RideWalk>(guest, std::move(walk));
                    }
                    riders->push_back(guest);
                }
                ride.queue.erase(ride.queue.begin(), ride.queue.begin() + static_cast<std::ptrdiff_t>(boarding));
            }
            if (riders != nullptr && !upkeep.broken)
            {
                const auto* departure = r.try_get<Departure>(e);
                // It waits for those still walking to their seats.
                if (std::ranges::none_of(*riders, on_their_way) && (attraction.testing || departs(departure != nullptr ? *departure : Departure { }, riders->size(), ride.capacity, ride.loading)))
                {
                    depart(riders->size());
                    if (fleet != nullptr)
                        fleet->trains[*loading].running = true;
                    else
                    {
                        // shortcut: every flat ride loads all at once and runs its loops; wheels that turn a step per
                        // pair seated, shows and rides in sections are left out, upgrade with the ride menu's setups.
                        ride.phase = RidePhase::Starting;
                        ride.phase_seconds = 0;
                    }
                }
                else
                    ride.loading += dt;
            }
            // In the rain, guests with nothing over them leave an open-air ride's queue.
            if (weather.rain && ride.cover < kTuning.weather.covered)
            {
                std::erase_if(ride.queue, [&](entt::entity guest) {
                    if (!r.valid(guest) || r.all_of<Umbrella>(guest) || paths.nodes()[r.get<Stride>(guest).from].covered || !random::chance(r.get<Dice>(guest).state, kTuning.weather.rain_leave_per_second * (weather.storm ? 3 : 1) * dt))
                        return false;
                    think(r, guest, ThoughtKind::RainKeepsMeOff, e, now);
                    r.get<Errand>(guest) = after_ride(r, guest, e);
                    stop_at(paths, attraction.entrance, r.get<Stride>(guest), r.get<Pose>(guest));
                    return true;
                });
            }
            // A broken ride's queue gives up, put out at the far end of the line.
            if (upkeep.broken)
            {
                for (const entt::entity guest : ride.queue)
                {
                    if (!r.valid(guest))
                        continue;
                    auto& mood = r.get<Mood>(guest);
                    mood.happiness = clamp01(mood.happiness - kTuning.wear.breakdown_gloom);
                    r.get<Errand>(guest) = after_ride(r, guest, e);
                    stop_at(paths, attraction.entrance, r.get<Stride>(guest), r.get<Pose>(guest));
                }
                ride.queue.clear();
            }
            // When the player asks, a train standing at the start goes, empty if need be, once it has waited its least
            // and another is held up behind it (within a centimetre: positions far round a long track lose that much
            // to rounding).
            const auto* leaving = r.try_get<Departure>(e);
            if (fleet != nullptr && loading && leaving != nullptr && leaving->leave_on_arrival && ride.loading >= static_cast<float>(leaving->min_wait) && !fleet->trains[*loading].running && !upkeep.broken && std::ranges::none_of(fleet->riders[*loading], on_their_way))
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                    if (t != *loading && fleet->trains[t].running && room_ahead(fleet->trains, t) < 0.01f)
                    {
                        fleet->trains[*loading].running = true;
                        depart(fleet->riders[*loading].size());
                    }
            }
        }

        phase.emplace("Parties");
        // 5. Parties: while some are in a ride's queue or on it, the others walk to its exit and wait; once all
        // are off, they go on together behind the leader.
        for (const entt::entity e : sorted(r.view<Party>()))
        {
            const Party& party = r.get<Party>(e);
            entt::entity ride = entt::null;
            bool waiting = false;
            for (const entt::entity m : party.members)
            {
                const Errand& errand = r.get<Errand>(m);
                if (errand.kind == ErrandKind::Queueing || errand.kind == ErrandKind::Riding)
                    ride = errand.target;
                waiting = waiting || errand.kind == ErrandKind::Waiting;
            }
            if (ride != entt::null && r.valid(ride) && r.all_of<CycleRide>(ride))
            {
                const NodeId exit = r.get<CycleRide>(ride).exit != kNoNode ? r.get<CycleRide>(ride).exit : r.get<Attraction>(ride).entrance;
                const NodeId spot = waiting_spot(paths, exit);
                for (const entt::entity m : party.members)
                {
                    auto& errand = r.get<Errand>(m);
                    auto& stride = r.get<Stride>(m);
                    // Those going to wait, and riders just off, who walk over to the others.
                    const bool off = errand.kind == ErrandKind::Waiting && stride.route.empty() && stride.progress >= 1 && stride.to == exit && spot != exit;
                    if (errand.kind != ErrandKind::Following && !(m == party.leader && errand.kind == ErrandKind::Wandering) && !off)
                        continue;
                    const NodeId at = stride.progress < 0.5f ? stride.from : stride.to;
                    stride.route = paths.route(at, spot).value_or(std::vector<NodeId> { });
                    stride.from = at;
                    stride.to = at;
                    stride.progress = 1;
                    stride.lane_to = stride.lane_from;
                    errand = { ErrandKind::Waiting, ride, 0 };
                }
            }
            else if (waiting)
            {
                for (const entt::entity m : party.members)
                {
                    auto& errand = r.get<Errand>(m);
                    if (m == party.leader)
                        errand = errand.kind == ErrandKind::Waiting ? Errand { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle_seconds) } : errand;
                    else
                        errand = { ErrandKind::Following, entt::null, 0 };
                }
            }
        }
        phase.emplace("Tidy");
        tidy(world);
        phase.reset();
        ++world.tick;
    }

} // namespace rct3::simulation
