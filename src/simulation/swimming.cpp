// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/swimming.hpp"

#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <queue>
#include <ranges>
#include <set>
#include <unordered_map>
#include <utility>

namespace rct3::simulation
{

    namespace
    {
        /// Whether a pool is cleaned with a harsh fluid while there is little dirt in it for the fluid to take.
        bool harsh(const SwimPool* pool, const PoolTank* tank)
        {
            return pool != nullptr && tank != nullptr && tank->fluid == PoolFluid::Harsh && tank->left.value > 0 && pool->dirt.value < kTuning.pools.good.value / 2;
        }

        /// A swimmer `dt` further with their dive off `board` of `pool`: up its ladder, out along it, off its end,
        /// down through the air and under, and back up, swimming on after.
        void dive_off(const SwimPool& pool, const PoolBoard& board, Swimmer& swimmer, Pose& pose, Mood& mood, float walk, float tile, float dt)
        {
            const auto& k = kTuning.dives;
            const glm::vec3 ahead { std::sin(board.heading.value), 0, std::cos(board.heading.value) };
            pose.heading = board.heading;
            swimmer.diving.value += dt;
            const auto next = [&](DiveStep step) {
                swimmer.dive = step;
                swimmer.diving = { };
            };
            // The pool's boards were laid out again (one built or taken away) under them: down off it, by its foot.
            if ((swimmer.dive == DiveStep::Climb || swimmer.dive == DiveStep::Walk || swimmer.dive == DiveStep::Jump) && std::ranges::find(board.levels, swimmer.height) == board.levels.end())
            {
                next(DiveStep::None);
                swimmer.board = -1;
                pose.position = swimmer.goal = board.foot;
                return;
            }
            switch (swimmer.dive)
            {
                case DiveStep::None:
                    break;
                case DiveStep::Climb:
                    pose.position = { board.foot.x, std::min(pose.position.y + k.climb.value * dt, swimmer.height), board.foot.z };
                    if (pose.position.y >= swimmer.height)
                        next(DiveStep::Walk);
                    break;
                case DiveStep::Walk:
                {
                    const glm::vec3 end = board.foot + ahead * board.reach;
                    const glm::vec2 to { end.x - pose.position.x, end.z - pose.position.z };
                    const float way = glm::length(to);
                    const glm::vec2 at = way <= walk * dt ? glm::vec2 { end.x, end.z } : glm::vec2 { pose.position.x, pose.position.z } + to * (walk * dt / way);
                    pose.position = { at.x, swimmer.height, at.y };
                    if (way <= walk * dt)
                        next(DiveStep::Jump);
                    break;
                }
                case DiveStep::Jump:
                    if (swimmer.diving.value >= k.jump.value)
                    {
                        next(DiveStep::Fall);
                        swimmer.fall = 0;
                    }
                    break;
                case DiveStep::Fall:
                {
                    swimmer.fall += kTuning.holding.gravity * dt;
                    pose.position += ahead * (k.leap.value * dt) + glm::vec3 { 0, -swimmer.fall * dt, 0 };
                    const auto water = std::ranges::find_if(pool.water, [&](const glm::vec3& w) { return std::abs(pose.position.x - w.x) <= tile / 2 && std::abs(pose.position.z - w.z) <= tile / 2; });
                    const float surface = water != pool.water.end() ? water->y : board.foot.y;
                    if (pose.position.y > surface)
                        break;
                    pose.position.y = surface;
                    swimmer.in_water = water != pool.water.end();
                    mood.happiness.value = std::min(mood.happiness.value + (board.high ? k.high_cheer : k.small_cheer).value, 1.0f);
                    next(DiveStep::Surface);
                    break;
                }
                case DiveStep::Surface:
                    if (swimmer.diving.value < k.surfacing.value)
                        break;
                    next(DiveStep::None);
                    swimmer.board = -1;
                    swimmer.goal = pose.position;
                    swimmer.idle = kTuning.pools.shortest_rest;
                    break;
            }
        }

        /// Joins each lazy river's stations to the complexes whose paving or pool paths lie beside them, and floats
        /// their riders round in their rings till they get off at a joined station and swim on in its complex.
        void float_on_rivers(SimWorld& world, float dt)
        {
            auto& r = world.registry;
            const auto& tuning = kTuning.rivers;
            constexpr float kPi = std::numbers::pi_v<float>;
            // Looked for again while a station joins none, or its complex has gone.
            for (const entt::entity e : sorted(r.view<River>()))
                for (River::Station& station : r.get<River>(e).stations)
                {
                    if (r.valid(station.pool) && r.all_of<SwimPool>(station.pool))
                        continue;
                    const auto join = pool_beside(world, station.points);
                    station.pool = join ? join->pool : entt::null;
                    station.square = join ? join->square : station.square;
                }
            for (const entt::entity g : sorted(r.view<Floating>()))
            {
                auto& f = r.get<Floating>(g);
                auto* swimmer = r.try_get<Swimmer>(g);
                auto* errand = r.try_get<Errand>(g);
                const auto* river = r.valid(f.river) ? r.try_get<River>(f.river) : nullptr;
                // Off the river some other way (it was pulled down, they were picked up): they float no more.
                if (swimmer == nullptr || errand == nullptr || errand->kind != ErrandKind::Riding || errand->target != f.river || river == nullptr || river->course.length() <= 0)
                {
                    r.remove<Floating>(g);
                    continue;
                }
                auto& dice = r.get<Dice>(g).state;
                auto& pose = r.get<Pose>(g);
                const float length = river->course.length();
                // Resting, their ring turns; come round to face along the river or back, they may paddle off that way.
                if (!f.paddling)
                {
                    const float was = f.facing.value;
                    f.facing.value += f.spin * dt;
                    for (const float way : { 0.0f, kPi, -kPi })
                        if ((was - way) * (f.facing.value - way) < 0 && random::chance(dice, tuning.paddle.value))
                        {
                            f.paddling = true;
                            f.facing.value = way == 0 ? 0 : kPi;
                            break;
                        }
                    f.facing.value = f.facing.value > kPi ? f.facing.value - 2 * kPi : f.facing.value < -kPi ? f.facing.value + 2 * kPi : f.facing.value;
                }
                else if (random::chance(dice, tuning.rest.value * dt))
                {
                    f.paddling = false;
                    f.spin = static_cast<float>(random::between(dice, -tuning.spin, tuning.spin));
                }
                // Carried by the current, faster paddling along it and slower paddling against it.
                const float moved = (tuning.flow.value + (f.paddling ? f.paddle.value * std::cos(f.facing.value) : 0.0f)) * dt;
                const float from = f.along;
                f.along = std::fmod(std::fmod(f.along + moved, length) + length, length);
                f.travelled += moved;
                swimmer->left.value -= dt;
                // Far enough, they get off at the next joined station they come to, onto the square joining it, and swim on in its
                // complex from a changing room of it, theirs if it is; with no station joined, back in their own.
                const auto joined = [&](const River::Station& s) { return r.valid(s.pool); };
                const auto reached = f.travelled >= f.stay && moved > 0 ? std::ranges::find_if(river->stations, [&](const River::Station& s) { return joined(s) && std::fmod(s.at - from + length, length) <= moved; }) : river->stations.end();
                const bool stranded = std::ranges::none_of(river->stations, joined);
                const entt::entity pool = reached != river->stations.end() ? reached->pool : stranded && r.valid(swimmer->room) && r.all_of<PoolDoor>(swimmer->room) ? r.get<PoolDoor>(swimmer->room).pool : entt::null;
                if (pool != entt::null)
                {
                    entt::entity room = r.valid(swimmer->room) && r.all_of<PoolDoor>(swimmer->room) && r.get<PoolDoor>(swimmer->room).pool == pool ? swimmer->room : entt::null;
                    for (const entt::entity door : sorted(r.view<const PoolDoor>()))
                        room = room == entt::null && r.get<PoolDoor>(door).pool == pool ? door : room;
                    if (room != entt::null)
                    {
                        std::erase(r.get<CycleRide>(f.river).riders, g);
                        r.remove<Floating>(g);
                        *errand = { .kind = ErrandKind::Swimming, .target = room };
                        pose.position = reached != river->stations.end() ? reached->square : r.get<PoolDoor>(room).poolside;
                        swimmer->goal = pose.position;
                        swimmer->room = entt::null;
                        swimmer->in_water = false;
                        swimmer->idle = { };
                        swimmer->spot = swimmer->place = -1;
                        continue;
                    }
                }
                // In their lane across the channel, facing as their ring does.
                const glm::vec3 way = track_way(river->course, f.along);
                const glm::vec3 across = glm::length(glm::vec2 { way.x, way.z }) > 0 ? glm::normalize(glm::vec3 { way.z, 0, -way.x }) : glm::vec3 { 1, 0, 0 };
                pose.position = track_point(river->course, f.along) + across * (f.lane * tuning.lane);
                pose.heading.value = std::atan2(way.x, way.z) + f.facing.value;
            }
        }

