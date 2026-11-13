// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/rides.hpp"

#include "profiling/profiler.hpp"
#include "simulation/animals.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/guests.hpp"
#include "simulation/holding.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    namespace
    {
        /// What a guest does once let go by a ride: a party member waits for the rest, others wander on.
        Errand after_ride(const entt::registry& r, entt::entity guest, entt::entity ride)
        {
            // Out of a pool slide's queue, a swimmer swims on.
            if (const auto* swimmer = r.try_get<Swimmer>(guest); swimmer != nullptr && r.valid(swimmer->room))
                return { .kind = ErrandKind::Swimming, .target = swimmer->room };
            if (r.all_of<Member>(guest))
            {
                const Errand& was = r.get<Errand>(guest);
                return { .kind = ErrandKind::Waiting, .target = ride, .station = was.station, .off = was.off };
            }
            return { ErrandKind::Wandering, entt::null, ticks(kTuning.needs.idle.value) };
        }

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

    float Upkeep::downtime_share() const
    {
        int sum = 0;
        for (const std::uint8_t w : downtime)
            sum += w;
        return static_cast<float>(sum) / static_cast<float>(downtime.size() * kDowntimeSamples);
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

    bool departs(const Departure& departure, std::size_t riders, std::size_t capacity, float waited)
    {
        if (departure.min_wait && waited < departure.min_wait->value)
            return false;
        if (departure.max_wait && waited >= departure.max_wait->value)
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

    void step_rides(SimWorld& world, const Weather& weather, bool night, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;
        const float now = static_cast<float>(world.tick) * kTickSeconds;

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
                    if (r.valid(guest) && (r.get<Errand>(guest).queued.value += dt) >= kLongQueue.value)
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
                            // Thrown onto hard ground, not into water: a dangerous happening, the slide is shut as
                            // dangerous, and the park's inspector, if it has one, says so.
                            if (pool != nullptr && !pool->water.empty())
                                continue;
                            ++r.ctx().emplace<PoolDangers>().landings;
                            if (attraction.open)
                                for (const auto [i, staff] : r.view<const Staff>().each())
                                    if (staff.role == StaffRole::Inspector)
                                    {
                                        tell(r, i, Finding::DangerousFlume, e, now);
                                        break;
                                    }
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
            if (ride.since_departure)
                ride.since_departure->value += dt;
            const auto depart = [&](std::size_t riding, units::Seconds& timer) {
                if (ride.since_departure && ride.since_departure->value > 0)
                {
                    const float rate = static_cast<float>(riding) * 3600 / ride.since_departure->value;
                    ride.per_hour = ride.per_hour > 0 ? std::lerp(ride.per_hour, rate, kTuning.wear.flow_smoothing.value) : rate;
                }
                ride.since_departure = units::Seconds { };
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
                if (wrecks->since && (wrecks->since->value += dt) >= crashes.forgotten.value)
                    wrecks->since.reset();
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
                        train.slow = { };
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
                        train.flight = Flight { .from = at, .way = way, .at = at, .velocity = way * train.speed, .water = std::min(water, at.y), .speed = { train.speed } };
                        // A pool's far wall stops it, its middle half a car short of it, however far it would fly.
                        constexpr float kFar = 100;   ///< Metres on it looks for the wall. Chosen by eye: past any slide's throw
                        constexpr float kHalfCar = 1; ///< Metres from a slide car's middle to its nose. Chosen by eye
                        if (const glm::vec2 level { way.x, way.z }; pool != nullptr && glm::length(level) > 1e-3f)
                        {
                            const glm::vec2 from { at.x, at.z };
                            const glm::vec2 far = from + glm::normalize(level) * kFar;
                            if (const glm::vec2 stop = pool_stop(*pool, from, far, world.paths.tile_size); stop != far)
                                train.flight->reach.value = std::max(glm::distance(from, stop) - kHalfCar, 0.0f);
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
                        wrecks->since = units::Seconds { };
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
                units::Seconds& timer = station == 0 ? ride.loading : ride.stops[station - 1].loading;
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
                        const units::Money price = r.all_of<Swimmer>(guest) ? units::Money { } : attraction.price;
                        if (!attraction.open || (!inspector && !pay(r, guest, price, &Wallet::spent_rides, park::LedgerLine::RideTickets)))
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
                        else if (r.get<Errand>(guest).queued.value >= kLongQueue.value)
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
            if (fleet != nullptr && standing[0] && leaving != nullptr && leaving->leave_on_arrival && (!leaving->min_wait || ride.loading.value >= leaving->min_wait->value) && !fleet->trains[*standing[0]].running && !upkeep.broken && std::ranges::none_of(fleet->riders[*standing[0]], on_their_way))
            {
                for (std::size_t t = 0; t < fleet->trains.size(); ++t)
                    if (t != *standing[0] && fleet->trains[t].running && room_ahead(fleet->trains, t) < 0.01f)
                    {
                        fleet->trains[*standing[0]].running = true;
                        depart(fleet->riders[*standing[0]].size(), ride.loading);
                    }
            }
        }
    }

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

    RideReaction react(RideReaction now, const Felt& feel, bool running, bool lifted, units::Share happiness, float over, const Leanings& leanings, bool front, random::RandomState& dice, float dt)
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

    RideReaction react_flat(RideReaction now, bool running, units::Share happiness, float over, const Leanings& leanings, random::RandomState& dice, float dt)
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

    float felt_excitement(float excitement, bool coaster, AgeGroup age, bool night)
    {
        if (!coaster || !night)
            return excitement;
        return excitement * (age == AgeGroup::Teen ? 1 + kTuning.night.thrill.value : 1 - kTuning.night.dread.value);
    }

} // namespace rct3::simulation
