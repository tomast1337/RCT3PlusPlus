// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/guests.hpp"

#include "profiling/profiler.hpp"
#include "simulation/animals.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"
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
#include <tuple>
#include <utility>
#include <vector>

namespace rct3::simulation
{
    namespace
    {
        /// The nearest spawn point a guest on path node `from` can walk to, and the way there; none without one.
        std::optional<std::pair<entt::entity, std::vector<NodeId>>> way_off(const entt::registry& r, const PathNetwork& paths, NodeId from)
        {
            std::vector<entt::entity> spawns;
            std::vector<NodeId> nodes;
            for (const auto [e, spawn] : r.view<const GuestSpawn>().each())
            {
                spawns.push_back(e);
                nodes.push_back(spawn.node);
            }
            auto found = nodes.empty() ? std::nullopt : paths.route_nearest(from, nodes);
            if (!found)
                return std::nullopt;
            return std::pair { spawns[found->first], std::move(found->second) };
        }

        /// Each of `party` pays the entrance fee, the party making up what one is short of. False, with nothing
        /// paid, when together they cannot pay for all of them: they are turned away, and no longer counted in.
        bool pay_entry(entt::registry& r, std::span<const entt::entity> party)
        {
            const units::Money fee = r.ctx().contains<Draw>() ? r.ctx().get<Draw>().entrance_fee : units::Money { };
            if (fee.value <= 0)
                return true;
            if (std::ranges::fold_left(party, std::int64_t { 0 }, [&](std::int64_t sum, entt::entity m) { return sum + r.get<Wallet>(m).cash.value; }) >= fee.value * static_cast<std::int64_t>(party.size()))
            {
                for (const entt::entity m : party)
                    pay(r, m, fee, &Wallet::spent_entry, park::LedgerLine::ParkEntranceTickets);
                return true;
            }
            for (const entt::entity m : party)
                --r.ctx().get<Turnstile>().in[std::to_underlying(r.get<Visitor>(m).age)];
            ++r.ctx().emplace<Turnstile>().turned_away;
            return false;
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

        /// Whether `pool` has as many guests as it takes: those changing, swimming or on its rides, by the room they
        /// came in at or swim on from, and with `on_way` those walking to one of its rooms.
        bool pool_full(const entt::registry& r, entt::entity pool, bool on_way)
        {
            const auto* water = r.valid(pool) ? r.try_get<SwimPool>(pool) : nullptr;
            if (water == nullptr)
                return false;
            std::size_t n = 0;
            for (const auto [e, errand] : r.view<const Errand>().each())
            {
                const auto* swimmer = r.try_get<Swimmer>(e);
                const entt::entity room = errand.kind == ErrandKind::Swimming || (on_way && errand.kind == ErrandKind::Heading) ? errand.target : swimmer != nullptr ? swimmer->room : entt::null;
                n += r.valid(room) && r.all_of<PoolDoor>(room) && r.get<PoolDoor>(room).pool == pool;
            }
            return static_cast<float>(n) >= kTuning.pools.crowd * static_cast<float>(water->water.size());
        }

        std::vector<Offer> offers(const entt::registry& r)
        {
            std::vector<Offer> out;
            for (const entt::entity e : sorted(r.view<const Attraction>()))
            {
                const Attraction& a = r.get<Attraction>(e);
                if (!a.open || a.entrance == kNoNode)
                    continue;
                if (const auto* shop = r.try_get<Shop>(e))
                    out.push_back({ e, AttractionKind::Shop, shop->product, a.entrance, a.price.value, 0, 1, shop->serve, false });
                else if (const auto* ride = r.try_get<CycleRide>(e))
                {
                    // A ride with more than one station is on offer at each, guests going to the nearest.
                    out.push_back({ e, AttractionKind::Ride, Product::Drink, a.entrance, a.price.value, ride->intensity.value, ride->cover.value, Serve::Any, ride->splash });
                    for (const Stop& stop : ride->stops)
                        if (stop.entrance != kNoNode)
                            out.push_back({ e, AttractionKind::Ride, Product::Drink, stop.entrance, a.price.value, ride->intensity.value, ride->cover.value, Serve::Any, ride->splash });
                }
                else if (const auto* door = r.try_get<PoolDoor>(e))
                {
                    // A full pool is not on offer till some leave.
                    if (pool_full(r, door->pool, true))
                        continue;
                    out.push_back({ e, AttractionKind::Pool, Product::Drink, a.entrance, a.price.value, 0, 0, Serve::Any, false });
                }
            }
            return out;
        }

        /// The nearest offer the guest can pay for that `wanted` picks, with the walk there.
        template<typename Pick> std::optional<std::pair<entt::entity, std::vector<NodeId>>> nearest_offer(const PathNetwork& paths, NodeId at, const std::vector<Offer>& offers, const Wallet& wallet, const Pick& pick)
        {
            std::vector<entt::entity> wanted;
            std::vector<NodeId> entrances;
            std::int64_t dearest = 0;
            for (const Offer& o : offers)
            {
                if (o.price > wallet.cash.value || !pick(o))
                    continue;
                wanted.push_back(o.entity);
                entrances.push_back(o.entrance);
                dearest = std::max(dearest, o.price);
            }
            if (wanted.empty())
                return std::nullopt;
            // A ride there on the way only with what they would still have left for it.
            auto found = paths.route_nearest(at, entrances, Walker::Guest, wallet.cash.value - dearest);
            if (!found)
                return std::nullopt;
            return std::pair { wanted[found->first], std::move(found->second) };
        }

        /// How many guests a ride's queue `line` holds; no limit for a ride guests walk straight into.
        std::size_t queue_room(const std::vector<NodeId>& line)
        {
            if (line.empty())
                return std::numeric_limits<std::size_t>::max();
            // Up to the middle of the line's far tile.
            return static_cast<std::size_t>(static_cast<float>(line.size() - 1) / kTuning.queues.spacing.value) + 1;
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
            const float hot = mood.heat.value;
            std::array<Want, 8> wants { {
                { rained_on && mood.wet.value > 0 ? 1.0f : 0.0f, Product::Umbrella },
                { needs.thirst.value, Product::Drink },
                { needs.hunger.value, Product::Food },
                { hot, Product::Drink, true },
                { hot, Product::Food, true },
                { needs.bladder.value, Product::Toilet },
                { mood.nausea.value, Product::FirstAid },
                { (wallet.cash.value < kTuning.facilities.low_cash.value || wallet.needs_cash) && wallet.bank.value > 0 ? 1.0f : 0.0f, Product::CashMachine },
            } };
            std::ranges::stable_sort(wants, std::greater { }, &Want::need);
            const Serve suits = hot >= kTuning.needs.want.value || temperature >= kTuning.weather.hot.value ? Serve::Cold : temperature < kTuning.weather.cold.value ? Serve::Hot : Serve::Any;
            const Serve wrong = suits == Serve::Cold ? Serve::Hot : suits == Serve::Hot ? Serve::Cold : Serve::Any;
            // What suits the weather, else anything but what does not.
            const auto shop = [&](Product product, bool only_suits) { return nearest_offer(paths, at, offers, wallet, [&](const Offer& o) { return o.kind == AttractionKind::Shop && o.product == product && (only_suits ? o.serve == suits : o.serve != wrong || wrong == Serve::Any); }); };
            for (const Want& want : wants)
            {
                if (want.need < kTuning.needs.want.value)
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
                // Only dearer ones than they can pay for: off for more cash first.
                if (wallet.bank.value > 0 && std::ranges::any_of(offers, [&](const Offer& o) { return o.kind == AttractionKind::Shop && o.product == want.product && o.price > wallet.cash.value; }))
                    if (auto found = shop(Product::CashMachine, false))
                        return found;
            }
            // A swim, when they wish for one enough for the weather.
            if (needs.swim.value >= swim_wish_needed(temperature))
                if (auto found = nearest_offer(paths, at, offers, wallet, [](const Offer& o) { return o.kind == AttractionKind::Pool; }))
                    return found;
            if (mood.nausea.value >= kTuning.needs.max_nausea.value || !random::chance(dice.state, kTuning.needs.ride_chance.value))
                return std::nullopt;
            const float wildest = std::min(mood.tolerance.value + kTuning.sickness.wildest.value, taste.most.value) + daring;
            const float least_cover = rained_on ? kTuning.weather.covered.value : 0;
            const auto ride = [&](bool splash) {
                const auto suits = [&](const Offer& o) { return o.kind == AttractionKind::Ride && o.intensity <= wildest && o.intensity >= taste.least.value && o.cover >= least_cover && (!splash || o.splash); };
                // The nearest of those that come to mind, so a ride behind another on the same path still gets riders.
                if (auto found = nearest_offer(paths, at, offers, wallet, [&](const Offer& o) { return suits(o) && random::chance(dice.state, kTuning.needs.ride_considered.value); }))
                    return found;
                return nearest_offer(paths, at, offers, wallet, suits);
            };
            if (hot >= kTuning.needs.want.value)
                if (auto found = ride(true))
                    return found;
            return ride(false);
        }

        glm::vec3 lane_point(const PathNetwork& paths, NodeId node, glm::vec2 lane)
        {
            return paths.centre(node) + glm::vec3(lane.x, 0.0f, lane.y);
        }

        /// The lane next after `was` on the way to `want`, at most a lane's drift across, so walkers never cut
        /// sharply over the path.
        glm::vec2 lane_towards(glm::vec2 was, glm::vec2 want)
        {
            const float most = kTuning.walking.lane_drift.value;
            return glm::clamp(was + glm::clamp(want - was, -most, most), -kLaneReach, kLaneReach);
        }

        /// The lane of the `place`th of `of` standing together in a ring on a tile: the more of them, the wider
        /// the ring, so neighbours keep apart.
        glm::vec2 ring_lane(std::size_t place, std::size_t of)
        {
            const float n = static_cast<float>(std::max<std::size_t>(of, 1));
            const float radius = std::clamp(kTuning.walking.ring_gap.value * n / (2 * glm::pi<float>()), kTuning.walking.huddle_radius.value, kLaneReach);
            const float angle = 2 * glm::pi<float>() * static_cast<float>(place) / n;
            return radius * glm::vec2(std::sin(angle), std::cos(angle));
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

        /// Where a guest walks off their party's middle: their place in a ring, so no two members walk in one
        /// another. Nowhere off it for a guest on their own.
        glm::vec2 party_slot(const entt::registry& r, entt::entity guest)
        {
            const auto [place, of] = place_in_party(r, guest);
            return of > 1 ? ring_lane(place, of) : glm::vec2 { 0.0f };
        }

        /// Where on its entrance tile `guest` stands at stall `shop`'s counter: at the tile's edge towards the
        /// stall, at one of a few places along it, by who they are.
        glm::vec2 counter_lane(const Shop& shop, entt::entity guest)
        {
            constexpr float kReach = 1.2f;                                       ///< Chosen by eye: just short of kLaneReach
            constexpr std::array<float, 4> kPlaces { -0.9f, -0.3f, 0.3f, 0.9f }; ///< Chosen by eye: along the counter
            const glm::vec2 across { -shop.counter.y, shop.counter.x };
            return shop.counter * kReach + across * kPlaces[entt::to_integral(guest) % kPlaces.size()];
        }

        /// A wandering guest (with their party, all walking with them) that may go home now: one done with
        /// the park, by night a family, in a storm anyone. Where they go, and the way there: out of the gate and
        /// on to the nearest spawn point, or the gate itself when no spawn point can be reached from it. A party
        /// goes by how done its members are on average. `cash_machine` is whether the park has one open.
        std::optional<std::pair<entt::entity, std::vector<NodeId>>> going_home(const entt::registry& r, const PathNetwork& paths, entt::entity guest, NodeId at, Dice& dice, bool night, bool storm, bool cash_machine, std::uint64_t tick)
        {
            // A guest of honour stays for their whole visit, then heads straight out.
            const auto* vip = r.try_get<Vip>(guest);
            if (vip != nullptr && !vip->leaving)
                return std::nullopt;
            const auto done = [&](entt::entity g) {
                double chance = 0;
                if (const auto* mood = r.try_get<Mood>(g))
                    chance = mood->happiness.value < kTuning.leaving.fed_up.value ? kTuning.leaving.fed_up_leave.value : mood->energy.value < kTuning.leaving.worn_out.value && mood->happiness.value < kTuning.leaving.enjoying.value ? kTuning.leaving.worn_out_leave.value : 0;
                // shortcut: guests from a save count their stay from when the park was loaded; read their saved
                // time in the park once its unit is settled.
                const float hours = static_cast<float>(tick - r.get<Visitor>(g).arrived) * kTickSeconds / kSecondsPerHour;
                chance += hours > 2 * kTuning.leaving.long_stay_hours ? 2 * kTuning.leaving.long_stay.value : hours > kTuning.leaving.long_stay_hours ? kTuning.leaving.long_stay.value : 0;
                if (const auto* wallet = r.try_get<Wallet>(g); wallet != nullptr && wallet->cash.value < kTuning.facilities.low_cash.value && (!cash_machine || wallet->bank.value <= 0))
                    chance += kTuning.leaving.broke.value;
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
            const double chance = fed_up + (night && age_of(r, guest) != AgeGroup::Teen ? kTuning.leaving.night.value : 0) + (storm ? kTuning.leaving.storm.value : 0);
            if (vip == nullptr && (chance == 0 || !random::chance(dice.state, chance * kTuning.needs.idle.value)))
                return std::nullopt;
            if (const auto* member = r.try_get<Member>(guest); member != nullptr && (!r.get<Party>(member->party).away.empty() || std::ranges::any_of(r.get<Party>(member->party).members, [&](entt::entity m) { return m != guest && r.get<Errand>(m).kind != ErrandKind::Following; })))
                return std::nullopt;
            for (const auto [gate, at_gate] : r.view<const ParkGate>().each())
                if (auto walk = paths.route(at, at_gate.node, Walker::Guest, r.get<Wallet>(guest).cash.value))
                {
                    if (auto off = way_off(r, paths, at_gate.node))
                    {
                        walk->append_range(off->second);
                        return std::pair { off->first, std::move(*walk) };
                    }
                    return std::pair { gate, std::move(*walk) };
                }
            return std::nullopt;
        }

        /// The guest goes home: they stop needing, feeling and walking, and leave their party.
        void leave(entt::registry& r, entt::entity guest)
        {
            // Those turned away at the gate never came in.
            if (const auto* outside = r.try_get<Outside>(guest); outside == nullptr || outside->way != Outside::Way::TurnedAway)
                ++r.ctx().emplace<Turnstile>().out[static_cast<std::size_t>(age_of(r, guest))];
            r.remove<Needs, Mood, Wallet, Errand, Stride, Dice, Member, Linger, Gesture, Glance, Watching, Carried, RideWalk, Crossing, Swimmer, PoolRoute, Outside>(guest);
            r.emplace<Left>(guest);
        }
    } // namespace

    float clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    float swim_wish_needed(float temperature)
    {
        const auto& pools = kTuning.pools;
        const float cold = pools.chill.value * std::max(0.0f, pools.cool.value - temperature);
        const float hot = pools.heat.value * std::max(0.0f, temperature - pools.warm.value);
        return std::max(pools.least_wish.value, kTuning.needs.want.value + cold - hot);
    }

    units::Money worth_of(const Ware& ware, units::Money flat, float temperature)
    {
        const std::size_t band = temperature < kTuning.weather.cold.value ? 1 : temperature >= kTuning.weather.hot.value ? 2 : 0;
        return ware.worth[band].value > 0 ? ware.worth[band] : flat;
    }

    bool pay(entt::registry& r, entt::entity guest, units::Money price, units::Money Wallet::* on, park::LedgerLine line)
    {
        std::vector<entt::entity> payers { guest };
        if (const auto* member = r.try_get<Member>(guest); member != nullptr && r.valid(member->party))
            for (const entt::entity m : r.get<Party>(member->party).members)
                if (m != guest && r.all_of<Wallet>(m))
                    payers.push_back(m);
        std::ranges::stable_sort(payers.begin() + 1, payers.end(), std::greater { }, [&](entt::entity m) { return r.get<Wallet>(m).cash.value; });
        if (std::ranges::fold_left(payers, std::int64_t { 0 }, [&](std::int64_t sum, entt::entity m) { return sum + r.get<Wallet>(m).cash.value; }) < price.value)
            return false;
        std::int64_t owed = price.value;
        for (const entt::entity m : payers)
        {
            Wallet& wallet = r.get<Wallet>(m);
            const std::int64_t part = std::clamp(wallet.cash.value, std::int64_t { 0 }, owed);
            wallet.cash.value -= part;
            wallet.spent.value += part;
            if (on != nullptr)
                (wallet.*on).value += part;
            owed -= part;
        }
        r.ctx().emplace<Receipts>().paid[std::to_underlying(line)].value += price.value;
        return true;
    }

    NodeId wander(const PathNetwork& paths, NodeId at, NodeId came, Dice& dice, Walker who, const staff::Patrol* patrol)
    {
        std::array<NodeId, 4> ways { };
        std::size_t count = 0;
        for (const NodeId n : paths.nodes()[at].next)
            if (n != kNoNode && n != came && !paths.nodes()[n].queue && paths.may_step(at, n, who) && (patrol == nullptr || patrol->covers(paths.nodes()[n].tile)))
                ways[count++] = n;
        if (count == 0)
            return came != kNoNode && paths.may_step(at, came, who) ? came : at;
        return ways[random::below(dice.state, count)];
    }

    glm::vec2 new_lane(Dice& dice, glm::vec2 was, glm::vec2 slot)
    {
        const glm::vec2 drift { static_cast<float>(random::between(dice.state, -kTuning.walking.lane_drift.value, kTuning.walking.lane_drift.value)), static_cast<float>(random::between(dice.state, -kTuning.walking.lane_drift.value, kTuning.walking.lane_drift.value)) };
        const float reach = kLaneReach - glm::length(slot);
        return lane_towards(was, glm::clamp(was - slot + drift, -reach, reach) + slot);
    }

    float step_length(const PathNetwork& paths, const Stride& s)
    {
        return glm::distance(lane_point(paths, s.from, s.lane_from), lane_point(paths, s.to, s.lane_to));
    }

    void walk_on(const PathNetwork& paths, Stride& s, float left)
    {
        if (s.from == s.to || s.progress.value != 0 || left <= 0)
            return;
        const float length = step_length(paths, s);
        s.progress.value = length > 0 ? std::min(left / length, 1.0f) : 1;
    }

    void face_moved(Pose& pose, float dt)
    {
        const glm::vec2 moved { pose.position.x - pose.was.x, pose.position.z - pose.was.z };
        if (glm::dot(moved, moved) < 0.0001f)
            return;
        const float turn = std::remainder(std::atan2(moved.x, moved.y) - pose.was_heading.value, 2 * glm::pi<float>()) / 2;
        const float most = kTuning.walking.turn_rate * dt;
        pose.heading.value = pose.was_heading.value + std::clamp(turn, -most, most);
    }

    void place(const PathNetwork& paths, const Stride& s, Pose& pose)
    {
        const glm::vec3 a = lane_point(paths, s.from, s.lane_from);
        const glm::vec3 b = lane_point(paths, s.to, s.lane_to);
        pose.position = glm::mix(a, b, s.progress.value);
        pose.position.y = paths.surface(s.from, s.to, { pose.position.x, pose.position.z });
    }

    bool eating(const entt::registry& r, entt::entity guest)
    {
        const auto* carried = r.try_get<Carried>(guest);
        return carried != nullptr && carried->food && carried->bites > 0;
    }

    bool munching(const entt::registry& r, entt::entity guest)
    {
        const auto* carried = r.try_get<Carried>(guest);
        return carried != nullptr && carried->held && carried->bites > 0;
    }

    void free_hand(entt::registry& r, entt::entity guest, Carried& carried, NodeId at)
    {
        if (carried.pocketed < Carried::kPockets)
            ++carried.pocketed;
        else
            r.emplace<Litter>(r.create(), Litter { at, carried.kind, 0 });
        carried.held = false;
        if (carried.pocketed == 0)
            r.remove<Carried>(guest);
    }

    entt::entity be_sick(entt::registry& r, entt::entity guest, NodeId at)
    {
        const entt::entity mess = r.create();
        r.emplace<Litter>(mess, Litter { .node = at, .vomit = true });
        auto& mood = r.get<Mood>(guest);
        mood.nausea.value = clamp01(mood.nausea.value - kTuning.sickness.relief.value);
        r.emplace_or_replace<Gesture>(guest, GestureKind::Retch, kTuning.gestures.shown.value);
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
        return found != nullptr ? found->time.value : Daylight { }.time.value;
    }

    Weather weather_in(const entt::registry& r)
    {
        const auto* found = r.ctx().find<Weather>();
        return found != nullptr ? *found : Weather { };
    }

    float side_after(random::RandomState& dice, float ahead)
    {
        const float side = kTuning.queues.reach.value * static_cast<float>(random::between(dice, 0.2, 1.0));
        return ahead > 0 || (ahead == 0 && random::chance(dice, 0.5)) ? -side : side;
    }

    void step_guests(SimWorld& world, std::optional<Workers&> workers, const Weather& weather, bool night, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
        const float now = static_cast<float>(world.tick) * kTickSeconds;

        const auto shops = r.view<const Shop>();
        const bool cash_machine = std::ranges::any_of(shops, [&](entt::entity e) { return shops.get<const Shop>(e).product == Product::CashMachine; });
        // Under cover (roof), or in the shade (roof or trees): on a ride under cover, or on a path tile.
        const auto under = [&](entt::entity guest, bool shade) {
            const auto* errand = r.try_get<Errand>(guest);
            if (errand != nullptr && errand->kind == ErrandKind::Riding)
            {
                const auto* ride = r.valid(errand->target) ? r.try_get<CycleRide>(errand->target) : nullptr;
                return ride != nullptr && ride->cover.value >= kTuning.weather.covered.value;
            }
            const auto* stride = r.try_get<Stride>(guest);
            if (stride == nullptr || stride->from >= paths.nodes().size())
                return false;
            const PathNode& node = paths.nodes()[stride->from];
            return shade ? node.shaded : node.covered;
        };
        // Out of the rain: under an umbrella, or under cover.
        const auto sheltered = [&](entt::entity guest) { return r.all_of<Umbrella>(guest) || under(guest, false); };
        const bool sun = weather.sunny && weather.temperature.value >= kTuning.weather.hot.value;

        phase.emplace("Needs");
        // 1. Needs and moods.
        {
            const float thirst = 1 + kTuning.weather.heat_per_degree.value * std::max(weather.temperature.value - kTuning.weather.hot.value, 0.0f);
            const float hunger = 1 + kTuning.weather.heat_per_degree.value * std::max(kTuning.weather.cold.value - weather.temperature.value, 0.0f);
            const auto view = r.view<Needs, Mood>();
            const auto guests = sorted(view);
            share(workers, guests.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [needs, mood] = view.get<Needs, Mood>(guests[i]);
                    mood.nausea.value = clamp01(mood.nausea.value - kTuning.sickness.settle.value * dt / (eating(r, guests[i]) ? kTuning.sickness.full_stomach.value : 1));
                    needs.thirst.value = clamp01(needs.thirst.value + kTuning.needs.thirst.value * thirst * dt);
                    needs.hunger.value = clamp01(needs.hunger.value + kTuning.needs.hunger.value * hunger * dt);
                    // A bite now and then of what they bought, until it is gone.
                    if (auto* carried = r.try_get<Carried>(guests[i]); carried != nullptr && carried->bites > 0 && (carried->since.value += dt) >= kTuning.eating.bite.value)
                    {
                        float& need = carried->food ? needs.hunger.value : needs.thirst.value;
                        need = clamp01(need - carried->fill.value);
                        carried->since.value = 0;
                        --carried->bites;
                    }
                    if (weather.rain && !sheltered(guests[i]))
                        mood.wet.value = clamp01(mood.wet.value + kTuning.weather.wetting.value * (weather.storm ? 2 : 1) * dt);
                    else
                        mood.wet.value = clamp01(mood.wet.value - kTuning.weather.drying.value * dt);
                    // Wet is only a misery when it is not hot.
                    if (mood.wet.value >= kTuning.needs.want.value && weather.temperature.value < kTuning.weather.hot.value)
                        mood.happiness.value = clamp01(mood.happiness.value - kTuning.weather.wet_gloom.value * (weather.temperature.value < kTuning.weather.cold.value ? 2 : 1) * dt);
                    // The sun heats those out in it; shade, a cooler day and getting wet cool them.
                    const float sweat = sun && !under(guests[i], true) ? kTuning.weather.sweat.value * (1 + kTuning.weather.heat_per_degree.value * (weather.temperature.value - kTuning.weather.hot.value)) : -kTuning.weather.cooling.value;
                    mood.heat.value = clamp01(mood.heat.value + (sweat - (mood.wet.value > 0 ? kTuning.weather.cooling.value : 0)) * dt);
                    if (mood.heat.value >= kTuning.needs.want.value)
                        mood.happiness.value = clamp01(mood.happiness.value - kTuning.weather.heat_gloom.value * dt);
                    needs.bladder.value = clamp01(needs.bladder.value + kTuning.needs.bladder.value * dt);
                    // Swimming eases the wish to swim over a stay; out of the pool it grows as they like pools.
                    if (const auto* errand = r.try_get<Errand>(guests[i]); errand != nullptr && errand->kind == ErrandKind::Swimming)
                        needs.swim.value = clamp01(needs.swim.value - dt / kTuning.pools.stay.value);
                    else if (const auto* taste = r.try_get<Taste>(guests[i]))
                        needs.swim.value = clamp01(needs.swim.value + kTuning.pools.longing.value * taste->pools.value * dt);
                    mood.energy.value = clamp01(mood.energy.value - kTuning.needs.tiring.value * dt);
                    if (std::max({ needs.thirst.value, needs.hunger.value, needs.bladder.value }) >= kTuning.needs.pressing.value)
                        mood.happiness.value = clamp01(mood.happiness.value - kTuning.needs.gloom.value * dt);
                }
            });
        }

        for (auto [g, gesture] : r.view<Gesture>().each())
            if ((gesture.left.value -= dt) <= 0)
                r.remove<Gesture>(g);
        for (auto [g, glance] : r.view<Glance>().each())
            if ((glance.left.value -= dt) <= 0)
                r.remove<Glance>(g);
        for (auto [_, pose] : r.view<Pose>().each())
        {
            pose.walked.value += glm::distance(glm::vec2(pose.was.x, pose.was.z), glm::vec2(pose.position.x, pose.position.z));
            pose.was = pose.position;
            pose.was_heading.value = pose.heading.value;
        }

        phase.emplace("Walking");
        // Now and then, each open, working ride with more than one station becomes a set of links between them,
        // as long as the wait at the station boarded at, the ride on to the other and getting through both,
        // walked at an adult's pace. Its ticket is the toll.
        if (world.tick % ticks(kTuning.transport.refresh.value) == 0)
        {
            world.paths.clear_links();
            for (const auto [e, ride, attraction] : r.view<const CycleRide, const Attraction>().each())
            {
                const auto* fleet = r.try_get<Trains>(e);
                const auto* upkeep = r.try_get<Upkeep>(e);
                if (ride.stops.empty() || fleet == nullptr || fleet->trains.empty() || !attraction.open || attraction.testing || (upkeep != nullptr && upkeep->broken))
                    continue;
                const Coaster& track = fleet->trains.front();
                const float length = track.length();
                // Timed by its last lap, or by one driven now without stops when it has none.
                const float lap = ride.run.value > 0 ? ride.run.value : lap_seconds(track).value_or(0);
                if (length <= 0 || lap <= 0 || track.stops.size() != ride.stops.size())
                    continue;
                const float pace = kTuning.walking.speed[2].value / paths.tile_size;
                for (std::size_t from = 0; from <= ride.stops.size(); ++from)
                {
                    const auto& queue = from == 0 ? ride.queue : ride.stops[from - 1].queue;
                    // The wait: the queue ahead at the rate the ride takes riders, or a load at a time by laps
                    // before it has a rate.
                    const float wait = ride.per_hour > 0 ? static_cast<float>(queue.size()) * 3600 / ride.per_hour : static_cast<float>(queue.size() / std::max<std::uint32_t>(ride.capacity, 1)) * lap;
                    // How far on round the track from this station each is.
                    const auto ahead = [&](std::size_t k) { return std::fmod((k == 0 ? 0 : track.stops[k - 1]) - (from == 0 ? 0 : track.stops[from - 1]) + length, length); };
                    for (std::size_t to = 0; to <= ride.stops.size(); ++to)
                    {
                        if (to == from || (to > 0 && ride.stops[to - 1].exit == kNoNode))
                            continue;
                        const float along = ahead(to);
                        // Stations passed on the way, where the train stands too.
                        std::size_t passed = 0;
                        for (std::size_t k = 0; k <= ride.stops.size(); ++k)
                            passed += k != from && ahead(k) < along;
                        const float seconds = kTuning.transport.boarding.value + wait + lap * along / length + kTuning.transport.dwell.value * static_cast<float>(passed);
                        world.paths.link({ .from = entrance_of(r, e, from), .to = exit_of(r, e, to), .length = seconds * pace, .toll = attraction.price.value, .owner = entt::to_integral(e) });
                    }
                }
            }
        }
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
        // Of those come to a station to ride on across the park: the rest of their way, from its exit.
        std::vector<std::vector<NodeId>> crossing(movers.size());
        if (!paths.nodes().empty())
        {
            share(workers, movers.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [errand, stride, pose, dice, needs, mood, wallet] = movers_view.get(movers[i]);
                    stride.fresh = false;
                    // Served at a counter; a party member's turn there ends with the party (5.).
                    if (errand.kind == ErrandKind::Buying && !r.all_of<Member>(movers[i]) && (errand.rest == 0 || --errand.rest == 0))
                        errand = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                    if (errand.kind != ErrandKind::Wandering && errand.kind != ErrandKind::Heading && errand.kind != ErrandKind::Waiting)
                        continue;
                    if (stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                        continue; // Off the paths; nothing to walk on
                    if (const auto* watching = r.try_get<Watching>(movers[i]); r.any_of<Linger, Gesture>(movers[i]) || (watching != nullptr && !watching->chasing))
                        continue; // Sitting, watching or showing how they feel
                    if (const auto* vip = r.try_get<Vip>(movers[i]); vip != nullptr && vip->fans > 0 && !vip->leaving)
                        continue; // A VIP stops for the fans round them
                    if (errand.rest > 0)
                        --errand.rest;
                    // A leader is the only one to write their party, so this is safe beside the other guests.
                    const auto* member = r.try_get<Member>(movers[i]);
                    Party* party = member != nullptr && r.get<Party>(member->party).leader == movers[i] ? &r.get<Party>(member->party) : nullptr;
                    if (party != nullptr && party->huddle > 0 && errand.kind == ErrandKind::Wandering)
                    {
                        --party->huddle;
                        continue; // Standing with the others
                    }

                    float left = 0;
                    if (stride.from != stride.to)
                    {
                        const float length = step_length(paths, stride);
                        stride.progress.value += length > 0 ? gait_pace(stride.speed, mood.energy, stride.gait).value * dt / length : 1;
                        left = (stride.progress.value - 1) * length;
                    }
                    if (stride.progress.value < 1)
                    {
                        place(paths, stride, pose);
                        continue;
                    }

                    // At the tile `to`.
                    const NodeId came = stride.from;
                    const NodeId at = stride.to;
                    stride.from = at;
                    stride.progress.value = 0;
                    stride.fresh = true;
                    if (const auto* visitor = r.try_get<Visitor>(movers[i]))
                        stride.gait = pick_gait(visitor->age, mood, needs, dice.state);
                    stride.lane_from = stride.lane_to;
                    bool ringed = false;
                    const auto link = stride.route.empty() ? std::nullopt : paths.link_between(at, stride.route.front());
                    if (link && r.valid(entt::entity { link->owner }) && r.all_of<CycleRide>(entt::entity { link->owner }))
                    {
                        arrived[i] = entt::entity { link->owner };
                        crossing[i] = std::move(stride.route);
                        stride.route.clear();
                        stride.to = at;
                        stride.progress.value = 1;
                    }
                    else if (!stride.route.empty())
                    {
                        stride.to = stride.route.front();
                        stride.route.erase(stride.route.begin());
                        // Waiting for the others: in a ring with them round the middle of the party's own tile.
                        if (stride.route.empty() && errand.kind == ErrandKind::Waiting)
                        {
                            const auto [place, of] = place_in_party(r, movers[i]);
                            stride.lane_to = ring_lane(place, of);
                            ringed = true;
                        }
                        // The last step to a stall: up to its counter.
                        if (const auto* shop = stride.route.empty() && errand.kind == ErrandKind::Heading ? r.try_get<Shop>(errand.target) : nullptr)
                        {
                            stride.lane_to = counter_lane(*shop, movers[i]);
                            ringed = true;
                        }
                    }
                    else if (errand.kind != ErrandKind::Wandering)
                    {
                        if (errand.kind == ErrandKind::Heading)
                            arrived[i] = errand.target;
                        stride.to = at;
                        stride.progress.value = 1;
                        // Standing in the ring, facing its middle.
                        if (errand.kind == ErrandKind::Waiting && glm::length(stride.lane_from) > 0.01f)
                            pose.heading.value = std::atan2(-stride.lane_from.x, -stride.lane_from.y);
                    }
                    else
                    {
                        if (errand.rest == 0)
                        {
                            errand.rest = ticks(kTuning.needs.idle.value);
                            // A party goes where its neediest member needs to; whoever is sickest decides on rides.
                            Needs want = needs;
                            Mood feel = mood;
                            if (const auto* member = r.try_get<Member>(movers[i]))
                            {
                                for (const entt::entity m : r.get<Party>(member->party).members)
                                {
                                    const Needs& n = r.get<Needs>(m);
                                    want = { std::max(want.hunger.value, n.hunger.value), std::max(want.thirst.value, n.thirst.value), std::max(want.bladder.value, n.bladder.value), std::max(want.swim.value, n.swim.value) };
                                    feel.nausea.value = std::max(feel.nausea.value, r.get<Mood>(m).nausea.value);
                                    feel.tolerance.value = std::min(feel.tolerance.value, r.get<Mood>(m).tolerance.value);
                                    feel.wet.value = std::max(feel.wet.value, r.get<Mood>(m).wet.value);
                                    feel.heat.value = std::max(feel.heat.value, r.get<Mood>(m).heat.value);
                                }
                            }
                            // Not more to eat or drink while still at what they have.
                            if (munching(r, movers[i]))
                                want.hunger.value = want.thirst.value = 0;
                            auto goal = going_home(r, paths, movers[i], at, dice, night, weather.storm, cash_machine, world.tick);
                            if (!goal)
                                goal = choose(paths, at, on_offer, want, feel, wallet, dice, weather.rain && !r.all_of<Umbrella>(movers[i]), night && age_of(r, movers[i]) == AgeGroup::Teen ? kTuning.night.daring.value : 0, taste_of(r, movers[i]), weather.temperature.value);
                            // Nowhere to go: out of the rain under the nearest roof (in a storm even with an
                            // umbrella), or out of the sun into the nearest shade.
                            const bool rained_on = weather.rain && (!r.all_of<Umbrella>(movers[i]) || weather.storm);
                            const bool sweltering = sun && feel.heat.value >= kTuning.needs.want.value;
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
                                    stride.progress.value = 1;
                                }
                                else
                                {
                                    stride.to = stride.route.front();
                                    stride.route.erase(stride.route.begin());
                                }
                            }
                        }
                        // Staying under cover while it rains, or in the shade until cooled.
                        const bool refuge = (weather.rain && (!r.all_of<Umbrella>(movers[i]) || weather.storm) && paths.nodes()[at].covered) || (sun && mood.heat.value >= kTuning.needs.want.value / 2 && paths.nodes()[at].shaded);
                        if (errand.kind == ErrandKind::Wandering && !stride.route.empty())
                        {
                            stride.to = stride.route.front();
                            stride.route.erase(stride.route.begin());
                        }
                        else if (errand.kind == ErrandKind::Wandering && refuge)
                        {
                            stride.to = at;
                            stride.progress.value = 1;
                        }
                        else if (errand.kind == ErrandKind::Wandering && party != nullptr && errand.rest == ticks(kTuning.needs.idle.value) && random::chance(dice.state, kTuning.walking.huddle_chance.value))
                        {
                            party->huddle = ticks(static_cast<float>(random::between(dice.state, double { kTuning.walking.shortest_huddle.value }, double { kTuning.walking.longest_huddle.value })));
                            stride.to = at;
                            stride.progress.value = 1;
                        }
                        else if (errand.kind == ErrandKind::Wandering)
                            stride.to = wander(paths, at, came, dice, Walker::Guest);
                    }
                    if (stride.to == at)
                        stride.lane_to = stride.lane_from;
                    else if (!ringed)
                    {
                        stride.lane_to = new_lane(dice, stride.lane_from, party_slot(r, movers[i]));
                        // Turning on the next tile: across its inside corner, not out round its middle.
                        if (!stride.route.empty())
                        {
                            const glm::vec3 in = paths.centre(stride.to) - paths.centre(at);
                            const glm::vec3 out = paths.centre(stride.route.front()) - paths.centre(stride.to);
                            const glm::vec2 inside = glm::vec2(out.x, out.z) / std::max(glm::length(glm::vec2(out.x, out.z)), 0.001f) - glm::vec2(in.x, in.z) / std::max(glm::length(glm::vec2(in.x, in.z)), 0.001f);
                            if (glm::length(inside) > 0.5f)
                                stride.lane_to = glm::clamp(stride.lane_to + glm::normalize(inside) * kTuning.walking.corner_cut.value, -kLaneReach, kLaneReach);
                        }
                    }
                    walk_on(paths, stride, left);
                    place(paths, stride, pose);
                }
            });
        }

        phase.emplace("Followers");
        // Followers take their leader's step, each in their own place round the party's middle.
        {
            const auto view = r.view<Errand, Stride, Pose, const Member>();
            const auto followers = sorted(view);
            share(workers, followers.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [errand, stride, pose, member] = view.get(followers[i]);
                    if (errand.kind != ErrandKind::Following)
                        continue;
                    stride.fresh = false;
                    if (r.any_of<Gesture, Watching>(followers[i]))
                        continue; // Showing how they feel or watching; they catch up after
                    const Party& party = r.get<Party>(member.party);
                    const Stride& lead = r.get<Stride>(party.leader);
                    if (party.huddle > 0 && lead.from == lead.to && stride.to == lead.to && stride.progress.value >= 1)
                    {
                        // In a ring round a point just ahead of the leader, facing in, the leader standing on it.
                        const Pose& leader = r.get<Pose>(party.leader);
                        const glm::vec3 middle = paths.centre(lead.to);
                        glm::vec3 centre = leader.position + kTuning.walking.huddle_radius.value * glm::vec3(std::sin(leader.heading.value), 0.0f, std::cos(leader.heading.value));
                        centre.x = std::clamp(centre.x, middle.x - kLaneReach, middle.x + kLaneReach);
                        centre.z = std::clamp(centre.z, middle.z - kLaneReach, middle.z + kLaneReach);
                        const auto place_in_ring = static_cast<float>(std::ranges::find(party.members, followers[i]) - party.members.begin());
                        const auto leader_place = static_cast<float>(std::ranges::find(party.members, party.leader) - party.members.begin());
                        const float angle = std::atan2(leader.position.x - centre.x, leader.position.z - centre.z) + 2 * glm::pi<float>() * (place_in_ring - leader_place) / static_cast<float>(party.members.size());
                        // Stepping across to their place in the ring rather than jumping there.
                        const glm::vec3 spot = centre + kTuning.walking.huddle_radius.value * glm::vec3(std::sin(angle), 0.0f, std::cos(angle));
                        const glm::vec3 off = spot - pose.position;
                        const float reach = kTuning.walking.catch_up.value * stride.speed.value * dt;
                        pose.position = glm::length(off) <= reach ? spot : pose.position + off * (reach / glm::length(off));
                        pose.heading.value = angle + glm::pi<float>();
                        stride.gait = Gait::Walk;
                        stride.from = lead.to;
                        stride.to = lead.to;
                        stride.lane_from = stride.lane_to = glm::vec2(pose.position.x - middle.x, pose.position.z - middle.z);
                        continue;
                    }
                    // A tick's walk, hurrying to make up ground: on through tiles until it is spent or they are level
                    // with the leader, never passing them. A tile or more behind they run; else they keep the
                    // leader's step, running when the leader does.
                    const bool behind = stride.to != lead.to && stride.to != lead.from;
                    const units::Share energy = r.get<Mood>(followers[i]).energy;
                    stride.gait = behind || lead.gait == Gait::Run ? Gait::Run : Gait::Walk;
                    float left = (behind ? gait_pace(stride.speed, energy, Gait::Run).value : kTuning.walking.catch_up.value * stride.speed.value) * dt;
                    for (int tiles = 0; tiles < 3 && left > 0; ++tiles)
                    {
                        const bool in_step = stride.from == lead.from && stride.to == lead.to;
                        if (in_step || (stride.from != stride.to && stride.progress.value < 1))
                        {
                            // In step, closing in on the leader; else finishing their own step first.
                            const float until = in_step ? lead.progress.value : 1.0f;
                            if (stride.progress.value >= until)
                                break;
                            const float length = step_length(paths, stride);
                            const float gain = length > 0 ? std::min(left / length, until - stride.progress.value) : until - stride.progress.value;
                            stride.progress.value += gain;
                            left -= gain * length;
                            if (in_step)
                                break;
                            continue;
                        }
                        // At a tile, behind: the leader's step when it starts here, else the next tile towards them.
                        const NodeId at = stride.to;
                        stride.lane_from = stride.lane_to;
                        stride.from = at;
                        stride.progress.value = 0;
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
                            stride.progress.value = lead.progress.value;
                        }
                        // Towards their place round the party's middle, which the leader's lane gives.
                        if (stride.to != stride.from)
                            stride.lane_to = lane_towards(stride.lane_from, lead.lane_to - party_slot(r, party.leader) + party_slot(r, followers[i]));
                        if (stride.from == stride.to)
                            break;
                    }
                    place(paths, stride, pose);
                }
            });
        }

        phase.emplace("Spacing");
        // People walking the paths near one another step apart, a little at a time, and keep to the right so two
        // meeting head on go round each other. Only those in the same or a neighbouring cell of a grid as fine as
        // the space each keeps are compared. Those sitting, watching, showing how they feel or standing with their
        // party stand still. The grid is a checkerboard: each tick only those in cells of one colour step, twice as
        // far, so half the searching is done for the same push (one crossing into the other colour may step twice running or
        // miss two, which evens out). Each reads the others where they stood before
        // anyone stepped and moves only themselves, so any split of the people can step at once.
        if (!paths.nodes().empty())
        {
            const float space = kTuning.walking.personal_space.value;
            auto& bodies = world.buffers.bodies;
            auto& steps = world.buffers.steps;
            // Cells of one column in a row of keys, so a cell and those above and below it are one run.
            const auto cell_key = [](int col, int row) { return std::int64_t { col } * (std::int64_t { 1 } << 32) + row; };
            bodies.clear();
            for (const auto [e, errand, stride, pose] : r.view<const Errand, const Stride, const Pose>().each())
            {
                const bool walking = errand.kind == ErrandKind::Wandering || errand.kind == ErrandKind::Heading || errand.kind == ErrandKind::Following || errand.kind == ErrandKind::Waiting;
                if (!walking || stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                    continue;
                const glm::ivec2 cell { glm::floor(glm::vec2(pose.position.x, pose.position.z) / space) };
                bodies.push_back({ .cell = cell_key(cell.x, cell.y), .at = pose.position, .who = e });
            }
            std::ranges::sort(bodies, [](const auto& a, const auto& b) { return std::tie(a.cell, a.who) < std::tie(b.cell, b.who); });
            steps.assign(bodies.size(), glm::vec2 { 0.0f });
            const float most = 2 * kTuning.walking.step_aside.value * dt; // Every second tick
            const int colour = static_cast<int>(world.tick % 2);
            share(workers, bodies.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    const auto& me = bodies[i];
                    const glm::ivec2 cell { glm::floor(glm::vec2(me.at.x, me.at.z) / space) };
                    if (((cell.x + cell.y) & 1) != colour)
                        continue;
                    glm::vec2 away { 0.0f };
                    for (int dc = -1; dc <= 1; ++dc)
                    {
                        const std::int64_t last = cell_key(cell.x + dc, cell.y + 1);
                        for (auto it = std::ranges::lower_bound(bodies, cell_key(cell.x + dc, cell.y - 1), { }, &StepBuffers::Body::cell); it != bodies.end() && it->cell <= last; ++it)
                        {
                            if (it->who == me.who || std::abs(it->at.y - me.at.y) > 1)
                                continue; // Themselves, or on a path above or below
                            const glm::vec2 off { me.at.x - it->at.x, me.at.z - it->at.z };
                            if (glm::dot(off, off) >= space * space)
                                continue;
                            const float apart = glm::length(off);
                            // Standing in one spot: apart by who they are, each one way.
                            const glm::vec2 way = apart > 0.001f ? off / apart : glm::vec2 { me.who < it->who ? 1.0f : -1.0f, 0.0f };
                            away += (space - apart) / space * (way + static_cast<float>(kTuning.walking.keep_right.value) * glm::vec2 { -way.y, way.x });
                        }
                    }
                    const float length = glm::length(away) * most;
                    if (length <= 0)
                        continue;
                    const auto* member = r.try_get<Member>(me.who);
                    const bool ringed = r.get<Errand>(me.who).kind == ErrandKind::Waiting && r.get<Stride>(me.who).route.empty();
                    if ((member != nullptr && r.get<Party>(member->party).huddle > 0) || ringed || r.any_of<Linger, Gesture, Watching>(me.who))
                        continue; // Standing still, in the others' way
                    // Only aside, never back against where they face nor turning them: as much as their walk allows.
                    const Stride& stride = r.get<Stride>(me.who);
                    if (stride.from == stride.to)
                        continue;
                    const float facing = r.get<Pose>(me.who).heading.value;
                    const glm::vec2 side { std::cos(facing), -std::sin(facing) };
                    const float room = std::min(most, kTuning.walking.swerve * (r.all_of<Mood>(me.who) ? gait_pace(stride.speed, r.get<Mood>(me.who).energy, stride.gait) : stride.speed).value * dt);
                    steps[i] = side * std::clamp(glm::dot(away / glm::length(away), side) * std::min(length, most), -room, room);
                }
            });
            share(workers, bodies.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    if (steps[i] == glm::vec2 { 0.0f })
                        continue;
                    // The lanes move with them, so the step lasts, but never off the path.
                    auto& stride = r.get<Stride>(bodies[i].who);
                    stride.lane_from = glm::clamp(stride.lane_from + steps[i], -kLaneReach, kLaneReach);
                    stride.lane_to = glm::clamp(stride.lane_to + steps[i], -kLaneReach, kLaneReach);
                    place(paths, stride, r.get<Pose>(bodies[i].who));
                }
            });
        }

        for (const auto [e, stride, pose] : r.view<const Stride, Pose>().each())
        {
            const auto* errand = r.try_get<Errand>(e);
            if (errand != nullptr ? errand->kind == ErrandKind::Wandering || errand->kind == ErrandKind::Heading || errand->kind == ErrandKind::Following || errand->kind == ErrandKind::Waiting || errand->kind == ErrandKind::Queueing : r.all_of<Staff>(e))
                face_moved(pose, dt);
        }

        phase.emplace("Arrivals");
        // 3. Arrivals, in entity order. A leader's followers arrive with them.
        // Whether `guest` bought something at stall `at`, from their own cash: a party pays together only for rides.
        const auto buy = [&](entt::entity guest, entt::entity at, const Shop& shop, bool chose) {
            auto& attraction = r.get<Attraction>(at);
            auto& wallet = r.get<Wallet>(guest);
            auto& mood = r.get<Mood>(guest);
            auto& needs = r.get<Needs>(guest);
            float* need = shop.product == Product::Drink ? &needs.thirst.value : shop.product == Product::Food ? &needs.hunger.value : shop.product == Product::Toilet ? &needs.bladder.value : shop.product == Product::FirstAid ? &mood.nausea.value : nullptr;
            const bool umbrella = shop.product == Product::Umbrella;
            const bool meal = shop.product == Product::Food || shop.product == Product::Drink;
            if (meal && munching(r, guest))
            {
                think(r, guest, ThoughtKind::HaventFinished, at, now);
                return false;
            }
            const bool cools = shop.serve == Serve::Cold && mood.heat.value >= kTuning.needs.want.value;
            if (!chose && !cools && (umbrella ? !weather.rain || r.all_of<Umbrella>(guest) : need != nullptr ? *need < kTuning.needs.want.value : wallet.cash.value >= kTuning.facilities.low_cash.value && !wallet.needs_cash))
            {
                // Came along without wanting anything.
                if (meal)
                    think(r, guest, shop.product == Product::Food ? ThoughtKind::NotHungry : ThoughtKind::NotThirsty, at, now);
                else if (umbrella && r.all_of<Umbrella>(guest))
                    think(r, guest, ThoughtKind::AlreadyGot, at, now);
                return false;
            }
            // Food or drink they pick off the menu: one of its kind they can afford at no more than it is worth to
            // them, any of those; a stall with no menu sells one thing at its price. What a thing is worth is its
            // item's for the weather, more in the cold for a hot drink and in the heat for an ice cream; where that
            // is not known, the same for every food or drink.
            const units::Money flat_worth = shop.product == Product::Food ? kTuning.eating.food_worth : kTuning.eating.drink_worth;
            const auto worth = [&](const Ware& w) { return worth_of(w, flat_worth, weather.temperature.value); };
            std::size_t ware = shop.menu.size();
            units::Money price = attraction.price;
            if (meal && !shop.menu.empty())
            {
                std::size_t fair = 0, affordable = 0;
                for (const Ware& w : shop.menu)
                    if (w.product == shop.product && w.price.value <= wallet.cash.value)
                        ++affordable, fair += w.price.value <= worth(w).value ? 1 : 0;
                if (fair > 0)
                    for (std::size_t pick = random::below(r.get<Dice>(guest).state, static_cast<std::uint32_t>(fair)), k = 0; k < shop.menu.size(); ++k)
                    {
                        const Ware& w = shop.menu[k];
                        if (w.product == shop.product && w.price.value <= wallet.cash.value && w.price.value <= worth(w).value && pick-- == 0)
                        {
                            ware = k;
                            price = w.price;
                            break;
                        }
                    }
                else if (affordable > 0 && attraction.open)
                {
                    mood.happiness.value = clamp01(mood.happiness.value - kTuning.needs.treat.value);
                    think(r, guest, ThoughtKind::NotPaying, at, now);
                    return false;
                }
                else
                    price = std::ranges::min(shop.menu, { }, [](const Ware& w) { return w.price.value; }).price;
            }
            else if (meal && price.value > flat_worth.value && price.value <= wallet.cash.value && attraction.open)
            {
                mood.happiness.value = clamp01(mood.happiness.value - kTuning.needs.treat.value);
                think(r, guest, ThoughtKind::NotPaying, at, now);
                return false;
            }
            else if (shop.product == Product::Toilet && static_cast<float>(price.value) > static_cast<float>(kTuning.facilities.toilet_worth.value) * needs.bladder.value && attraction.open)
            {
                // Worth more the more they need it.
                mood.happiness.value = clamp01(mood.happiness.value - kTuning.needs.treat.value);
                think(r, guest, ThoughtKind::NotPayingToilet, at, now);
                return false;
            }
            units::Money Wallet::* const on = shop.product == Product::Drink ? &Wallet::spent_drink : shop.product == Product::Food ? &Wallet::spent_food : umbrella ? &Wallet::spent_souvenirs : nullptr;
            if (!attraction.open || wallet.cash.value < price.value || !pay(r, guest, price, on, meal ? park::LedgerLine::FoodDrinkSales : park::LedgerLine::ShopSales))
            {
                mood.happiness.value = clamp01(mood.happiness.value - kTuning.needs.treat.value);
                if (attraction.open)
                {
                    think(r, guest, ThoughtKind::CantAfford, at, now);
                    wallet.needs_cash = true;
                }
                return false;
            }
            if (meal && price.value <= kTuning.eating.good_value.value * static_cast<float>((ware < shop.menu.size() ? worth(shop.menu[ware]) : flat_worth).value))
                think(r, guest, ThoughtKind::GoodValue, at, now);
            attraction.income.value += price.value;
            ++attraction.customers;
            // The park pays for the stock each one sold took, its extras with it.
            if (ware < shop.menu.size())
                r.ctx().emplace<Supplies>().paid[std::to_underlying(meal ? park::LedgerLine::FoodDrinkStock : park::LedgerLine::ShopStock)].value += shop.menu[ware].cost.value;
            if (meal)
            {
                // Eaten or drunk a bite at a time from now on, in the hand: an empty still in it goes in a pocket,
                // or on the path when both are full.
                std::uint8_t pocketed = 0;
                if (auto* old = r.try_get<Carried>(guest))
                {
                    if (old->held)
                        free_hand(r, guest, *old, r.get<Stride>(guest).to);
                    pocketed = r.all_of<Carried>(guest) ? old->pocketed : 0;
                }
                r.emplace_or_replace<Carried>(guest, Carried { .stall = at, .ware = static_cast<std::uint8_t>(ware), .kind = std::uint8_t { shop.product == Product::Drink }, .food = shop.product == Product::Food, .bites = kTuning.eating.bites, .fill = { *need / kTuning.eating.bites }, .pocketed = pocketed });
                if (ware < shop.menu.size())
                {
                    // Its extras feed, quench or upset them the more (or, as salt does, leave them thirstier), and
                    // now and then they remark on getting none of them or all.
                    const Ware& w = shop.menu[ware];
                    needs.hunger.value = clamp01(needs.hunger.value - w.topping[0]);
                    needs.thirst.value = clamp01(needs.thirst.value - w.topping[1]);
                    mood.nausea.value = clamp01(mood.nausea.value + w.topping[2]);
                    if (w.extras >= kTuning.eating.toppings_noticed && (w.topped == 0 || w.topped == w.extras) && random::chance(r.get<Dice>(guest).state, kTuning.eating.topping_thought.value))
                    {
                        const bool none = w.topped == 0;
                        mood.happiness.value = clamp01(mood.happiness.value + (none ? -kTuning.needs.treat.value : kTuning.needs.treat.value));
                        think(r, guest, none ? ThoughtKind::StingyToppings : ThoughtKind::MassesOfToppings, at, now);
                    }
                }
            }
            else if (need != nullptr)
                *need = 0;
            else if (umbrella)
                r.emplace_or_replace<Umbrella>(guest);
            else
            {
                // As much as they draw, or what their bank has left.
                const std::int64_t drawn = std::clamp(wallet.bank.value, std::int64_t { 0 }, kTuning.facilities.withdraw.value);
                wallet.cash.value += drawn;
                wallet.bank.value -= drawn;
                wallet.withdrawn.value += drawn;
                wallet.needs_cash = false;
            }
            mood.happiness.value = clamp01(mood.happiness.value + kTuning.needs.treat.value);
            // Something cold on a hot day or hot on a cold one pleases the more, and something cold cools.
            if ((shop.serve == Serve::Cold && (weather.temperature.value >= kTuning.weather.hot.value || mood.heat.value >= kTuning.needs.want.value)) || (shop.serve == Serve::Hot && weather.temperature.value < kTuning.weather.cold.value))
                mood.happiness.value = clamp01(mood.happiness.value + kTuning.needs.treat.value);
            if (shop.serve == Serve::Cold)
                mood.heat.value = 0;
            return true;
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
            // Come from a spawn point to the gate: in, paying, or, at a closed gate or short of the fee, back the way
            // they came. With no way back they go home there, as those leaving do.
            if (const auto* outside = r.try_get<Outside>(guest); outside != nullptr && outside->way == Outside::Way::In && r.all_of<ParkGate>(arrived[i]))
            {
                std::vector<entt::entity> party { guest };
                party.append_range(followers);
                const bool shut = r.get<ParkGate>(arrived[i]).closed;
                if (!shut && pay_entry(r, party))
                {
                    for (const entt::entity m : party)
                        r.remove<Outside>(m);
                    errand = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                    continue;
                }
                for (const entt::entity m : party)
                {
                    r.get<Outside>(m).way = Outside::Way::TurnedAway;
                    if (shut) // Not counted in, as those who cannot pay are not
                        --r.ctx().get<Turnstile>().in[std::to_underlying(r.get<Visitor>(m).age)];
                }
                if (auto off = way_off(r, paths, r.get<ParkGate>(arrived[i]).node))
                {
                    errand = { ErrandKind::Heading, off->first, 0 };
                    r.get<Stride>(guest).route = std::move(off->second);
                    continue;
                }
            }
            if (auto* shop = r.try_get<Shop>(arrived[i]))
            {
                errand = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                // Those who bought stand at the counter a moment, facing the stall, and pay; at a toilet or first
                // aid the longer the more they needed it. The rest of a party waits nearby (5.).
                const auto need = [&](entt::entity m) { return r.all_of<Staff>(m) ? 0.0f : shop->product == Product::Toilet ? r.get<Needs>(m).bladder.value : shop->product == Product::FirstAid ? r.get<Mood>(m).nausea.value : 0.0f; };
                const auto served = [&](entt::entity m, float needed) {
                    const units::Seconds turn { std::max(kTuning.facilities.served.value, needed * kTuning.facilities.relief.value) };
                    r.get<Errand>(m) = { .kind = ErrandKind::Buying, .target = arrived[i], .rest = ticks(turn.value), .turn = turn };
                    auto& stride = r.get<Stride>(m);
                    const NodeId at = r.get<Attraction>(arrived[i]).entrance;
                    stride.route.clear();
                    stride.from = at;
                    stride.to = at;
                    stride.progress.value = 1;
                    stride.lane_from = stride.lane_to = counter_lane(*shop, m);
                    auto& pose = r.get<Pose>(m);
                    if (glm::length(shop->counter) > 0)
                        pose.heading.value = std::atan2(shop->counter.x, shop->counter.y);
                    place(paths, stride, pose);
                };
                // An inspector only stands at the counter a while; they judged its prices on the way.
                if (const float needed = need(guest); r.all_of<Staff>(guest) || buy(guest, arrived[i], *shop, !r.all_of<Member>(guest)))
                    served(guest, needed);
                for (const entt::entity m : followers)
                    if (const float needed = need(m); buy(m, arrived[i], *shop, false))
                        served(m, needed);
            }
            else if (r.any_of<ParkGate, GuestSpawn>(arrived[i]))
            {
                if (const auto* member = r.try_get<Member>(guest))
                {
                    // Members not walking out with them (busy, or just back from a fall) stay on alone.
                    for (const entt::entity m : r.get<Party>(member->party).members)
                        if (m != guest && !std::ranges::contains(followers, m))
                            r.remove<Member>(m);
                    r.destroy(member->party);
                }
                for (const entt::entity m : followers)
                    leave(r, m);
                leave(r, guest);
            }
            else if (r.all_of<PoolDoor>(arrived[i]))
            {
                // Each pays at the door and goes in to change, at no more than the complex is worth to them, its
                // slides too; those of the party who would rather not wait (5.).
                const entt::entity pool = r.get<PoolDoor>(arrived[i]).pool;
                const auto slides = static_cast<std::size_t>(std::ranges::count_if(r.view<const Trains>(), [&](entt::entity e) { return pool_ride_pool(world, e) == pool; }));
                // And lazy rivers with a station joined to it.
                const auto rivers = static_cast<std::size_t>(std::ranges::count_if(r.view<const River>(), [&](entt::entity e) { return std::ranges::any_of(r.get<River>(e).stations, [&](const River::Station& s) { return s.pool == pool; }); }));
                const auto* swim_pool = r.valid(pool) ? r.try_get<SwimPool>(pool) : nullptr;
                const std::int64_t worth = swim_pool != nullptr ? rct3::park::pools::pool_worth(swim_pool->tally, slides, rivers, swim_pool->dirt.value) : 0;
                const auto swim = [&](entt::entity g) {
                    auto& attraction = r.get<Attraction>(arrived[i]);
                    const bool inspector = r.all_of<Staff>(g);
                    // Those who set out for it together may find it full.
                    if (pool_full(r, pool, false))
                    {
                        r.get<Errand>(g) = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                        return;
                    }
                    if (attraction.open && !inspector && attraction.price.value > worth && attraction.price.value <= r.get<Wallet>(g).cash.value)
                    {
                        auto& happiness = r.get<Mood>(g).happiness.value;
                        happiness = clamp01(happiness - kTuning.needs.treat.value);
                        think(r, g, ThoughtKind::NotPaying, arrived[i], now);
                        r.get<Errand>(g) = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                        return;
                    }
                    if (!attraction.open || (!inspector && !pay(r, g, attraction.price, &Wallet::spent_rides, park::LedgerLine::PoolTickets)))
                    {
                        if (attraction.open)
                        {
                            think(r, g, ThoughtKind::CantAfford, arrived[i], now);
                            r.get<Wallet>(g).needs_cash = true;
                        }
                        r.get<Errand>(g) = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                        return;
                    }
                    if (!inspector)
                    {
                        attraction.income.value += attraction.price.value;
                        ++attraction.customers;
                    }
                    r.get<Errand>(g) = { .kind = ErrandKind::Swimming, .target = arrived[i] };
                    r.emplace_or_replace<Swimmer>(g, Swimmer { .changing = kTuning.pools.changing, .left = kTuning.pools.stay });
                };
                swim(guest);
                for (const entt::entity m : followers)
                    if (r.get<Needs>(m).swim.value >= kTuning.needs.want.value / 2 && random::chance(r.get<Dice>(m).state, kTuning.walking.join_chance.value))
                        swim(m);
            }
            else if (auto* ride = r.try_get<CycleRide>(arrived[i]))
            {
                // At whichever of its stations they came to; with more than one, off at one of the others, alike
                // (a party all at the same).
                const NodeId at = r.get<Stride>(guest).to;
                std::size_t station = 0;
                while (station < ride->stops.size() && ride->stops[station].entrance != at)
                    ++station;
                station = station < ride->stops.size() ? station + 1 : 0;
                auto& queue = station == 0 ? ride->queue : ride->stops[station - 1].queue;
                const auto& line = station == 0 ? ride->line : ride->stops[station - 1].line;
                std::vector<std::size_t> offs;
                for (std::size_t k = 0; k <= ride->stops.size(); ++k)
                    if (k != station && (k == 0 ? exit_of(r, arrived[i], 0) != kNoNode : ride->stops[k - 1].exit != kNoNode))
                        offs.push_back(k);
                auto off = static_cast<std::uint8_t>(offs.empty() ? station : offs.size() == 1 ? offs[0] : offs[random::below(r.get<Dice>(guest).state, offs.size())]);
                // Riding across the park: off where their way goes on.
                if (!crossing[i].empty())
                {
                    for (std::size_t k = 0; k <= ride->stops.size(); ++k)
                        if (k != station && exit_of(r, arrived[i], k) == crossing[i].front())
                            off = static_cast<std::uint8_t>(k);
                    crossing[i].erase(crossing[i].begin());
                    r.emplace_or_replace<Crossing>(guest, Crossing { .ride = arrived[i], .errand = errand, .route = std::move(crossing[i]) });
                }
                else
                    r.remove<Crossing>(guest);
                // A full line puts them off: they think so and go elsewhere, and the party with them.
                if (queue.size() >= queue_room(line))
                {
                    think(r, guest, ThoughtKind::QueueTooLong, arrived[i], now);
                    errand = { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                    continue;
                }
                // In at the far end of the line, with those of the party who want to come; the rest wait (phase 5).
                // Each stands to the other side of the line's middle from the guest ahead.
                const auto join = [&](entt::entity g) {
                    const float ahead = queue.empty() || !r.valid(queue.back()) ? 0 : r.get<Stride>(queue.back()).side.value;
                    const float side = side_after(r.get<Dice>(g).state, ahead);
                    queue.push_back(g);
                    r.get<Errand>(g) = { .kind = ErrandKind::Queueing, .target = arrived[i], .station = static_cast<std::uint8_t>(station), .off = off };
                    auto& stride = r.get<Stride>(g);
                    stride.along.value = line.empty() ? 0 : static_cast<float>(line.size() - 1);
                    stride.side.value = side;
                };
                join(guest);
                for (const entt::entity m : followers)
                    if (r.get<Mood>(m).nausea.value < kTuning.needs.max_nausea.value && random::chance(r.get<Dice>(m).state, kTuning.walking.join_chance.value))
                        join(m);
            }
        }
    }

    void step_gates(SimWorld& world, const Weather& weather)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
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

    units::Pace gait_pace(units::Pace walk, units::Share energy, Gait gait)
    {
        const auto& gaits = kTuning.gaits;
        const double by_gait = gait == Gait::Run ? gaits.run_pace.value : gait == Gait::Weary ? gaits.weary_pace.value : gait == Gait::Queasy ? gaits.queasy_pace.value : gait == Gait::Bursting ? gaits.bursting_pace.value : 1.0;
        const double by_energy = gaits.drained.value + (gaits.fresh.value - gaits.drained.value) * std::clamp(energy.value, 0.0f, 1.0f);
        return { static_cast<float>(walk.value * by_energy * by_gait) };
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
            const auto around = [&](units::Money average, units::Money fallback) { return units::Money { std::llround(static_cast<double>(average.value > 0 ? average.value : fallback.value) * (1 + random::between(dice, -1.0, 1.0) * kTuning.arrivals.cash_spread.value)) }; };
            const units::Money cash = around(draw != nullptr ? draw->guest_cash : units::Money { }, kTuning.arrivals.cash);
            r.emplace<Wallet>(guest, cash, around(draw != nullptr ? draw->guest_bank : units::Money { }, kTuning.arrivals.bank));
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

} // namespace rct3::simulation