        /// Swimmers on slides climbed on foot: up the steps, then down the slide, faster down its steeper stretches,
        /// and off its end into the pool, splashing down as off a pool slide.
        void climb_slides(SimWorld& world, float dt)
        {
            auto& r = world.registry;
            const auto& tuning = kTuning.climbs;
            for (const entt::entity g : sorted(r.view<Climbing>()))
            {
                auto& c = r.get<Climbing>(g);
                const auto* errand = r.try_get<Errand>(g);
                const auto* slide = r.valid(c.slide) ? r.try_get<ClimbSlide>(c.slide) : nullptr;
                // Off it some other way (picked up, or it was pulled down from under them, when they fall from where
                // they are).
                if (!r.all_of<Swimmer>(g) || errand == nullptr || errand->kind != ErrandKind::Riding || errand->target != c.slide || slide == nullptr || c.way >= slide->ways.size())
                {
                    if (slide == nullptr && errand != nullptr && errand->kind == ErrandKind::Riding && errand->target == c.slide && r.all_of<Swimmer>(g))
                        c.along = std::numeric_limits<float>::max();
                    else
                    {
                        r.remove<Climbing>(g);
                        continue;
                    }
                }
                auto& pose = r.get<Pose>(g);
                glm::vec3 at = pose.position;
                glm::vec3 way { 0, 0, 1 };
                bool off = slide == nullptr;
                if (slide != nullptr)
                {
                    const ClimbSlide::Way& w = slide->ways[c.way];
                    way = track_way(w.course, c.along);
                    if (c.along < w.top)
                        c.speed = tuning.climb;
                    else
                    {
                        // Pulled down the slope, slowed by the slide.
                        c.speed.value += (-kTuning.holding.gravity * way.y - tuning.drag * c.speed.value) * dt;
                        c.speed.value = std::clamp(c.speed.value, tuning.slowest.value, tuning.fastest.value);
                    }
                    c.along += c.speed.value * dt;
                    off = c.along >= w.course.length();
                    at = track_point(w.course, std::min(c.along, w.course.length()));
                    way = track_way(w.course, std::min(c.along, w.course.length()));
                    r.get<Swimmer>(g).left.value -= dt;
                }
                if (!off)
                {
                    pose.position = at;
                    if (glm::length(glm::vec2 { way.x, way.z }) > 0)
                        pose.heading.value = std::atan2(way.x, way.z);
                    continue;
                }
                // Off the end: on as fast as they went, down into the water of the complex under it, or the one
                // they came from.
                const glm::vec3 velocity = way * c.speed.value;
                const auto door = pool_door_at(world, at);
                const entt::entity room = door ? *door : r.get<Swimmer>(g).room;
                const auto* door_of = r.valid(room) ? r.try_get<PoolDoor>(room) : nullptr;
                const auto* pool = door_of != nullptr && r.valid(door_of->pool) ? r.try_get<SwimPool>(door_of->pool) : nullptr;
                const entt::entity from = c.slide;
                if (r.valid(from) && r.all_of<CycleRide>(from))
                    std::erase(r.get<CycleRide>(from).riders, g);
                r.remove<Climbing>(g);
                const float ground = pool != nullptr && !pool->water.empty() ? std::ranges::min_element(pool->water, { }, [&](const glm::vec3& w) { return glm::distance(glm::vec2(w.x, w.z), glm::vec2(at.x, at.z)); })->y : at.y;
                step_out(r, g);
                r.remove<Errand, Stride, Linger, Gesture, Watching, RideWalk>(g);
                pose.position = at; // On from where they were as the step began
                pose.heading.value = std::atan2(velocity.x, velocity.z);
                r.emplace<Dropped>(g, Dropped { .ground = ground, .fall = { -velocity.y }, .drift = { velocity.x, velocity.z }, .node = path_to_rejoin(world.paths, at).value_or(kNoNode), .swimwear = true, .pool = room, .slide = from });
            }
        }
    } // namespace

    std::size_t dirt_step(const SwimPool& pool)
    {
        return std::min(static_cast<std::size_t>(pool.dirt.value * 6), std::size_t { 5 });
    }

    bool open_water(const SwimPool& pool, std::size_t square, glm::vec3 at, float tile)
    {
        if (square >= pool.open.size())
            return true;
        const float steps = static_cast<float>(kOpenSteps);
        const auto step = [&](float along) { return static_cast<std::size_t>(std::clamp((along / tile + 0.5f) * steps, 0.0f, steps - 1)); };
        return pool.open[square][step(at.z - pool.water[square].z) * kOpenSteps + step(at.x - pool.water[square].x)];
    }

    glm::vec2 pool_stop(const SwimPool& pool, glm::vec2 from, glm::vec2 to, float tile)
    {
        const auto over = [&](glm::vec2 at) {
            const auto square = std::ranges::find_if(pool.water, [&](const glm::vec3& w) { return std::abs(at.x - w.x) <= tile / 2 && std::abs(at.y - w.z) <= tile / 2; });
            return square != pool.water.end() && open_water(pool, static_cast<std::size_t>(square - pool.water.begin()), { at.x, 0, at.y }, tile);
        };
        // In steps of the open water's marks, at most a few hundred squares' worth.
        const float mark = tile / static_cast<float>(kOpenSteps);
        const int steps = static_cast<int>(std::min(std::ceil(glm::distance(from, to) / mark), 4096.0f));
        bool inside = over(from);
        glm::vec2 last = from;
        for (int i = 1; i <= steps; ++i)
        {
            const glm::vec2 at = glm::mix(from, to, static_cast<float>(i) / static_cast<float>(steps));
            const bool wet = over(at);
            if (inside && !wet)
                return last;
            inside = inside || wet;
            last = at;
        }
        return to;
    }

