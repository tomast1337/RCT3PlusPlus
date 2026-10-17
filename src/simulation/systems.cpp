// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/systems.hpp"

#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
        constexpr float kQueueSpacing = 0.25f;          ///< Tiles between guests standing in a queue
        constexpr double kJoinChance = 0.6;             ///< Of a party member going on the ride their leader picked
        constexpr double kHuddleChance = 0.08;          ///< Of a wandering party stopping to stand together, each thought
        constexpr float kShortestHuddle = 8;            ///< Seconds
        constexpr float kLongestHuddle = 20;
        constexpr float kHuddleRadius = 0.7f; ///< Of the ring a party stands in, world units
        constexpr float kCatchUp = 1.5f;      ///< How much faster than their walk a follower makes up ground

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
                else if (const auto* ride = r.try_get<CycleRide>(e))
                    out.push_back({ e, AttractionKind::Ride, Product::Drink, a.entrance, a.price, ride->intensity });
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
                return nearest_offer(paths, at, offers, AttractionKind::Ride, Product::Drink, wallet);
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

        glm::vec3 lane_point(const PathNetwork& paths, NodeId node, glm::vec2 lane)
        {
            return paths.centre(node) + glm::vec3(lane.x, 0.0f, lane.y);
        }

        glm::vec2 new_lane(Dice& dice)
        {
            return { static_cast<float>(random::between(dice.state, -kLaneReach, kLaneReach)), static_cast<float>(random::between(dice.state, -kLaneReach, kLaneReach)) };
        }

        void place(const PathNetwork& paths, const Stride& s, Pose& pose)
        {
            const glm::vec3 a = lane_point(paths, s.from, s.lane_from);
            const glm::vec3 b = lane_point(paths, s.to, s.lane_to);
            pose.position = glm::mix(a, b, s.progress);
            if (glm::distance(a, b) > 0.01f)
                pose.heading = std::atan2(b.x - a.x, b.z - a.z);
        }

        /// What a guest does once let go by a ride: a party member waits for the rest, others wander on.
        Errand after_ride(const entt::registry& r, entt::entity guest, entt::entity ride)
        {
            if (r.all_of<Member>(guest))
                return { ErrandKind::Waiting, ride, 0 };
            return { ErrandKind::Wandering, entt::null, ticks(kIdleSeconds) };
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

    Pose between_steps(const Pose& pose, float along)
    {
        const float turn = std::remainder(pose.heading - pose.was_heading, 2 * glm::pi<float>());
        return { glm::mix(pose.was, pose.position, along), pose.was_heading + turn * along, pose.was, pose.was_heading };
    }

    std::uint32_t ticks(float seconds)
    {
        return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0f) / kTickSeconds));
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
        r.emplace_or_replace<Stride>(guest, Stride { .from = at, .to = at, .progress = 1, .lane_from = lane, .lane_to = lane });
        std::uint64_t mix = world.seed ^ (std::uint64_t { entt::to_integral(guest) } << 32);
        r.emplace_or_replace<Dice>(guest, random::seeded(random::split_mix(mix)));
        auto& pose = r.get_or_emplace<Pose>(guest);
        place(world.paths, r.get<Stride>(guest), pose);
        pose.was = pose.position;
        pose.was_heading = pose.heading;
    }

    bool departs(const Departure& departure, std::size_t riders, std::size_t capacity, float waited)
    {
        if (departure.max_wait >= 0 && waited >= static_cast<float>(departure.max_wait))
            return true;
        if (departure.min_wait >= 0 && waited < static_cast<float>(departure.min_wait))
            return false;
        // Quarters of the seats it waits for; any load is one rider.
        const auto quarters = 4 - static_cast<std::size_t>(departure.wait_for);
        return riders >= std::max<std::size_t>((capacity * quarters + 3) / 4, 1);
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

        for (auto [e, pose] : r.view<Pose>().each())
        {
            pose.was = pose.position;
            pose.was_heading = pose.heading;
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
                    if (errand.kind != ErrandKind::Wandering && errand.kind != ErrandKind::Heading && errand.kind != ErrandKind::Waiting)
                        continue;
                    if (stride.from >= paths.nodes().size() || stride.to >= paths.nodes().size())
                        continue; // Off the paths; nothing to walk on
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

                    if (stride.from != stride.to)
                    {
                        const float length = glm::distance(lane_point(paths, stride.from, stride.lane_from), lane_point(paths, stride.to, stride.lane_to));
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
                    stride.lane_from = stride.lane_to;
                    if (!stride.route.empty())
                    {
                        stride.to = stride.route.front();
                        stride.route.erase(stride.route.begin());
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
                            errand.rest = ticks(kIdleSeconds);
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
                                }
                            }
                            if (auto goal = choose(paths, at, on_offer, want, feel, wallet, dice))
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
                        if (errand.kind == ErrandKind::Wandering && party != nullptr && errand.rest == ticks(kIdleSeconds) && random::chance(dice.state, kHuddleChance))
                        {
                            party->huddle = ticks(static_cast<float>(random::between(dice.state, double { kShortestHuddle }, double { kLongestHuddle })));
                            stride.to = at;
                            stride.progress = 1;
                        }
                        else if (errand.kind == ErrandKind::Wandering)
                            stride.to = wander(paths, at, came, dice);
                    }
                    stride.lane_to = stride.to != at ? new_lane(dice) : stride.lane_from;
                    place(paths, stride, pose);
                }
            });
        }

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
                    const Party& party = r.get<Party>(member.party);
                    const Stride& lead = r.get<Stride>(party.leader);
                    // How far through a step the follower gets in a tick, hurrying to make up ground.
                    const auto hurry = [&] {
                        const float length = glm::distance(lane_point(paths, stride.from, stride.lane_from), lane_point(paths, stride.to, stride.lane_to));
                        return length > 0 ? kCatchUp * stride.speed * dt / length : 1.0f;
                    };
                    if (party.huddle > 0 && lead.from == lead.to && stride.to == lead.to && stride.progress >= 1)
                    {
                        // In a ring round a point just ahead of the leader, facing in, the leader standing on it.
                        const Pose& leader = r.get<Pose>(party.leader);
                        const glm::vec3 middle = paths.centre(lead.to);
                        glm::vec3 centre = leader.position + kHuddleRadius * glm::vec3(std::sin(leader.heading), 0.0f, std::cos(leader.heading));
                        centre.x = std::clamp(centre.x, middle.x - kLaneReach, middle.x + kLaneReach);
                        centre.z = std::clamp(centre.z, middle.z - kLaneReach, middle.z + kLaneReach);
                        const auto place_in_ring = static_cast<float>(std::ranges::find(party.members, followers[i]) - party.members.begin());
                        const auto leader_place = static_cast<float>(std::ranges::find(party.members, party.leader) - party.members.begin());
                        const float angle = std::atan2(leader.position.x - centre.x, leader.position.z - centre.z) + 2 * glm::pi<float>() * (place_in_ring - leader_place) / static_cast<float>(party.members.size());
                        // Stepping across to their place in the ring rather than jumping there.
                        const glm::vec3 spot = centre + kHuddleRadius * glm::vec3(std::sin(angle), 0.0f, std::cos(angle));
                        const glm::vec3 off = spot - pose.position;
                        const float reach = kCatchUp * stride.speed * dt;
                        pose.position = glm::length(off) <= reach ? spot : pose.position + off * (reach / glm::length(off));
                        pose.heading = angle + glm::pi<float>();
                        stride.from = lead.to;
                        stride.to = lead.to;
                        stride.lane_from = stride.lane_to = glm::vec2(pose.position.x - middle.x, pose.position.z - middle.z);
                        continue;
                    }
                    if (stride.from == lead.from && stride.to == lead.to)
                    {
                        // In step: closing in on the leader, never passing them.
                        if (stride.progress < lead.progress)
                            stride.progress = std::min(stride.progress + hurry(), lead.progress);
                    }
                    else if (stride.from != stride.to && stride.progress < 1)
                        stride.progress = std::min(stride.progress + hurry(), 1.0f); // Finishing their own step first
                    else
                    {
                        // At a tile, behind: the leader's step when it starts here, else the next tile towards them.
                        const NodeId at = stride.to;
                        stride.lane_from = stride.lane_to;
                        stride.from = at;
                        stride.progress = 0;
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
                        stride.lane_to = stride.to != stride.from ? new_lane(dice) : stride.lane_from;
                    }
                    place(paths, stride, pose);
                }
            });
        }

        // 3. Arrivals, in entity order. A leader's followers arrive with them.
        const auto buy = [&](entt::entity guest, entt::entity at, const Shop& shop, bool chose) {
            auto& attraction = r.get<Attraction>(at);
            auto& wallet = r.get<Wallet>(guest);
            auto& mood = r.get<Mood>(guest);
            auto& needs = r.get<Needs>(guest);
            float& need = shop.product == Product::Drink ? needs.thirst : needs.hunger;
            if (!chose && need < kWant)
                return; // Came along without wanting anything
            if (!attraction.open || wallet.cash < attraction.price)
            {
                mood.happiness = clamp01(mood.happiness - kTreat);
                return;
            }
            wallet.cash -= attraction.price;
            attraction.income += attraction.price;
            ++attraction.customers;
            need = 0;
            mood.happiness = clamp01(mood.happiness + kTreat);
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
            else if (auto* ride = r.try_get<CycleRide>(arrived[i]))
            {
                // In at the far end of the line, with those of the party who want to come; the rest wait (phase 5).
                const auto join = [&](entt::entity g) {
                    ride->queue.push_back(g);
                    r.get<Errand>(g) = { ErrandKind::Queueing, arrived[i], 0 };
                    r.get<Stride>(g).along = ride->line.empty() ? 0 : static_cast<float>(ride->line.size() - 1);
                };
                join(guest);
                for (const entt::entity m : followers)
                    if (r.get<Mood>(m).nausea < kMaxNausea && random::chance(r.get<Dice>(m).state, kJoinChance))
                        join(m);
            }
        }

        // 4. Rides, in entity order: queues shuffle forward, riders get off, the next ones board.
        for (const entt::entity e : sorted(r.view<CycleRide>()))
        {
            auto& ride = r.get<CycleRide>(e);
            auto& attraction = r.get<Attraction>(e);
            if (!ride.line.empty())
            {
                const float last = static_cast<float>(ride.line.size() - 1);
                for (std::size_t k = 0; k < ride.queue.size(); ++k)
                {
                    if (!r.valid(ride.queue[k]))
                        continue;
                    auto& stride = r.get<Stride>(ride.queue[k]);
                    const float place_in_line = std::min(static_cast<float>(k) * kQueueSpacing, last);
                    const float step = stride.speed * dt / paths.tile_size;
                    stride.along = stride.along > place_in_line ? std::max(stride.along - step, place_in_line) : place_in_line;
                    // Between the tile behind and the one ahead, facing the front; standing once in place.
                    const auto behind = static_cast<std::size_t>(std::ceil(stride.along));
                    const auto ahead = static_cast<std::size_t>(std::floor(stride.along));
                    const bool moving = stride.along > place_in_line;
                    stride.from = ride.line[behind];
                    stride.to = moving ? ride.line[ahead] : ride.line[behind];
                    stride.progress = static_cast<float>(behind) - stride.along;
                    auto& pose = r.get<Pose>(ride.queue[k]);
                    pose.position = glm::mix(paths.centre(ride.line[behind]), paths.centre(ride.line[ahead]), stride.progress);
                    if (ahead != behind)
                    {
                        const glm::vec3 d = paths.centre(ride.line[ahead]) - paths.centre(ride.line[behind]);
                        pose.heading = std::atan2(d.x, d.z);
                    }
                }
            }
            const auto unload = [&](std::vector<entt::entity>& riders) {
                for (const entt::entity guest : riders)
                {
                    if (!r.valid(guest))
                        continue;
                    auto& mood = r.get<Mood>(guest);
                    mood.happiness = clamp01(mood.happiness + ride.excitement * 0.3f);
                    mood.nausea = clamp01(mood.nausea + ride.nausea * 0.2f);
                    r.get<Errand>(guest) = after_ride(r, guest, e);
                    stop_at(paths, ride.exit != kNoNode ? ride.exit : attraction.entrance, r.get<Stride>(guest), r.get<Pose>(guest));
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
                    ride.per_hour = ride.per_hour > 0 ? std::lerp(ride.per_hour, rate, kFlowSmoothing) : rate;
                }
                ride.since_departure = 0;
                ride.loading = 0;
            };
            // A cycle ends when the ride's time is up, or a train has come round.
            auto* fleet = r.try_get<Trains>(e);
            std::optional<std::size_t> loading; // The train standing at the start, ready to load
            if (fleet != nullptr)
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                {
                    Coaster& train = fleet->trains[t];
                    if (drive(train, dt, room_ahead(fleet->trains, t)))
                        unload(fleet->riders[t]);
                    if (!train.running && train.travelled == 0 && !loading)
                        loading = t;
                }
            }
            else if (ride.timer > 0 && --ride.timer == 0)
                unload(ride.riders);
            // Loads once the first in line has reached the front, as many as there are seats left, and leaves when
            // its departure settings say.
            // shortcut: the rest of a load boards from wherever they stand in the line, upgrade when guests are shown boarding.
            const bool at_front = !ride.queue.empty() && (ride.line.empty() || !r.valid(ride.queue.front()) || r.get<Stride>(ride.queue.front()).along < 0.5f);
            const bool ready = fleet != nullptr ? loading.has_value() : ride.timer == 0;
            auto* riders = !ready ? nullptr : fleet != nullptr ? &fleet->riders[*loading] : &ride.riders;
            if (riders != nullptr && at_front && riders->size() < ride.capacity)
            {
                const std::size_t boarding = std::min<std::size_t>(ride.capacity - riders->size(), ride.queue.size());
                for (std::size_t i = 0; i < boarding; ++i)
                {
                    const entt::entity guest = ride.queue[i];
                    if (!r.valid(guest))
                        continue;
                    auto& wallet = r.get<Wallet>(guest);
                    if (!attraction.open || wallet.cash < attraction.price)
                    {
                        r.get<Errand>(guest) = after_ride(r, guest, e);
                        continue;
                    }
                    wallet.cash -= attraction.price;
                    attraction.income += attraction.price;
                    ++attraction.customers;
                    r.get<Errand>(guest).kind = ErrandKind::Riding;
                    riders->push_back(guest);
                }
                ride.queue.erase(ride.queue.begin(), ride.queue.begin() + static_cast<std::ptrdiff_t>(boarding));
            }
            if (riders != nullptr)
            {
                const auto* departure = r.try_get<Departure>(e);
                if (departs(departure != nullptr ? *departure : Departure { }, riders->size(), ride.capacity, ride.loading))
                {
                    depart(riders->size());
                    if (fleet != nullptr)
                        fleet->trains[*loading].running = true;
                    else
                        ride.timer = std::max<std::uint32_t>(ride.cycle_ticks, 1);
                }
                else
                    ride.loading += dt;
            }
            // A train left standing at the start goes, empty if need be, once another is held up behind it (within
            // a centimetre: positions far round a long track lose that much to rounding).
            if (fleet != nullptr && loading && !fleet->trains[*loading].running)
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                    if (t != *loading && fleet->trains[t].running && room_ahead(fleet->trains, t) < 0.01f)
                    {
                        fleet->trains[*loading].running = true;
                        depart(fleet->riders[*loading].size());
                    }
            }
        }

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
                for (const entt::entity m : party.members)
                {
                    auto& errand = r.get<Errand>(m);
                    if (errand.kind != ErrandKind::Following && !(m == party.leader && errand.kind == ErrandKind::Wandering))
                        continue;
                    auto& stride = r.get<Stride>(m);
                    const NodeId at = stride.progress < 0.5f ? stride.from : stride.to;
                    stride.route = paths.route(at, exit).value_or(std::vector<NodeId> { });
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
                        errand = errand.kind == ErrandKind::Waiting ? Errand { ErrandKind::Wandering, entt::null, ticks(kIdleSeconds) } : errand;
                    else
                        errand = { ErrandKind::Following, entt::null, 0 };
                }
            }
        }
        ++world.tick;
    }

} // namespace rct3::simulation
