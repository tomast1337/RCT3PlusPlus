// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/systems.hpp"

#include "simulation/components.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace rct3::simulation
{

    namespace
    {
        // Our own first guesses, to be tuned against how parks play.
        constexpr float kThirstPerSecond = 1.0f / 480; ///< From satisfied to desperate in eight minutes
        constexpr float kHungerPerSecond = 1.0f / 720;
        constexpr float kBladderPerSecond = 1.0f / 900;
        constexpr float kEnergyPerSecond = 1.0f / 3600; ///< Lost while walking about
        constexpr float kGloomPerSecond = 1.0f / 600;   ///< Happiness lost while a need is pressing
        constexpr float kPressing = 0.7f;               ///< A need this high spoils the guest's mood
        constexpr float kWant = 0.55f;                  ///< A need this high sends the guest to a shop
        constexpr double kRideChance = 0.25;            ///< Of a guest with no pressing need going for a ride
        constexpr float kMaxNausea = 0.6f;              ///< Guests feeling sicker skip rides
        constexpr float kIdleSeconds = 5;               ///< Between thoughts while wandering
        constexpr float kConsumeSeconds = 10;           ///< Spent with what was bought before moving on
        constexpr float kTreat = 0.05f;                 ///< Happiness from buying what was wanted

        float clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        /// The entities of `view`, in ascending order, so serial phases go the same way every time.
        template<typename View> std::vector<entt::entity> sorted(const View& view)
        {
            std::vector<entt::entity> out(view.begin(), view.end());
            std::ranges::sort(out);
            return out;
        }

        void share(Workers* workers, std::size_t count, const std::function<void(std::size_t, std::size_t)>& body)
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
                    out.push_back({ e, AttractionKind::Shop, shop->product, a.entrance, a.price, 0 });
                else if (const auto* ride = r.try_get<FlatRide>(e))
                    out.push_back({ e, AttractionKind::FlatRide, Product::Drink, a.entrance, a.price, ride->intensity });
            }
            return out;
        }

        /// The nearest offer of `kind` (and `product`, for shops) the guest can pay for, with the walk there.
        std::optional<std::pair<entt::entity, std::vector<NodeId>>> nearest_offer(const PathNetwork& paths, NodeId at, const std::vector<Offer>& offers, AttractionKind kind, Product product, const Wallet& wallet)
        {
            // shortcut: one search per offer, upgrade to a single search that stops at the first entrance reached
            // when parks with many shops show up in profiles.
            std::optional<std::pair<entt::entity, std::vector<NodeId>>> best;
            for (const Offer& o : offers)
            {
                if (o.kind != kind || (kind == AttractionKind::Shop && o.product != product) || o.price > wallet.cash)
                    continue;
                auto walk = paths.route(at, o.entrance);
                if (walk && (!best || walk->size() < best->second.size()))
                    best.emplace(o.entity, std::move(*walk));
            }
            return best;
        }

        /// Where the guest goes next: a shop for the most pressing need there is one for, else now and then a
        /// flat ride.
        std::optional<std::pair<entt::entity, std::vector<NodeId>>> choose(const PathNetwork& paths, NodeId at, const std::vector<Offer>& offers, const Needs& needs, const Mood& mood, const Wallet& wallet, Dice& dice)
        {
            const bool thirst_first = needs.thirst >= needs.hunger;
            for (const auto& [need, product] : { std::pair { thirst_first ? needs.thirst : needs.hunger, thirst_first ? Product::Drink : Product::Food }, std::pair { thirst_first ? needs.hunger : needs.thirst, thirst_first ? Product::Food : Product::Drink } })
            {
                if (need < kWant)
                    break;
                if (auto found = nearest_offer(paths, at, offers, AttractionKind::Shop, product, wallet))
                    return found;
            }
            if (mood.nausea < kMaxNausea && random::chance(dice.state, kRideChance))
                return nearest_offer(paths, at, offers, AttractionKind::FlatRide, Product::Drink, wallet);
            return std::nullopt;
        }

        /// The next tile for a wandering guest at `at`, not back where they came from unless it is the only way.
        NodeId wander(const PathNetwork& paths, NodeId at, NodeId came, Dice& dice)
        {
            std::array<NodeId, 4> ways { };
            std::size_t count = 0;
            for (const NodeId n : paths.nodes()[at].next)
                if (n != kNoNode && n != came && !paths.nodes()[n].queue)
                    ways[count++] = n;
            if (count == 0)
                return came != kNoNode ? came : at;
            return ways[random::below(dice.state, count)];
        }

        void place(const PathNetwork& paths, const Stride& s, Pose& pose)
        {
            const glm::vec3 a = paths.centre(s.from);
            const glm::vec3 b = paths.centre(s.to);
            pose.position = glm::mix(a, b, s.progress);
            if (a != b)
                pose.heading = std::atan2(b.x - a.x, b.z - a.z);
        }

        void stop_at(const PathNetwork& paths, NodeId node, Stride& stride, Pose& pose)
        {
            stride.from = node;
            stride.to = node;
            stride.progress = 1;
            stride.route.clear();
            place(paths, stride, pose);
        }
    } // namespace

    std::uint32_t ticks(float seconds)
    {
        return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0f) / kTickSeconds));
    }

    void start_walking(SimWorld& world, entt::entity guest, NodeId at)
    {
        auto& r = world.registry;
        r.emplace_or_replace<Errand>(guest);
        r.emplace_or_replace<Stride>(guest, Stride { .from = at, .to = at, .progress = 1 });
        std::uint64_t mix = world.seed ^ (std::uint64_t { entt::to_integral(guest) } << 32);
        r.emplace_or_replace<Dice>(guest, random::seeded(random::split_mix(mix)));
        place(world.paths, r.get<Stride>(guest), r.get_or_emplace<Pose>(guest));
    }

    void step(SimWorld& world, Workers* workers)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;

        // 1. Needs and moods.
        {
            const auto view = r.view<Needs, Mood>();
            const auto guests = sorted(view);
            share(workers, guests.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [needs, mood] = view.get<Needs, Mood>(guests[i]);
                    needs.thirst = clamp01(needs.thirst + kThirstPerSecond * dt);
                    needs.hunger = clamp01(needs.hunger + kHungerPerSecond * dt);
                    needs.bladder = clamp01(needs.bladder + kBladderPerSecond * dt);
                    mood.energy = clamp01(mood.energy - kEnergyPerSecond * dt);
                    if (std::max({ needs.thirst, needs.hunger, needs.bladder }) >= kPressing)
                        mood.happiness = clamp01(mood.happiness - kGloomPerSecond * dt);
                }
            });
        }

        // 2. Walking and deciding. Arrivals are noted per guest and dealt with after.
        const std::vector<Offer> on_offer = offers(r);
        const auto movers_view = r.view<Errand, Stride, Pose, Dice, const Needs, const Mood, const Wallet>();
        const auto movers = sorted(movers_view);
        std::vector<entt::entity> arrived(movers.size(), entt::null);
        if (!paths.nodes().empty())
        {
            share(workers, movers.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto [errand, stride, pose, dice, needs, mood, wallet] = movers_view.get(movers[i]);
                    if (errand.kind != ErrandKind::Wandering && errand.kind != ErrandKind::Heading)
                        continue;
                    if (stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                        continue; // Off the paths; nothing to walk on
                    if (errand.rest > 0)
                        --errand.rest;

                    if (stride.from != stride.to)
                    {
                        const float length = glm::distance(paths.centre(stride.from), paths.centre(stride.to));
                        stride.progress += length > 0 ? stride.speed * dt / length : 1;
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
                    if (!stride.route.empty())
                    {
                        stride.to = stride.route.front();
                        stride.route.erase(stride.route.begin());
                    }
                    else if (errand.kind == ErrandKind::Heading)
                    {
                        arrived[i] = errand.target;
                        stride.to = at;
                        stride.progress = 1;
                    }
                    else
                    {
                        if (errand.rest == 0)
                        {
                            errand.rest = ticks(kIdleSeconds);
                            if (auto goal = choose(paths, at, on_offer, needs, mood, wallet, dice))
                            {
                                errand = { ErrandKind::Heading, goal->first, 0 };
                                stride.route = std::move(goal->second);
                                if (stride.route.empty())
                                {
                                    arrived[i] = errand.target;
                                    stride.to = at;
                                    stride.progress = 1;
                                    place(paths, stride, pose);
                                    continue;
                                }
                                stride.to = stride.route.front();
                                stride.route.erase(stride.route.begin());
                                place(paths, stride, pose);
                                continue;
                            }
                        }
                        stride.to = wander(paths, at, came, dice);
                    }
                    place(paths, stride, pose);
                }
            });
        }

        // 3. Arrivals, in entity order.
        for (std::size_t i = 0; i < movers.size(); ++i)
        {
            if (arrived[i] == entt::null || !r.valid(arrived[i]))
                continue;
            const entt::entity guest = movers[i];
            auto& errand = r.get<Errand>(guest);
            auto& attraction = r.get<Attraction>(arrived[i]);
            auto& wallet = r.get<Wallet>(guest);
            if (auto* shop = r.try_get<Shop>(arrived[i]))
            {
                errand = { ErrandKind::Wandering, entt::null, ticks(kConsumeSeconds) };
                auto& mood = r.get<Mood>(guest);
                if (!attraction.open || wallet.cash < attraction.price)
                {
                    mood.happiness = clamp01(mood.happiness - kTreat);
                    continue;
                }
                wallet.cash -= attraction.price;
                attraction.income += attraction.price;
                ++attraction.customers;
                auto& needs = r.get<Needs>(guest);
                (shop->product == Product::Drink ? needs.thirst : needs.hunger) = 0;
                mood.happiness = clamp01(mood.happiness + kTreat);
            }
            else if (auto* ride = r.try_get<FlatRide>(arrived[i]))
            {
                ride->queue.push_back(guest);
                errand = { ErrandKind::Queueing, arrived[i], 0 };
            }
        }

        // 4. Flat rides, in entity order.
        for (const entt::entity e : sorted(r.view<FlatRide>()))
        {
            auto& ride = r.get<FlatRide>(e);
            auto& attraction = r.get<Attraction>(e);
            if (ride.timer > 0 && --ride.timer == 0)
            {
                for (const entt::entity guest : ride.riders)
                {
                    if (!r.valid(guest))
                        continue;
                    auto& mood = r.get<Mood>(guest);
                    mood.happiness = clamp01(mood.happiness + ride.excitement * 0.3f);
                    mood.nausea = clamp01(mood.nausea + ride.intensity * 0.2f);
                    r.get<Errand>(guest) = { ErrandKind::Wandering, entt::null, ticks(kIdleSeconds) };
                    stop_at(paths, attraction.entrance, r.get<Stride>(guest), r.get<Pose>(guest));
                }
                ride.riders.clear();
            }
            if (ride.timer == 0 && !ride.queue.empty())
            {
                const std::size_t boarding = std::min<std::size_t>(ride.capacity, ride.queue.size());
                for (std::size_t i = 0; i < boarding; ++i)
                {
                    const entt::entity guest = ride.queue[i];
                    if (!r.valid(guest))
                        continue;
                    auto& wallet = r.get<Wallet>(guest);
                    if (!attraction.open || wallet.cash < attraction.price)
                    {
                        r.get<Errand>(guest) = { ErrandKind::Wandering, entt::null, ticks(kIdleSeconds) };
                        continue;
                    }
                    wallet.cash -= attraction.price;
                    attraction.income += attraction.price;
                    ++attraction.customers;
                    r.get<Errand>(guest).kind = ErrandKind::Riding;
                    ride.riders.push_back(guest);
                }
                ride.queue.erase(ride.queue.begin(), ride.queue.begin() + static_cast<std::ptrdiff_t>(boarding));
                if (!ride.riders.empty())
                    ride.timer = std::max<std::uint32_t>(ride.cycle_ticks, 1);
            }
        }
        ++world.tick;
    }

} // namespace rct3::simulation