    std::optional<std::size_t> pool_walk_at(const SwimPool& pool, glm::vec3 at, float tile, float level)
    {
        std::optional<std::size_t> best;
        float nearest = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < pool.walks.size(); ++i)
            if (const glm::vec3& w = pool.walks[i].at; std::abs(at.x - w.x) <= tile / 2 && std::abs(at.z - w.z) <= tile / 2 && std::abs(at.y - w.y) < nearest)
            {
                best = i;
                nearest = std::abs(at.y - w.y);
            }
        if (best)
            return best;
        for (std::size_t i = 0; i < pool.walks.size(); ++i)
            if (const glm::vec3& w = pool.walks[i].at; std::max(std::abs(at.x - w.x), std::abs(at.z - w.z)) <= kTuning.pools.platform * tile && std::abs(at.y - w.y) <= 2 * level && glm::distance(at, w) < nearest)
            {
                best = i;
                nearest = glm::distance(at, w);
            }
        return best;
    }

    std::vector<std::optional<std::size_t>> pool_ways(const SwimPool& pool, std::size_t from)
    {
        std::vector<std::optional<std::size_t>> before(pool.walks.size());
        std::vector<float> far(pool.walks.size(), std::numeric_limits<float>::max());
        std::priority_queue<std::pair<float, std::size_t>, std::vector<std::pair<float, std::size_t>>, std::greater<>> open;
        before[from] = from;
        far[from] = 0;
        open.emplace(0.0f, from);
        while (!open.empty())
        {
            const auto [so_far, at] = open.top();
            open.pop();
            if (so_far > far[at] || (at != from && pool.walks[at].end))
                continue;
            const PoolWalk& walk = pool.walks[at];
            for (const PoolWalk::Join& join : walk.joins)
            {
                const PoolWalk& next = pool.walks[join.to];
                float length = walk.water != next.water ? kTuning.pools.dip : 0.0f;
                glm::vec3 last = walk.at;
                for (const glm::vec3& p : join.via)
                    length += glm::distance(std::exchange(last, p), p);
                length += glm::distance(last, next.at);
                if (so_far + length < far[join.to])
                {
                    far[join.to] = so_far + length;
                    before[join.to] = at;
                    open.emplace(far[join.to], join.to);
                }
            }
        }
        return before;
    }

    std::optional<entt::entity> pool_door_at(const SimWorld& world, glm::vec3 point)
    {
        const auto& r = world.registry;
        const float half = world.paths.tile_size / 2;
        const auto within = [&](const glm::vec3& w) { return std::abs(point.x - w.x) <= half && std::abs(point.z - w.z) <= half; };
        for (const entt::entity door : sorted(r.view<const PoolDoor>()))
        {
            const PoolDoor& d = r.get<PoolDoor>(door);
            const auto* pool = r.valid(d.pool) ? r.try_get<SwimPool>(d.pool) : nullptr;
            if (pool != nullptr && !pool->water.empty() && (std::ranges::any_of(pool->water, within) || std::ranges::any_of(pool->paved, within) || std::ranges::any_of(pool->spa, within)))
                return door;
        }
        return std::nullopt;
    }

    std::optional<PoolJoin> pool_beside(const SimWorld& world, std::span<const glm::vec3> station, float raised)
    {
        const auto& r = world.registry;
        const float tile = world.paths.tile_size;
        std::optional<PoolJoin> found;
        float closest = kTuning.rivers.join * tile;
        for (const entt::entity pool : sorted(r.view<const SwimPool>()))
        {
            const SwimPool& complex = r.get<SwimPool>(pool);
            for (const auto* squares : { &complex.paved, &complex.steps })
                for (const glm::vec3& square : *squares)
                    for (std::size_t k = 0; k < station.size(); ++k)
                    {
                        // Across the track from a stretch of the station, not past its ends, and at its level.
                        const glm::vec3 a = station[k];
                        const glm::vec3 b = station[std::min(k + 1, station.size() - 1)];
                        const glm::vec2 run { b.x - a.x, b.z - a.z };
                        const glm::vec2 to { square.x - a.x, square.z - a.z };
                        const float length = glm::length(run);
                        const float along = length > 0 ? glm::dot(to, run) / length : 0.0f;
                        const float across = length > 0 ? std::abs(run.x * to.y - run.y * to.x) / length : glm::length(to);
                        if (a.y - square.y <= -world.paths.level_height || a.y - square.y >= world.paths.level_height + raised || along < -tile / 2 || along > length + tile / 2 || across >= closest)
                            continue;
                        closest = across;
                        found = PoolJoin { .pool = pool, .square = square };
                    }
        }
        return found;
    }

    std::optional<entt::entity> pool_ride_pool(const SimWorld& world, entt::entity ride)
    {
        const auto& r = world.registry;
        const auto pool_of = [&](std::optional<entt::entity> door) { return door ? std::optional(r.get<PoolDoor>(*door).pool) : std::nullopt; };
        if (const auto* river = r.try_get<River>(ride))
        {
            const auto joined = std::ranges::find_if(river->stations, [&](const River::Station& s) { return r.valid(s.pool); });
            return joined != river->stations.end() ? std::optional(joined->pool) : std::nullopt;
        }
        if (const auto* climb = r.try_get<ClimbSlide>(ride); climb != nullptr && !climb->ways.empty() && !climb->ways.front().course.points.empty())
            return pool_of(pool_door_at(world, climb->ways.front().course.points.front()));
        const auto* fleet = r.try_get<Trains>(ride);
        if (fleet == nullptr || fleet->trains.empty())
            return std::nullopt;
        const Coaster& train = fleet->trains.front();
        if (train.splash_out)
            return pool_of(pool_door_at(world, track_point(train, train.length())));
        if (const auto* attraction = r.try_get<Attraction>(ride); attraction == nullptr || attraction->entrance != kNoNode)
            return std::nullopt;
        std::vector<glm::vec3> station;
        for (std::size_t i = 0; i + 1 < train.points.size() && i < train.kinds.size(); ++i)
            if (train.kinds[i] == Stretch::Station)
                station.insert(station.end(), { train.points[i], train.points[i + 1] });
        const auto join = station.empty() ? std::nullopt : pool_beside(world, station, kTuning.pools.slide_deck * world.paths.level_height);
        return join ? std::optional(join->pool) : std::nullopt;
    }

    bool in_swimwear(const entt::registry& r, entt::entity person)
    {
        if (const auto* swimmer = r.try_get<Swimmer>(person))
            return swimmer->changed;
        if (const auto* held = r.try_get<Held>(person))
            return held->swimwear;
        if (const auto* drop = r.try_get<Dropped>(person))
            return drop->swimwear && (r.get<Pose>(person).position.y > drop->ground || r.valid(drop->pool));
        return false;
    }

    void step_pools(SimWorld& world, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
        const float now = static_cast<float>(world.tick) * kTickSeconds;

        phase.emplace("Pools");
        // Changing rooms use up their cleaning fluid while open, cleaning their pool, and call for a mechanic to
        // fill the tank when it runs low.
        for (const entt::entity e : sorted(r.view<PoolDoor, PoolTank>()))
        {
            const auto& door = r.get<PoolDoor>(e);
            auto& tank = r.get<PoolTank>(e);
            auto& upkeep = r.get_or_emplace<Upkeep>(e, Upkeep { .inspect_every = { kInspectionSeconds[kRoomInspection] } });
            upkeep.since_inspection.value += dt;
            if (tank.left.value < kTuning.pools.refill_at.value)
                upkeep.called = true;
            if (!r.get<Attraction>(e).open || tank.left.value <= 0)
                continue;
            tank.left.value = std::max(tank.left.value - kTuning.pools.tank_use.value * dt, 0.0f);
            if (auto* pool = r.valid(door.pool) ? r.try_get<SwimPool>(door.pool) : nullptr)
                pool->dirt.value = std::max(pool->dirt.value - kTuning.pools.cleaning[std::to_underlying(tank.fluid)].value * dt, 0.0f);
        }

        phase.emplace("Swimming");
        // Swimmers change out of sight in the room, then swim from spot to spot in the water and now and then walk
        // the paved squares round it, resting between; their time up, they go back and change again, and come out
        // at the door. At an extra with every place taken they wait beside it a while.
        // The places taken on each pool's extras, a bit for each.
        std::unordered_map<entt::entity, std::vector<std::uint32_t>> places_taken;
        for (const auto [g, swimmer, errand] : r.view<const Swimmer, const Errand>().each())
            if (const auto* door = swimmer.place >= 0 && r.valid(errand.target) ? r.try_get<PoolDoor>(errand.target) : nullptr)
            {
                auto& places = places_taken[door->pool];
                places.resize(std::max(places.size(), static_cast<std::size_t>(swimmer.spot) + 1));
                places[static_cast<std::size_t>(swimmer.spot)] |= 1u << swimmer.place;
            }
        // The diving boards in use, by pool, one swimmer on each at a time.
        std::set<std::pair<entt::entity, std::int32_t>> boards_taken;
        for (const auto [g, swimmer, errand] : r.view<const Swimmer, const Errand>().each())
            if (const auto* door = swimmer.dive != DiveStep::None && r.valid(errand.target) ? r.try_get<PoolDoor>(errand.target) : nullptr)
                boards_taken.insert({ door->pool, swimmer.board });
        // And the ladders, one swimmer on the way to each or on it at a time.
        std::set<std::pair<entt::entity, std::int32_t>> ladders_taken;
        for (const auto [g, swimmer, errand] : r.view<const Swimmer, const Errand>().each())
            if (const auto* door = swimmer.ladder >= 0 && r.valid(errand.target) ? r.try_get<PoolDoor>(errand.target) : nullptr)
                ladders_taken.insert({ door->pool, swimmer.ladder });
        // And those waiting their turn at each high board, in a line behind its ladder.
        std::map<std::pair<entt::entity, std::int32_t>, std::vector<entt::entity>> board_lines;
        for (const auto [g, swimmer, errand] : r.view<const Swimmer, const Errand>().each())
            if (const auto* door = swimmer.board >= 0 && swimmer.dive == DiveStep::None && swimmer.waited.value > 0 && r.valid(errand.target) ? r.try_get<PoolDoor>(errand.target) : nullptr)
                board_lines[{ door->pool, swimmer.board }].push_back(g);
        for (const entt::entity g : sorted(r.view<Swimmer>()))
        {
            auto& swimmer = r.get<Swimmer>(g);
            auto& pose = r.get<Pose>(g);
            // How far they have still to go along their way over the complex to their goal.
            const auto way_left = [&](const PoolRoute& route) {
                float left = 0;
                glm::vec3 from = pose.position;
                for (const PoolRoute::Point& p : route.via | std::views::reverse)
                    left += glm::distance(from, std::exchange(from, p.at));
                return left + glm::distance(from, swimmer.goal);
            };
            // Splashing down off a slide, or queueing for or riding one, they swim on after.
            auto* swimming = r.try_get<Errand>(g);
            if (r.all_of<Dropped>(g) || (swimming != nullptr && (swimming->kind == ErrandKind::Queueing || swimming->kind == ErrandKind::Riding) && r.valid(swimmer.room)))
            {
                // In a slide's line they stand a gap behind the one ahead along the way they came to it, stepping up
                // as it moves on, never within a gap of anyone ahead; there already, as two coming the same way at once
                // or lines from two ways meeting are, they step away.
                auto* route = swimming != nullptr && swimming->kind == ErrandKind::Queueing && r.all_of<Trains>(swimming->target) ? r.try_get<PoolRoute>(g) : nullptr;
                if (route == nullptr || route->goal != swimmer.goal)
                    continue;
                const auto& line = r.get<CycleRide>(swimming->target).queue;
                const auto at = std::ranges::find(line, g);
                const float pace = r.get<Stride>(g).speed.value * dt;
                const auto crowding = [&](const glm::vec3& p) { return std::find_if(line.begin(), at, [&](entt::entity o) { return glm::distance(p, r.get<Pose>(o).position) < kTuning.pools.queue_gap; }); };
                if (crowding(pose.position) != at)
                {
                    // Away from them all; from the very spot one stands on, back from the way they face; caught between
                    // two, to the side.
                    const glm::vec2 back = -glm::vec2 { std::sin(pose.heading.value), std::cos(pose.heading.value) };
                    glm::vec2 away { 0.0f };
                    for (auto o = line.begin(); o != at; ++o)
                        if (const glm::vec3 off = pose.position - r.get<Pose>(*o).position; glm::length(off) < kTuning.pools.queue_gap)
                            away += glm::length(glm::vec2 { off.x, off.z }) > 1e-3f ? glm::normalize(glm::vec2 { off.x, off.z }) : back;
                    away = glm::length(away) > 0.5f ? glm::normalize(away) : glm::vec2 { -back.y, back.x };
                    pose.position += glm::vec3 { away.x, 0, away.y } * pace;
                    continue;
                }
                const float step = std::min(pace, way_left(*route) - static_cast<float>(at - line.begin()) * kTuning.pools.queue_gap);
                if (step <= 0)
                    continue;
                const glm::vec3 to = (route->via.empty() ? swimmer.goal : route->via.back().at) - pose.position;
                const float left = glm::length(to);
                // Nor up close on anyone behind them in it who stands in the way, who steps away.
                if (const glm::vec3 next = pose.position + (left <= step ? to : to * (step / left)); crowding(next) != at || std::any_of(std::next(at), line.end(), [&](entt::entity o) { return glm::distance(next, r.get<Pose>(o).position) < kTuning.pools.queue_gap / 2; }))
                    continue;
                if (glm::length(glm::vec2 { to.x, to.z }) > 0)
                    pose.heading.value = std::atan2(to.x, to.z);
                pose.position += left <= step ? to : to * (step / left);
                if (left <= step && !route->via.empty())
                    route->via.pop_back();
                continue;
            }
            // Thrown out by a crashed train's blast, they swim no more.
            const auto* door = swimming != nullptr && r.valid(swimming->target) ? r.try_get<PoolDoor>(swimming->target) : nullptr;
            auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<SwimPool>(door->pool) : nullptr;
            if (swimming == nullptr || swimming->kind != ErrandKind::Swimming || pool == nullptr || pool->water.empty())
            {
                r.remove<Swimmer, PoolRoute>(g);
                continue;
            }
            auto& errand = *swimming;
            auto& dice = r.get<Dice>(g).state;
            if (swimmer.changing.value > 0)
            {
                pose.position = door->room;
                if ((swimmer.changing.value -= dt) > 0)
                    continue;
                if (swimmer.changed)
                {
                    // Dressed again: out at the door, thinking of the water, the party going on together behind
                    // its leader.
                    const auto* tank = r.try_get<PoolTank>(errand.target);
                    if (r.all_of<Staff>(g) && pool->dirt.value >= kTuning.pools.bad.value)
                        tell(r, g, pool->dirt.value >= kTuning.inspector.filthy.value ? Finding::FilthyPool : Finding::DirtyPool, errand.target, now);
                    if (harsh(pool, tank))
                        think(r, g, ThoughtKind::PoolOvercleaned, errand.target, now);
                    else if (pool->dirt.value >= kTuning.pools.bad.value)
                        think(r, g, ThoughtKind::PoolBad, errand.target, now);
                    else if (pool->dirt.value < kTuning.pools.good.value)
                        think(r, g, ThoughtKind::PoolGood, errand.target, now);
                    const NodeId out = r.get<Attraction>(errand.target).entrance;
                    const auto* member = r.try_get<Member>(g);
                    const bool follows = member != nullptr && r.valid(member->party) && r.get<Party>(member->party).leader != g;
                    errand = follows ? Errand { ErrandKind::Following, entt::null, 0 } : Errand { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
                    r.remove<Swimmer, PoolRoute>(g);
                    if (out != kNoNode)
                        stop_at(paths, out, r.get<Stride>(g), pose);
                    continue;
                }
                swimmer.changed = true;
                pose.position = door->poolside;
                swimmer.goal = door->poolside;
                swimmer.to_spa = false;
            }
            // On or off a diving board, the dive takes them till they are back in the water.
            if (swimmer.dive != DiveStep::None)
            {
                if (swimmer.board >= 0 && static_cast<std::size_t>(swimmer.board) < pool->boards.size())
                    dive_off(*pool, pool->boards[static_cast<std::size_t>(swimmer.board)], swimmer, pose, r.get<Mood>(g), r.get<Stride>(g).speed.value, paths.tile_size, dt);
                else
                    swimmer.dive = DiveStep::None;
                continue;
            }
            // Over the water's edge: still where they stand while its motion carries them over, then where it leaves
            // them. Seen from the sim they go the way straight, in its time.
            if (swimmer.edge != PoolEdge::None)
            {
                const auto& k = kTuning.pools;
                const float length = (swimmer.edge == PoolEdge::DiveIn ? k.dive_in : swimmer.edge == PoolEdge::ClimbOut ? k.climb_out : k.ladder).value;
                swimmer.edging.value += dt;
                pose.position = glm::mix(swimmer.edge_from, swimmer.edge_to, std::min(swimmer.edging.value / length, 1.0f));
                swimmer.left.value -= dt;
                if (swimmer.edging.value < length)
                    continue;
                swimmer.in_water = swimmer.edge == PoolEdge::DiveIn || swimmer.edge == PoolEdge::LadderIn;
                swimmer.edge = PoolEdge::None;
                swimmer.ladder = -1;
                // A goal the motion carried them past is as good as reached: not back over the edge to it.
                const auto flat = [](const glm::vec3& v) { return glm::vec2 { v.x, v.z }; };
                if (glm::distance(flat(swimmer.edge_from), flat(swimmer.goal)) <= glm::distance(flat(swimmer.edge_from), flat(swimmer.edge_to)))
                    swimmer.goal = pose.position;
                swimmer.edging = { };
                swimmer.stayed = { };
                continue;
            }
            // To their goal over the squares joined to the one they are on, the way found again whenever the goal
            // changes: across the paving and up and down its ramps, slopes, steps and staircases straight from point to
            // point, at the paving's height all the way; across water as below. A goal no way leads to they go to
            // straight, as one left off the complex does to the room's door.
            auto* route = r.try_get<PoolRoute>(g);
            if (route == nullptr || route->goal != swimmer.goal)
            {
                route = &r.emplace_or_replace<PoolRoute>(g, PoolRoute { .goal = swimmer.goal });
                const auto from = pool_walk_at(*pool, pose.position, paths.tile_size, paths.level_height);
                const auto to = pool_walk_at(*pool, swimmer.goal, paths.tile_size, paths.level_height);
                const auto before = from ? pool_ways(*pool, *from) : std::vector<std::optional<std::size_t>> { };
                // They keep to the lane they are on of the three each way across a square, crossing from square to
                // square on it, so a crowd spreads over the pool; on their way to a slide, to the middle one, so its
                // line is one file. A lane taking them over the water's edge, they take the middle one.
                const auto way = [&](glm::vec3 lane) {
                    std::vector<PoolRoute::Point> via;
                    for (std::size_t at = to.value_or(0); from && to && before[at] && at != *from; at = *before[at])
                    {
                        const PoolWalk& last = pool->walks[*before[at]];
                        const auto join = std::ranges::find(last.joins, at, &PoolWalk::Join::to);
                        for (std::size_t i = join->via.size(); i-- > 0;)
                        {
                            glm::vec3 p = join->via[i];
                            if (join->via.size() == 1) // Where the two squares meet: along their side
                            {
                                const bool across_x = std::abs(p.x - last.at.x) > std::abs(p.z - last.at.z);
                                (across_x ? p.z : p.x) += across_x ? lane.z : lane.x;
                            }
                            via.push_back({ .at = p, .water = i == 0 && last.water });
                        }
                    }
                    return via;
                };
                const auto wet = [&](glm::vec3 at) {
                    const auto w = std::ranges::find_if(pool->water, [&](const glm::vec3& s) { return std::abs(at.x - s.x) <= paths.tile_size / 2 && std::abs(at.z - s.z) <= paths.tile_size / 2; });
                    return w != pool->water.end() && open_water(*pool, static_cast<std::size_t>(w - pool->water.begin()), at, paths.tile_size);
                };
                const auto clear = [&](const std::vector<PoolRoute::Point>& via) {
                    std::vector<glm::vec3> points { pose.position };
                    for (const PoolRoute::Point& p : via | std::views::reverse)
                        points.push_back(p.at);
                    points.push_back(swimmer.goal);
                    for (std::size_t i = 1; i < points.size(); ++i)
                        if (const glm::vec3 a = points[i - 1], b = points[i]; wet(a) && wet(b) && pool_stop(*pool, { a.x, a.z }, { b.x, b.z }, paths.tile_size) != glm::vec2 { b.x, b.z })
                            return false;
                    return true;
                };
                glm::vec3 lane { 0.0f };
                if (from && !r.valid(swimmer.slide))
                {
                    const auto snap = [](float off) { return std::clamp(std::round(off / kTuning.pools.lanes), -1.0f, 1.0f) * kTuning.pools.lanes; };
                    lane = { snap(pose.position.x - pool->walks[*from].at.x), 0, snap(pose.position.z - pool->walks[*from].at.z) };
                }
                route->via = way(lane);
                if (lane != glm::vec3 { 0.0f } && !clear(route->via))
                    route->via = way(glm::vec3 { 0.0f });
            }
            // Out of the water on their way to a slide whose line reaches back to them: at its end, there.
            if (const auto* line = r.valid(swimmer.slide) && !swimmer.in_water && r.all_of<Trains>(swimmer.slide) ? r.try_get<CycleRide>(swimmer.slide) : nullptr; line != nullptr && !line->queue.empty() && way_left(*route) <= static_cast<float>(line->queue.size()) * kTuning.pools.queue_gap)
            {
                const auto* upkeep = r.try_get<Upkeep>(swimmer.slide);
                if (r.get<Attraction>(swimmer.slide).open && (upkeep == nullptr || !upkeep->broken))
                {
                    swimmer.room = errand.target;
                    errand = { .kind = ErrandKind::Queueing, .target = swimmer.slide };
                    r.get<CycleRide>(swimmer.slide).queue.push_back(g);
                }
                swimmer.slide = entt::null;
                continue;
            }
            if (!route->via.empty() && !route->via.back().water && !swimmer.in_water)
            {
                const glm::vec3 to = route->via.back().at - pose.position;
                const float left = glm::length(to);
                const float pace = r.get<Stride>(g).speed.value * dt;
                if (glm::length(glm::vec2 { to.x, to.z }) > 0)
                    pose.heading.value = std::atan2(to.x, to.z);
                pose.position += left <= pace ? to : to * (pace / left);
                if (left <= pace)
                    route->via.pop_back();
                swimmer.left.value -= dt;
                continue;
            }
            const glm::vec3 target = route->via.empty() ? swimmer.goal : route->via.back().at;
            // Across the ground, at the water's surface over open water and on the paving elsewhere, the coping round
            // the water too: of the squares under them, the one nearest their height; off them, at the height they are
            // going to.
            const glm::vec2 to { target.x - pose.position.x, target.z - pose.position.z };
            const float way = glm::length(to);
            const float pace = (swimmer.in_water ? kTuning.pools.swim.value : r.get<Stride>(g).speed.value) * dt;
            const bool there = way <= pace;
            swimmer.stayed.value = there && route->via.empty() ? swimmer.stayed.value + dt : 0;
            if (!there)
                pose.heading.value = std::atan2(to.x, to.y);
            const glm::vec2 at = there ? glm::vec2 { target.x, target.z } : glm::vec2 { pose.position.x, pose.position.z } + to * (pace / way);
            const float half = paths.tile_size / 2;
            const auto within = [&](const glm::vec3& w) { return std::abs(at.x - w.x) <= half && std::abs(at.y - w.z) <= half; };
            const auto under = [&](const std::vector<glm::vec3>& spots) {
                const auto best = std::ranges::min_element(spots, { }, [&](const glm::vec3& w) { return within(w) ? std::abs(w.y - pose.position.y) : std::numeric_limits<float>::max(); });
                return best != spots.end() && within(*best) ? best : spots.end();
            };
            const auto square = under(pool->water);
            const auto pave = under(pool->paved);
            std::optional<float> water;
            std::optional<float> paved;
            if (pave != pool->paved.end())
                paved = pave->y;
            if (square != pool->water.end() && open_water(*pool, static_cast<std::size_t>(square - pool->water.begin()), { at.x, 0, at.y }, paths.tile_size))
                water = square->y;
            else if (square != pool->water.end() && !paved)
                paved = square->y - rct3::park::kPoolBrim * paths.level_height + kTuning.pools.deck;
            const bool in_spa = swimmer.to_spa && within(swimmer.goal);
            const bool wet = water && (!paved || std::abs(*water - pose.position.y) <= std::abs(*paved - pose.position.y));
            // At the open water's edge, not on the way to a ladder: in off it, diving where they stand on the paving,
            // or out over the wall from the water. Out of a spa pool they step.
            const auto edge = [&](PoolEdge how, float reach, float height) {
                const glm::vec3 ahead { std::sin(pose.heading.value), 0, std::cos(pose.heading.value) };
                swimmer.edge = how;
                swimmer.edge_from = pose.position;
                swimmer.edge_to = glm::vec3 { pose.position.x, height, pose.position.z } + ahead * reach;
                swimmer.edging = { };
            };
            if (swimmer.ladder < 0 && !in_spa && wet && !swimmer.in_water)
            {
                edge(PoolEdge::DiveIn, kTuning.pools.dive_reach, *water);
                continue;
            }
            const bool spa = std::ranges::any_of(pool->spa, [&](const glm::vec3& seat) { return std::abs(seat.x - pose.position.x) <= half && std::abs(seat.z - pose.position.z) <= half; });
            if (swimmer.ladder < 0 && !in_spa && !wet && swimmer.in_water && !spa)
            {
                edge(PoolEdge::ClimbOut, kTuning.pools.out_reach, paved.value_or(pose.position.y));
                continue;
            }
            // On the way to a ladder they keep to the water or the paving they are on.
            // shortcut: one walking to a ladder over open water walks it at the paving's height; upgrade with a way
            // round the water's edge.
            if (swimmer.ladder >= 0)
                pose.position = { at.x, swimmer.in_water ? water.value_or(swimmer.goal.y) : paved.value_or(swimmer.goal.y), at.y };
            else
            {
                swimmer.in_water = wet || in_spa;
                pose.position = { at.x, in_spa ? swimmer.goal.y : wet ? *water : paved.value_or(target.y), at.y };
            }
            // In the water they dirty it, the less the more water there is, and enjoy it the cleaner it is.
            if (swimmer.in_water)
            {
                pool->dirt.value = std::min(pool->dirt.value + kTuning.pools.dirtying.value * dt / static_cast<float>(pool->water.size()), 1.0f);
                const float liking = harsh(pool, r.try_get<PoolTank>(errand.target)) ? -0.5f : 1 - 2 * pool->dirt.value / kTuning.pools.bad.value;
                auto& happiness = r.get<Mood>(g).happiness.value;
                happiness = std::clamp(happiness + kTuning.pools.cheer.value * liking * dt, 0.0f, 1.0f);
            }
            swimmer.left.value -= dt;
            if (!there)
                continue;
            if (!route->via.empty())
            {
                route->via.pop_back();
                continue;
            }
            // At a ladder: up it out of the water, or down it in.
            if (swimmer.ladder >= 0)
            {
                const auto which = static_cast<std::size_t>(swimmer.ladder);
                if (which >= pool->ladders.size())
                    swimmer.ladder = -1;
                else
                {
                    const PoolLadder& ladder = pool->ladders[which];
                    pose.heading = ladder.heading;
                    swimmer.edge = swimmer.in_water ? PoolEdge::LadderOut : PoolEdge::LadderIn;
                    swimmer.edge_from = swimmer.in_water ? ladder.water : ladder.deck;
                    swimmer.edge_to = swimmer.in_water ? ladder.deck : ladder.water;
                    swimmer.edging = { };
                    pose.position = swimmer.edge_from;
                    continue;
                }
            }
            // At a slide's station: into its queue, up on the platform, if it still runs.
            if (r.valid(swimmer.slide))
            {
                const entt::entity slide = std::exchange(swimmer.slide, entt::null);
                const auto* upkeep = r.try_get<Upkeep>(slide);
                // Into a lazy river at its station there, in a ring, free on the complex's ticket.
                if (const auto* river = r.try_get<River>(slide); river != nullptr && r.get<Attraction>(slide).open && (upkeep == nullptr || !upkeep->broken))
                {
                    const auto station = std::ranges::min_element(river->stations, { }, [&](const River::Station& st) { return st.pool == door->pool ? glm::distance(st.square, pose.position) : std::numeric_limits<float>::max(); });
                    if (station->pool != door->pool)
                        continue;
                    const auto& tuning = kTuning.rivers;
                    r.emplace_or_replace<Floating>(
                        g,
                        Floating { .river = slide,
                                   .along = station->at,
                                   .stay = static_cast<float>(random::between(dice, tuning.least_stay, tuning.most_stay)) * river->course.length(),
                                   .lane = static_cast<float>(random::between(dice, -1.0, 1.0)),
                                   .facing = { static_cast<float>(random::between(dice, -std::numbers::pi, std::numbers::pi)) },
                                   .spin = static_cast<float>(random::between(dice, -tuning.spin, tuning.spin)),
                                   .paddle = { static_cast<float>(random::between(dice, tuning.least_paddle.value, tuning.most_paddle.value)) } });
                    swimmer.room = errand.target;
                    swimmer.in_water = true;
                    errand = { .kind = ErrandKind::Riding, .target = slide };
                    r.get<CycleRide>(slide).riders.push_back(g);
                    ++r.get<Attraction>(slide).customers;
                    continue;
                }
                // Up the steps of a slide climbed on foot, once the one ahead is far enough up; treading water
                // at its foot till then.
                // shortcut: those waiting tread water together at the foot, upgrade to a line in the water.
                if (const auto* climb = r.try_get<ClimbSlide>(slide); climb != nullptr && r.get<Attraction>(slide).open && (upkeep == nullptr || !upkeep->broken))
                {
                    if (std::ranges::any_of(r.get<CycleRide>(slide).riders, [&](entt::entity on) {
                            const auto* c = r.try_get<Climbing>(on);
                            return c != nullptr && c->along < kTuning.climbs.gap;
                        }))
                    {
                        swimmer.slide = slide;
                        continue;
                    }
                    r.emplace_or_replace<Climbing>(g, Climbing { .slide = slide, .way = static_cast<std::uint8_t>(random::below(dice, climb->ways.size())), .speed = kTuning.climbs.climb });
                    swimmer.room = errand.target;
                    swimmer.in_water = false;
                    errand = { .kind = ErrandKind::Riding, .target = slide };
                    r.get<CycleRide>(slide).riders.push_back(g);
                    ++r.get<Attraction>(slide).customers;
                    continue;
                }
                if (r.get<Attraction>(slide).open && (upkeep == nullptr || !upkeep->broken))
                {
                    pose.position = swimmer.goal;
                    swimmer.room = errand.target;
                    swimmer.in_water = false;
                    errand = { .kind = ErrandKind::Queueing, .target = slide };
                    r.get<CycleRide>(slide).queue.push_back(g);
                    continue;
                }
            }
            // At a diving board's foot: on it, once nobody else is; a high one up to one of its boards, the top one
            // unless they walk out on a lower one on the way.
            if (swimmer.board >= 0 && static_cast<std::size_t>(swimmer.board) < pool->boards.size())
            {
                const PoolBoard& board = pool->boards[static_cast<std::size_t>(swimmer.board)];
                // Those waiting their turn stand in a line behind it, a high board's behind its ladder, not in the
                // tower, a gap apart, in the order they came to their places; each steps up as the line moves.
                auto& line = board_lines[{ door->pool, swimmer.board }];
                const auto ahead = swimmer.waited.value == 0 ? line.size() : static_cast<std::size_t>(std::ranges::count_if(line, [&](entt::entity o) {
                    const float waited = r.get<Swimmer>(o).waited.value;
                    return o != g && (waited > swimmer.waited.value || (waited == swimmer.waited.value && o < g));
                }));
                if (ahead > 0 || !boards_taken.insert({ door->pool, swimmer.board }).second)
                {
                    const float back = (board.high ? kTuning.dives.ladder_queue : kTuning.pools.queue_gap) + static_cast<float>(ahead) * kTuning.pools.queue_gap;
                    glm::vec3 place = board.foot - glm::vec3 { std::sin(board.heading.value), 0, std::cos(board.heading.value) } * back;
                    // A line as long as the paving behind it takes no more: the rest go off to do something else.
                    const bool room = std::ranges::any_of(pool->walks, [&](const PoolWalk& w) { return !w.water && std::abs(place.x - w.at.x) <= paths.tile_size / 2 && std::abs(place.z - w.at.z) <= paths.tile_size / 2 && std::abs(place.y - w.at.y) < paths.level_height; });
                    if (room && swimmer.waited.value == 0) // At its end, behind any joining before them this moment too
                        line.push_back(g);
                    if (room)
                        swimmer.waited.value = std::max(swimmer.waited.value, dt);
                    if (room && glm::distance(place, swimmer.goal) > 0.01f)
                    {
                        swimmer.goal = place;
                        continue;
                    }
                    if (room && (swimmer.waited.value += dt) < kTuning.pools.patience.value)
                        continue;
                    swimmer.board = -1;
                    swimmer.waited = { };
                }
                else
                {
                    swimmer.waited = { };
                    swimmer.height = board.levels.back();
                    for (std::size_t level = 0; level + 1 < board.levels.size(); ++level)
                        if (random::chance(dice, kTuning.dives.step_off.value))
                        {
                            swimmer.height = board.levels[level];
                            break;
                        }
                    swimmer.dive = board.high ? DiveStep::Climb : DiveStep::Walk;
                    swimmer.diving = { };
                    swimmer.in_water = false;
                    pose.position = board.foot;
                    continue;
                }
            }
            if (swimmer.left.value <= 0)
            {
                swimmer.spot = swimmer.place = -1;
                // Back at the room's side of the pool: in to change.
                if (glm::distance(pose.position, door->poolside) < 0.01f)
                    swimmer.changing = kTuning.pools.changing;
                else
                {
                    swimmer.goal = door->poolside;
                    swimmer.to_spa = false;
                }
                continue;
            }
            if (swimmer.spot >= 0 && static_cast<std::size_t>(swimmer.spot) < pool->extras.size() && swimmer.place < 0 && pool->extras[static_cast<std::size_t>(swimmer.spot)].use != PoolUse::Stand)
            {
                // At a lounger or shower: on it in a free place, one lounger of a set each, round a shower's pole
                // facing it; else wait their turn.
                const PoolSpot& spot = pool->extras[static_cast<std::size_t>(swimmer.spot)];
                auto& places = places_taken[door->pool];
                places.resize(std::max(places.size(), pool->extras.size()));
                std::uint32_t& used = places[static_cast<std::size_t>(swimmer.spot)];
                std::int32_t free = 0;
                while (free < spot.places && (used & (1u << free)) != 0)
                    ++free;
                if (free < spot.places)
                {
                    used |= 1u << free;
                    swimmer.place = free;
                    swimmer.use = spot.use;
                    swimmer.waited = { };
                    swimmer.idle = spot.use == PoolUse::Lounge ? kTuning.pools.lounging : kTuning.pools.showering;
                    const glm::vec3 ahead { std::sin(spot.heading.value), 0, std::cos(spot.heading.value) };
                    const glm::vec3 side { ahead.z, 0, -ahead.x };
                    const auto& tuning = kTuning.pools;
                    // The shower's places a third of a turn apart, the last where a saved park's showering guest stood.
                    const float turn = glm::radians(30.5f) + 2 * std::numbers::pi_v<float> * static_cast<float>(free + 1 - spot.places) / static_cast<float>(spot.places);
                    const glm::vec3 off = spot.use == PoolUse::Lounge ? ahead * (tuning.lounger_reach - static_cast<float>(spot.places - 1 - free) * tuning.lounger_gap) + side * tuning.lounger_side : (side * std::cos(turn) + ahead * std::sin(turn)) * tuning.shower_reach;
                    swimmer.goal = spot.at + off;
                    pose.position = swimmer.goal;
                    pose.heading = spot.use == PoolUse::Lounge ? spot.heading : Radians { std::atan2(-off.x, -off.z) };
                    continue;
                }
                // shortcut: those waiting stand in the middle of its square together; upgrade to a line beside it.
                if ((swimmer.waited.value += dt) < kTuning.pools.patience.value)
                    continue;
                swimmer.idle = { };
            }
            if ((swimmer.idle.value -= dt) > 0)
                continue;
            swimmer.spot = swimmer.place = -1;
            swimmer.use = PoolUse::Stand;
            swimmer.waited = { };
            swimmer.idle.value = static_cast<float>(random::between(dice, double { kTuning.pools.shortest_rest.value }, double { kTuning.pools.longest_rest.value }));
            // Only to what a way leads to from where they are.
            const auto here = pool_walk_at(*pool, pose.position, paths.tile_size, paths.level_height);
            const auto before = here ? pool_ways(*pool, *here) : std::vector<std::optional<std::size_t>> { };
            const auto reach = [&](const glm::vec3& p) {
                const auto w = here ? pool_walk_at(*pool, p, paths.tile_size, paths.level_height) : std::nullopt;
                return w && before[*w].has_value();
            };
            // Now and then over to one of the complex's open slides, those ending in its water or running back up
            // through it to their station, where the rider gets on: along its station, at the end nearest the paving
            // or steps beside the platform, so they step from there straight on to it.
            // shortcut: the platform's deck is taken to be level with that paving, as slides' platforms are laid;
            // upgrade with the platform's own deck height should one stand higher or lower.
            std::vector<std::pair<entt::entity, glm::vec3>> slides;
            for (const entt::entity s : sorted(r.view<const Trains, const Attraction, const CycleRide>()))
            {
                const Coaster& train = r.get<Trains>(s).trains.front();
                const auto* upkeep = r.try_get<Upkeep>(s);
                if ((!train.splash_out && train.drop_offs.empty()) || !r.get<Attraction>(s).open || (upkeep != nullptr && upkeep->broken) || pool_ride_pool(world, s) != door->pool)
                    continue;
                glm::vec3 on = track_point(train, train.start - train.train_length / 2);
                if (const auto beside = pool_walk_at(*pool, on, paths.tile_size, paths.level_height))
                {
                    // Off the paving's own square: a station's last point may lie on its edge.
                    const auto off = [&](const glm::vec3& p) {
                        const glm::vec2 d { p.x - pool->walks[*beside].at.x, p.z - pool->walks[*beside].at.z };
                        return std::max(std::abs(d.x), std::abs(d.y)) <= paths.tile_size / 2 ? std::numeric_limits<float>::max() : glm::length(d);
                    };
                    for (std::size_t i = 0; i + 1 < train.points.size(); ++i)
                        if (i < train.kinds.size() && train.kinds[i] == Stretch::Station)
                            for (const glm::vec3& p : { train.points[i], train.points[i + 1] })
                                if (off(p) < off(on))
                                    on = p;
                    on.y = pool->walks[*beside].at.y; // On the platform's deck, level with the paving, not its track
                }
                else
                {
                    // A platform standing off the complex, beside the paving or steps that join it: on from there.
                    std::vector<glm::vec3> station;
                    for (std::size_t i = 0; i + 1 < train.points.size() && i < train.kinds.size(); ++i)
                        if (train.kinds[i] == Stretch::Station)
                            station.insert(station.end(), { train.points[i], train.points[i + 1] });
                    if (const auto join = pool_beside(world, station, kTuning.pools.slide_deck * paths.level_height))
                        on = join->square;
                }
                slides.emplace_back(s, on);
            }
            // And over to the foot of a slide climbed on foot that starts and ends in its water.
            for (const entt::entity s : sorted(r.view<const ClimbSlide, const Attraction>()))
            {
                const auto* upkeep = r.try_get<Upkeep>(s);
                const auto& ways = r.get<ClimbSlide>(s).ways;
                if (r.get<Attraction>(s).open && (upkeep == nullptr || !upkeep->broken) && pool_door_at(world, ways.front().course.points.front()) == errand.target)
                    slides.emplace_back(s, ways.front().course.points.front());
            }
            // And over to the square joining a lazy river's station to the complex, to float round it.
            for (const entt::entity s : sorted(r.view<const River, const Attraction>()))
            {
                const auto* upkeep = r.try_get<Upkeep>(s);
                if (r.get<Attraction>(s).open && (upkeep == nullptr || !upkeep->broken))
                    for (const River::Station& station : r.get<River>(s).stations)
                        if (station.pool == door->pool)
                        {
                            slides.emplace_back(s, station.square);
                            break;
                        }
            }
            std::erase_if(slides, [&](const auto& slide) { return !reach(slide.second); });
            if (!slides.empty() && random::chance(dice, kTuning.pools.slide.value))
            {
                std::tie(swimmer.slide, swimmer.goal) = slides[random::below(dice, slides.size())];
                swimmer.to_spa = false;
                continue;
            }
            // Now and then off one of its diving boards.
            std::vector<std::int32_t> boards;
            for (std::size_t i = 0; i < pool->boards.size(); ++i)
                if (reach(pool->boards[i].foot))
                    boards.push_back(static_cast<std::int32_t>(i));
            if (!boards.empty() && random::chance(dice, kTuning.dives.chance.value))
            {
                swimmer.board = boards[random::below(dice, boards.size())];
                swimmer.goal = pool->boards[static_cast<std::size_t>(swimmer.board)].foot;
                swimmer.to_spa = false;
                continue;
            }
            // Out on the paving they mostly walk on along it, standing little, before going back in.
            const bool out = !pool->paved.empty() && random::chance(dice, (swimmer.in_water ? kTuning.pools.poolside : kTuning.pools.stay_out).value);
            if (out)
                swimmer.idle.value = static_cast<float>(random::between(dice, 0.0, double { kTuning.pools.deck_rest.value }));
            if (out == swimmer.in_water && !pool->ladders.empty() && random::chance(dice, kTuning.dives.ladder.value))
                if (const auto which = static_cast<std::int32_t>(random::below(dice, pool->ladders.size())); reach(pool->ladders[static_cast<std::size_t>(which)].water) && ladders_taken.insert({ door->pool, which }).second)
                {
                    swimmer.ladder = which;
                    const PoolLadder& ladder = pool->ladders[static_cast<std::size_t>(which)];
                    swimmer.goal = swimmer.in_water ? ladder.water : ladder.deck;
                    swimmer.to_spa = false;
                    continue;
                }
            const bool extra = out && pool->spa.size() + pool->extras.size() > 0 && random::chance(dice, kTuning.pools.extra.value);
            const auto spot_at = [&](std::size_t i) { return extra ? (i < pool->spa.size() ? pool->spa[i] : pool->extras[i - pool->spa.size()].at) : out ? pool->paved[i] : pool->water[i]; };
            std::vector<std::size_t> spots;
            for (std::size_t i = 0; i < (extra ? pool->spa.size() + pool->extras.size() : out ? pool->paved.size() : pool->water.size()); ++i)
                if (reach(spot_at(i)))
                    spots.push_back(i);
            if (spots.empty())
                continue;
            std::size_t pick = spots[random::below(dice, spots.size())];
            swimmer.to_spa = extra && pick < pool->spa.size();
            if (extra)
            {
                swimmer.goal = spot_at(pick);
                swimmer.spot = swimmer.to_spa ? -1 : static_cast<std::int32_t>(pick - pool->spa.size());
            }
            else
            {
                // Where two of a square's lanes cross; in the water, a few tries for one clear of the paving round it,
                // as a curved corner's square has only its open corner.
                const auto lane = [&] { return (static_cast<float>(random::below(dice, 3)) - 1.0f) * kTuning.pools.lanes; };
                const auto near = [&](const glm::vec3& middle) { return middle + glm::vec3 { lane(), 0, lane() }; };
                swimmer.goal = near(spot_at(pick));
                for (std::size_t tries = 0; !out && tries < 8 && !open_water(*pool, pick, swimmer.goal, paths.tile_size); ++tries)
                {
                    pick = spots[random::below(dice, spots.size())];
                    swimmer.goal = near(spot_at(pick));
                }
            }
        }

        phase.emplace("Rivers");
        float_on_rivers(world, dt);

        phase.emplace("Climbs");
        climb_slides(world, dt);
    }

} // namespace rct3::simulation
