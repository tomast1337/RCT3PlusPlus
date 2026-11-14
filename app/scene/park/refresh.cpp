// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/refresh.hpp"

#include "logging/log.hpp"
#include "park/chain_lifts.hpp"
#include "park/object/models.hpp"
#include "park/object/pick.hpp"
#include "park/person/motions.hpp"
#include "park/ride/cars.hpp"
#include "park/water/flow.hpp"
#include "profiling/profiler.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/people.hpp"
#include "scene/park/poses.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_park.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"
#include "ui/scale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <future>
#include <inplace_vector>
#include <map>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {

        /// Starts rings spreading on the pool water at `at` in `delay` seconds, where someone falls in with
        /// `strength` (rct3::park::pools::entry_rings). When there are already the most, they take the place of the lowest, if
        /// those are lower than the new ones start.
        void start_rings(ParkScene& scene, glm::vec3 at, float strength, float delay = 0)
        {
            constexpr std::size_t kMostRings = rct3::park::pools::kMaxPoolWaves / 2; ///< Chosen by eye: room left for the machines' and drawn waves
            const auto rings = rct3::park::pools::entry_rings(scene.pools, scene.grid, at, strength, scene.park_clock + delay);
            if (!rings)
                return;
            if (scene.rings.size() < kMostRings)
            {
                scene.rings.push_back(*rings);
                return;
            }
            const auto crest = [&](const rct3::park::pools::PoolWave& w) { return w.start > scene.park_clock ? w.strength * rct3::park::pools::kPoolWaveLook.ring_height : rct3::park::pools::ring_crest(w, scene.park_clock); };
            if (const auto lowest = std::ranges::min_element(scene.rings, { }, crest); crest(*lowest) < rings->strength * rct3::park::pools::kPoolWaveLook.ring_height)
                *lowest = *rings;
        }

        /// How far away people are drawn at each level of detail, finest first; beyond the last they are not.
        constexpr std::array kPersonDistances { 75.0f, 200.0f, 600.0f };

        /// How far away scenery moving on its own is seen to move; beyond, it keeps its pose.
        constexpr float kPropMoveDistance = 200.0f;

        constexpr std::size_t kNoCar = static_cast<std::size_t>(-1);

        /// Where rider carrier `k` (see ParkScene::rider_cars) stands now.
        glm::mat4 carrier_place(const ParkScene& scene, std::size_t k)
        {
            if (k == kNoCar)
                return glm::mat4(1.0f);
            if (k < scene.car_places.size())
            {
                const auto arm = std::ranges::find(scene.arm_cars, k, &ParkScene::ArmCar::car);
                return arm != scene.arm_cars.end() ? scene.car_places[k] * arm->seats : scene.car_places[k];
            }
            k -= scene.cars.size();
            return k < scene.seat_carriers.size() ? scene.seat_carriers[k] : glm::mat4(1.0f);
        }

        /// Where simulated guest `guest` sits while riding: the car, by its place in `cars`, and the seat in
        /// the car's own space that their place among the riders comes to, counting the train's seats front
        /// to back. On a ride without cars the car is kNoCar and the seat is in the park, on the ride as it is
        /// drawn. Nothing when not riding, or when the ride has no seat for them.
        /// shortcut: flat rides are drawn standing still, so their riders sit still too, upgrade when the
        /// rides' motions are played.
        std::optional<std::pair<std::size_t, glm::mat4>> seat_of(const ParkScene& scene, entt::entity guest)
        {
            const auto& r = scene.sim.registry;
            const auto* walking = r.try_get<simulation::Errand>(guest); // None once gone home
            if (walking == nullptr || walking->kind != simulation::ErrandKind::Riding || !r.all_of<simulation::CycleRide>(walking->target) || r.all_of<simulation::RideWalk>(guest))
                return std::nullopt;
            const simulation::Errand& errand = *walking;
            // On a ride with trains, the train the guest boarded and their place among its riders.
            const auto* fleet = r.try_get<simulation::Trains>(errand.target);
            std::size_t train = 0;
            const std::vector<entt::entity>* riders = &r.get<simulation::CycleRide>(errand.target).riders;
            if (fleet != nullptr)
                for (std::size_t t = 0; t < fleet->riders.size(); ++t)
                    if (std::ranges::contains(fleet->riders[t], guest))
                    {
                        train = t;
                        riders = &fleet->riders[t];
                    }
            auto seat = static_cast<std::size_t>(std::ranges::find(*riders, guest) - riders->begin());
            if (const auto object = scene.ride_seats.find(errand.target); object != scene.ride_seats.end())
            {
                const save::model::PlacedObject& o = scene.objects[object->second];
                const auto model = scene.models.find(rct3::park::object::model_ref(o));
                if (model == scene.models.end() || seat >= model->second.seat_places.size())
                    return std::nullopt;
                // On a ride its bones move, the seat's bone carries the seat.
                if (const auto boned = std::ranges::find(scene.boned_rides, object->second, &ParkScene::BonedRide::object); boned != scene.boned_rides.end() && seat < boned->seats)
                    return std::pair { scene.cars.size() + boned->first_seat + seat, model->second.seat_places[seat] };
                return std::pair { kNoCar, rct3::park::object::placed_transform(o, &model->second, scene.grid, scene.fitted) * model->second.seat_places[seat] };
            }
            const auto cars = scene.ride_cars.find(errand.target);
            if (cars == scene.ride_cars.end())
                return std::nullopt;
            for (const std::size_t car : cars->second)
            {
                if (fleet != nullptr && scene.car_spots[car].train != train)
                    continue;
                const auto model = scene.models.find(rct3::park::object::model_ref(scene.cars[car]));
                const std::size_t seats = model != scene.models.end() ? model->second.seat_places.size() : 0;
                if (seat < seats)
                    return std::pair { car, model->second.seat_places[seat] };
                seat -= seats;
            }
            return std::nullopt;
        }

        /// Where simulated guest `guest` sits in the park while riding, as seat_of finds it.
        std::optional<glm::mat4> seat_place(const ParkScene& scene, entt::entity guest)
        {
            if (const auto seat = seat_of(scene, guest))
                return seat->first == kNoCar ? seat->second : carrier_place(scene, seat->first) * seat->second;
            return std::nullopt;
        }

        /// How riders sit in a car, as the game's motions name it.
        const std::int32_t kRideMotion = rct3::park::person::motion_number("ridecar").value_or(rct3::park::person::motion::kIdle);

        /// Places the people near enough to see that stand still, or those that move and may be in `view`, as
        /// they are at `clock`, at `levels`. With `simulate`, the simulated guests count as moving and stand
        /// where the simulation has them. The still ones read only what is set when the park loads besides the
        /// arguments, so they may be placed beside the frame; the moving ones only on it.
        std::vector<rct3::park::person::PlacedPerson> place_people(const ParkScene& scene, const rct3::park::object::ObjectLevels& levels, float clock, bool moving, const glm::mat4& view, bool simulate)
        {
            std::vector<rct3::park::person::PlacedPerson> placed;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                if (levels[i] == rct3::park::object::kNotDrawn || ((simulate && shown.guest != entt::null) || !shown.walking) != moving)
                    continue;
                std::optional<rct3::park::person::PlacedPerson> pose = pose_person(scene, i, clock, simulate);
                if (!pose || (moving && !maybe_in_view(view, glm::vec3(pose->world[3]))))
                    continue;
                pose->level = static_cast<std::size_t>(levels[i]);
                placed.push_back(*pose);
                // A lazy river rider's ring round them at the water line, turning with them. The ring slide's riders
                // sit lying back, which a rider treading water does not fit, so they tread water in it. One with a
                // float of their own rides that instead.
                if (scene.swim_ring && shown.guest != entt::null && scene.sim.registry.all_of<simulation::Floating>(shown.guest) && !own_float(*scene.person_models, *pose))
                    placed.push_back({ .world = pose->world, .held = &*scene.swim_ring });
            }
            // Litter and sick lying on the paths, with what moves: each piece somewhere about its tile, turned its
            // own way.
            if (moving && simulate)
            {
                constexpr float kSpread = 1.2f; ///< Chosen by eye: farthest a piece lies from its tile's middle, in world units
                for (const auto [e, piece] : scene.sim.registry.view<const simulation::Litter>().each())
                {
                    const auto& model = scene.litter_looks[piece.vomit ? 1 : 0];
                    if (!model || piece.node >= scene.sim.paths.nodes().size())
                        continue;
                    const auto seed = static_cast<std::uint32_t>(entt::to_integral(e)) * 2654435761u;
                    const glm::vec2 off = (glm::vec2(seed & 0xffu, seed >> 8 & 0xffu) / 255.0f * 2.0f - 1.0f) * kSpread;
                    const glm::vec3 at = scene.sim.paths.centre(piece.node) + glm::vec3(off.x, 0.0f, off.y);
                    if (!maybe_in_view(view, at))
                        continue;
                    placed.push_back({ .world = glm::rotate(glm::translate(glm::mat4(1.0f), at), static_cast<float>(seed >> 16 & 0xffu) / 255.0f * 2.0f * std::numbers::pi_v<float>, glm::vec3(0.0f, 1.0f, 0.0f)), .held = &*model });
                }
            }
            return placed;
        }

        /// Starts posing the riders seated in the ride cars away from the frame, each car's in the car's own
        /// space; place_cars moves them with their cars. Riders drawn at no level of detail are left out.
        std::future<RidersBuild> pose_riders(ParkScene& scene)
        {
            std::map<std::size_t, std::vector<rct3::park::person::PlacedPerson>> by_car;
            std::map<entt::entity, ParkScene::RiderTurn> turns;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < scene.people_levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                if (shown.guest == entt::null || scene.people_levels[i] == rct3::park::object::kNotDrawn)
                    continue;
                const auto seat = seat_of(scene, shown.guest);
                if (!seat)
                    continue;
                rct3::park::person::PlacedPerson pose = dressed(shown);
                pose.level = static_cast<std::size_t>(scene.people_levels[i]);
                pose.time = shown.saved_time + scene.park_clock * shown.speed;
                pose.world = seat->second;
                pose.motion = kRideMotion;
                // They sit as their car seats them, or on a flat ride as the ride does, reacting to the ride.
                std::optional<std::uint8_t> seating;
                if (seat->first < scene.cars.size())
                {
                    if (const auto model = scene.models.find(rct3::park::object::model_ref(scene.cars[seat->first])); model != scene.models.end())
                        seating = model->second.seating;
                }
                else if (const auto flat = scene.ride_seatings.find(scene.sim.registry.get<simulation::Errand>(shown.guest).target); flat != scene.ride_seatings.end())
                    seating = flat->second;
                if (const auto motions = seating ? rct3::park::person::rider_motions(*seating) : std::nullopt)
                {
                    using simulation::RideReaction;
                    const auto* thrill = scene.sim.registry.try_get<simulation::Thrill>(shown.guest);
                    const RideReaction now = thrill != nullptr ? thrill->reaction : RideReaction::Calm;
                    // Riders not posed last time show no change.
                    const auto last = scene.rider_turns.find(shown.guest);
                    auto& turn = turns[shown.guest] = last != scene.rider_turns.end() ? last->second : ParkScene::RiderTurn { now, now, -1e9f };
                    if (turn.now != now)
                        turn = { turn.now, now, scene.park_clock };
                    // Bringing down their hands or their arms from their eyes, then raising them, then the loop.
                    std::inplace_vector<rct3::park::person::Clip, 3> clips;
                    const auto add = [&](RideReaction reaction, const std::optional<std::int32_t>& hands, const std::optional<std::int32_t>& eyes) {
                        if (const auto& once = reaction == RideReaction::HandsUp ? hands : reaction == RideReaction::CoverEyes ? eyes : std::nullopt; once && turn.was != turn.now)
                        {
                            pose.motion = *once;
                            clips.push_back({ *once, rct3::park::person::motion_seconds(*scene.person_models, pose) });
                        }
                    };
                    add(turn.was, motions->hands_up_out, motions->cover_eyes_out);
                    add(turn.now, motions->hands_up_in, motions->cover_eyes_in);
                    // In RideReaction's order.
                    const std::array by_reaction { motions->wait, motions->ride, motions->hands_up, motions->cover_eyes, motions->strained };
                    clips.push_back({ by_reaction[std::to_underlying(now)], 0 });
                    pose.motion = clips.back().motion;
                    if (clips.size() > 1)
                    {
                        const rct3::park::person::Clip at = rct3::park::person::clip_at(clips, scene.park_clock - turn.since);
                        pose.motion = at.motion;
                        pose.time = at.seconds;
                    }
                }
                by_car[seat->first].push_back(pose);
            }
            scene.rider_turns = std::move(turns);
            return std::async(std::launch::async, [&library = *scene.person_models, &runs = scene.rider_runs, &gathered = scene.rider_gathered, by_car = std::move(by_car)] {
                std::vector<std::span<const rct3::park::person::PlacedPerson>> groups;
                RidersBuild built;
                for (const auto& [car, riders] : by_car)
                {
                    groups.emplace_back(riders);
                    built.cars.push_back(car);
                }
                built.riders = rct3::park::person::carried_people_meshes(library, groups, runs, gathered);
                return built;
            });
        }

        constexpr float kGateHold = 3;                             ///< How long a ride's gate stays open after its last rider walks through
        constexpr std::string_view kCannonSplash = "paddlesplash"; ///< Chosen by eye: the effect where a cannon's water lands

        /// Turns and moves the bones of the boned flat rides as far into their motions as their rides are, and
        /// the seats they carry.
        void swing_rides(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view)
        {
            for (ParkScene::BonedRide& boned : scene.boned_rides)
            {
                const glm::vec3 at(boned.placed[3]);
                if ((boned.ride == entt::null || boned.gate) && (glm::distance(eye, at) > kPropMoveDistance || !maybe_in_view(view, at, boned.reach)))
                    continue;
                const auto model = scene.models.find(rct3::park::object::model_ref(scene.objects[boned.object]));
                if (model == scene.models.end() || !model->second.rig)
                    continue;
                const auto& seconds = model->second.animation_seconds;
                std::pair<std::size_t, float> playing;
                if (boned.gate)
                {
                    // Kept open while the ride's riders walk through, on or off as the gate is, and a while after so
                    // it does not flap between them; it changes motion only once the playing one ends.
                    if (scene.simulate)
                        for (const auto [g, walk, errand] : scene.sim.registry.view<const simulation::RideWalk, const simulation::Errand>().each())
                            if (errand.target == boned.ride && walk.off == boned.exit)
                                boned.open_until = scene.park_clock + kGateHold;
                    const std::size_t now = (*boned.gate)[boned.step];
                    if (scene.park_clock - boned.since >= (now < seconds.size() ? seconds[now] : 0.0f))
                    {
                        boned.step = rct3::park::next_gate_motion(boned.step, scene.park_clock < boned.open_until);
                        boned.since = scene.park_clock;
                    }
                    playing = { (*boned.gate)[boned.step], scene.park_clock - boned.since };
                }
                else
                {
                    const auto* cycle = scene.simulate && boned.ride != entt::null ? scene.sim.registry.try_get<simulation::CycleRide>(boned.ride) : nullptr;
                    const auto into = cycle != nullptr ? simulation::seconds_into_cycle(*cycle) : std::nullopt;
                    const float length = cycle != nullptr ? simulation::cycle_seconds(*cycle) : 0.0f;
                    // Scenery and stalls carry on from where the save left them, each at its own point in its loop.
                    const float idle = scene.park_clock + (boned.ride == entt::null ? scene.objects[boned.object].motion_time.value : 0.0f);
                    playing = rct3::park::flat_ride_motion(seconds, length, into ? std::min(*into + scene.sim_time, length) : 0.0f, into.has_value(), idle);
                }
                const auto [motion, time] = playing;
                std::vector<glm::mat4> moves = rct3::park::bone_moves(*model->second.rig, motion, time);
                // A water cannon a guest fires turns its gun to the guest they spray, or sweeps it about with no
                // one in reach, and shoots water from its nozzle.
                if (const auto& aim = model->second.aim_bones; aim && scene.simulate)
                {
                    const auto& r = scene.sim.registry;
                    const std::uint64_t id = scene.objects[boned.object].id;
                    std::optional<const simulation::Linger&> firing;
                    for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                        if (item.object == id)
                            for (const auto [g, linger] : r.view<const simulation::Linger>().each())
                                if (linger.at == simulation::PathItemKind::WaterCannon && linger.bench == e)
                                    firing = linger;
                    auto jet = std::ranges::find(scene.jets, boned.object, &ParkScene::Jet::object);
                    if (firing && jet == scene.jets.end())
                        jet = scene.jets.insert(scene.jets.end(), ParkScene::Jet { .object = boned.object });
                    if (jet != scene.jets.end())
                    {
                        const rct3::park::BoneRig& rig = *model->second.rig;
                        const auto [turret, nozzle] = *aim;
                        const glm::vec3 pivot(rig.rest_world[static_cast<std::size_t>(turret)][3]);
                        const glm::vec3 muzzle(rig.rest_world[static_cast<std::size_t>(nozzle)][3]);
                        const glm::vec3 ahead = glm::normalize(glm::vec3(muzzle.x - pivot.x, 0, muzzle.z - pivot.z));
                        constexpr float kSpeed = 11;     ///< Chosen by eye: metres a second the water leaves at
                        constexpr float kTurn = 2.5f;    ///< Chosen by eye: radians a second the gun turns
                        constexpr float kHighest = 0.8f; ///< Chosen by eye: most the gun tips up, and down
                        constexpr float kLowest = -0.35f;
                        const float swing = simulation::kTuning.surroundings.cannon_swing.value;
                        float yaw = 0.6f * swing * std::sin(scene.park_clock * 0.5f + static_cast<float>(boned.object)); // Sweeping about
                        float pitch = 0.35f;
                        float ground = boned.placed[3].y;
                        if (firing && r.valid(firing->target) && r.all_of<simulation::Pose>(firing->target))
                        {
                            const glm::vec3 at = simulation::between_steps(r.get<simulation::Pose>(firing->target), scene.sim_time / simulation::kTickSeconds).position;
                            ground = at.y;
                            const glm::vec3 local = glm::vec3(glm::inverse(boned.placed) * glm::vec4(at + glm::vec3(0, 1, 0), 1)) - muzzle;
                            const float across = std::hypot(local.x, local.z);
                            // One out of its swing, as they walk round it, is let be until the firer picks another.
                            if (const auto turn = simulation::cannon_turn(std::atan2(ahead.x, ahead.z), { local.x, local.z }, swing); turn && across > 0.5f)
                            {
                                yaw = *turn;
                                // The lower of the two arcs that land on them, else the farthest throw.
                                const float g = rct3::park::water::kJetGravity;
                                const float v2 = kSpeed * kSpeed;
                                const float room = v2 * v2 - g * (g * across * across + 2 * local.y * v2);
                                pitch = room >= 0 ? std::atan((v2 - std::sqrt(room)) / (g * across)) : std::numbers::pi_v<float> / 4;
                            }
                        }
                        // Let go, the gun stays as it was left.
                        if (const float seconds = scene.park_clock - scene.jets_clock; firing)
                        {
                            jet->yaw += std::clamp(std::remainder(yaw - jet->yaw, 2 * std::numbers::pi_v<float>), -kTurn * seconds, kTurn * seconds);
                            jet->pitch += std::clamp(std::clamp(pitch, kLowest, kHighest) - jet->pitch, -kTurn * seconds, kTurn * seconds);
                        }
                        const glm::mat4 turn = glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), jet->yaw, glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1.0f), jet->pitch, glm::cross(ahead, glm::vec3(0, 1, 0))) * glm::translate(glm::mat4(1.0f), -pivot);
                        for (std::size_t b = 0; b < moves.size(); ++b)
                            for (std::int32_t up = static_cast<std::int32_t>(b); up >= 0; up = rig.parents[static_cast<std::size_t>(up)])
                                if (up == turret)
                                {
                                    moves[b] = turn * moves[b];
                                    break;
                                }
                        const glm::vec3 way = glm::normalize(glm::mat3(boned.placed) * glm::mat3(turn) * ahead);
                        jet->heading = std::atan2(way.x, way.z);
                        if (firing)
                        {
                            jet->fed = true;
                            jet->water.aim(glm::vec3(boned.placed * moves[static_cast<std::size_t>(nozzle)] * glm::vec4(muzzle, 1)), way * kSpeed);
                        }
                        // Spray where it lands.
                        // shortcut: it lands on a level at the sprayed guest's feet, or the cannon's; upgrade with a ground test.
                        if (const auto land = jet->water.landing([&](glm::vec2) { return ground; }); land && scene.park_clock - jet->splashed > 0.15f)
                        {
                            // One splash where it lands and two round it, as wide as the jet is there, turning each time.
                            constexpr float kSplashWide = 0.5f; ///< Chosen by eye
                            for (int s = 0; s < 3; ++s)
                            {
                                const float round = scene.park_clock * 5 + static_cast<float>(s) * 2.1f;
                                fire_park_particles(scene.particles, kCannonSplash, *land + (s == 0 ? glm::vec3(0) : glm::vec3(std::cos(round), 0, std::sin(round)) * kSplashWide), glm::vec3(0, 1, 0));
                            }
                            jet->splashed = scene.park_clock;
                        }
                    }
                }
                std::vector<glm::mat4> places(boned.bones.size());
                for (std::size_t p = 0; p < places.size(); ++p)
                    places[p] = boned.placed * moves[static_cast<std::size_t>(boned.bones[p])];
                rct3::park::move_meshes(boned.mesh, places);
                const auto& seat_bones = model->second.seat_bones;
                for (std::size_t seat = 0; seat < std::min(boned.seats, seat_bones.size()); ++seat)
                    if (seat_bones[seat] >= 0 && static_cast<std::size_t>(seat_bones[seat]) < moves.size())
                        scene.seat_carriers[boned.first_seat + seat] = boned.placed * moves[static_cast<std::size_t>(seat_bones[seat])];
                const auto& view_bones = model->second.view_bones;
                for (std::size_t view = 0; view < std::min(boned.views.size(), view_bones.size()); ++view)
                    if (view_bones[view] >= 0 && static_cast<std::size_t>(view_bones[view]) < moves.size())
                        boned.views[view] = moves[static_cast<std::size_t>(view_bones[view])];
            }
            // Jumping fountains on neighbouring path tiles throw water from each nozzle into the same nozzle of the
            // next, which throws on to the one after as it lands, along the row. With no jump in the air for a
            // while, the fountains at the ends of rows start again.
            // shortcut: every row in the park keeps one beat, and a ring of fountains with no end never starts;
            // upgrade with patterns per row.
            if (scene.simulate)
            {
                constexpr float kJumpAngle = 1.0f; ///< Chosen by eye: radians above the level a jump leaves at
                constexpr float kPour = 0.4f;      ///< Chosen by eye: seconds of water in each jump
                constexpr float kRest = 1.5f;      ///< Chosen by eye: seconds between the last jump landing and the next
                const auto& r = scene.sim.registry;
                const auto& nodes = scene.sim.paths.nodes();
                // The jumping fountains next to the one at `node`.
                const auto next_to = [&](simulation::NodeId node) {
                    std::vector<simulation::NodeId> out;
                    for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                        if (item.kind == simulation::PathItemKind::Fountain && node < nodes.size() && std::ranges::contains(nodes[node].next, item.node))
                            out.push_back(item.node);
                    return out;
                };
                const auto fountain_at = [&](simulation::NodeId node) -> std::optional<std::size_t> {
                    for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                        if (item.kind == simulation::PathItemKind::Fountain && item.node == node)
                            if (const auto k = std::ranges::find(scene.objects, item.object, &save::model::PlacedObject::id); k != scene.objects.end())
                                return static_cast<std::size_t>(k - scene.objects.begin());
                    return std::nullopt;
                };
                const auto node_of = [&](std::size_t object) {
                    for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                        if (item.object == scene.objects[object].id)
                            return item.node;
                    return simulation::kNoNode;
                };
                const auto jump = [&](std::size_t from, std::size_t to) {
                    const auto a = scene.models.find(rct3::park::object::model_ref(scene.objects[from]));
                    const auto b = scene.models.find(rct3::park::object::model_ref(scene.objects[to]));
                    if (a == scene.models.end() || b == scene.models.end() || b->second.nozzles.empty())
                        return;
                    const glm::mat4 at = rct3::park::object::placed_transform(scene.objects[from], &a->second, scene.grid, scene.fitted);
                    const glm::mat4 into = rct3::park::object::placed_transform(scene.objects[to], &b->second, scene.grid, scene.fitted);
                    const glm::vec3 step = glm::vec3(into[3]) - glm::vec3(at[3]);
                    for (const glm::vec3& nozzle : a->second.nozzles)
                    {
                        const glm::vec3 out(at * glm::vec4(nozzle, 1));
                        const auto landing = std::ranges::min(b->second.nozzles | std::views::transform([&](const glm::vec3& n) { return glm::vec3(into * glm::vec4(n, 1)); }), { }, [&](const glm::vec3& n) { return glm::distance(n, out + step); });
                        if (const auto thrown = rct3::park::water::throw_to(out, landing, kJumpAngle))
                        {
                            // Chosen by eye: a thin, even stream.
                            ParkScene::Jet& jet = scene.jets.emplace_back(ParkScene::Jet { .object = from, .to = to, .pours = scene.park_clock + kPour, .lands = scene.park_clock + thrown->seconds + kPour, .water = rct3::park::water::WaterJet(thrown->seconds + kPour, 0.04f, 0) });
                            jet.water.aim(out, thrown->velocity);
                        }
                    }
                };
                // Landed: the fountain landed in throws on, away from where the water came from.
                std::vector<std::pair<std::size_t, std::size_t>> landed;
                for (ParkScene::Jet& jet : scene.jets)
                    if (jet.to && !jet.landed && scene.park_clock >= jet.lands)
                    {
                        jet.landed = true;
                        if (!std::ranges::contains(landed, std::pair { jet.object, *jet.to }))
                            landed.emplace_back(jet.object, *jet.to);
                    }
                for (const auto& [from, to] : landed)
                    for (const simulation::NodeId n : next_to(node_of(to)))
                        if (const auto onto = fountain_at(n); onto && *onto != from)
                            jump(to, *onto);
                if (std::ranges::any_of(scene.jets, [](const ParkScene::Jet& jet) { return jet.to && !jet.landed; }))
                    scene.jumps_ended = scene.park_clock;
                else if (scene.park_clock - scene.jumps_ended > kRest)
                {
                    scene.jumps_ended = scene.park_clock;
                    for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                        if (const auto near = next_to(item.node); item.kind == simulation::PathItemKind::Fountain && near.size() == 1)
                            if (const auto from = fountain_at(item.node), to = fountain_at(near.front()); from && to)
                                jump(*from, *to);
                }
                for (ParkScene::Jet& jet : scene.jets)
                    if (jet.to && scene.park_clock < jet.pours)
                        jet.fed = true;
            }
            // Jets not aimed this frame stop, their water falling on; the spent ones go.
            const float seconds = scene.park_clock - scene.jets_clock;
            scene.jets_clock = scene.park_clock;
            for (ParkScene::Jet& jet : scene.jets)
            {
                if (!jet.fed)
                    jet.water.stop();
                jet.fed = false;
                jet.water.advance(seconds);
            }
            // A cannon keeps its own, to stay turned as it was left.
            std::erase_if(scene.jets, [&](const ParkScene::Jet& jet) { return jet.water.finished() && (jet.to || !std::ranges::contains(scene.boned_rides, jet.object, &ParkScene::BonedRide::object)); });
        }

        /// The band and side of each of `owners`' vertices: `side` 0 for objects drawn whole.
        std::vector<std::pair<glm::vec4, glm::vec2>> crossing_bands(const ParkScene& scene, const rct3::park::object::LevelPlan& plan, std::span<const std::uint32_t> owners, float side)
        {
            std::vector<std::pair<glm::vec4, glm::vec2>> out;
            out.reserve(owners.size());
            for (const std::uint32_t o : owners)
                out.push_back({ glm::vec4(scene.anchors[o], plan.bands[o].x), glm::vec2(plan.bands[o].y, side) });
            return out;
        }

        /// Puts `shapes` in `shown`'s buffers, dropping what they held.
        void show_shapes(render::Device& device, ParkScene& scene, ShownObjects& shown, ObjectShapes&& shapes, const rct3::park::object::LevelPlan& plan, float side = 0)
        {
            for (const render::BufferHandle b : { shown.vertices, shown.indices })
            {
                if (b)
                    device.destroy_buffer(b);
            }
            shown = { .batches = shapes.mesh.batches, .billboards = std::move(shapes.billboards) };
            if (side != 0)
                shown.bands = crossing_bands(scene, plan, shown.billboards.mesh.owners, side);
            profiling::section("Textures", [&] {
                add_textures(device, scene, shapes.mesh.batches);
                add_textures(device, scene, shown.billboards.mesh.batches);
            });
            if (shapes.mesh.indices.empty())
                return;
            const profiling::Scope zone("Upload");
            auto vertices = side != 0 ? upload(device, render::BufferUsage::Vertex, crossing_vertices(shapes.mesh.vertices, crossing_bands(scene, plan, shapes.mesh.owners, side)), "crossing object vertices") : upload(device, render::BufferUsage::Vertex, shapes.mesh.vertices, "object vertices");
            auto indices = upload(device, render::BufferUsage::Index, shapes.mesh.indices, "object indices");
            if (!vertices || !indices)
            {
                logging::logger("park").warning("object buffers: {}", !vertices ? vertices.error() : indices.error());
                if (vertices)
                    device.destroy_buffer(*vertices);
                if (indices)
                    device.destroy_buffer(*indices);
                shown.batches.clear();
                return;
            }
            shown.vertices = *vertices;
            shown.indices = *indices;
        }

    } // namespace

    bool maybe_in_view(const glm::mat4& transform, const glm::vec3& at, float reach)
    {
        constexpr float kSpare = 1.3f;
        const glm::vec4 clip = transform * glm::vec4(at + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
        const float edge = clip.w * kSpare + reach;
        return clip.w > -reach && std::abs(clip.x) <= edge && std::abs(clip.y) <= edge;
    }

    glm::mat4 person_place(const ParkScene& scene, const ShownPerson& shown)
    {
        if (scene.simulate && shown.guest != entt::null)
            if (const auto seat = seat_place(scene, shown.guest).or_else([&] { return bench_seat(scene, shown.guest); }))
                return *seat;
        if (scene.simulate && shown.guest != entt::null)
        {
            const auto at = simulation::between_steps(scene.sim.registry.get<simulation::Pose>(shown.guest), scene.sim_time / simulation::kTickSeconds);
            return glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        return shown.pose.world;
    }

    glm::vec3 standing_at(const ParkScene& scene, const ShownPerson& shown)
    {
        return glm::vec3(person_place(scene, shown)[3]);
    }

    /// Moves the ride cars to where the simulation has their trains, or back to where the park kept them
    /// while it does not run.
    std::optional<glm::mat4> ride_eye(const ParkScene& scene, std::size_t car, rct3::park::object::RideEye eye)
    {
        const auto model = car < scene.cars.size() ? scene.models.find(rct3::park::object::model_ref(scene.cars[car])) : scene.models.end();
        if (model == scene.models.end() || car >= scene.car_spots.size())
            return std::nullopt;
        const rct3::park::ride::CarSpot spot = scene.car_spots[car];
        if (eye == rct3::park::object::RideEye::Nose && std::ranges::any_of(std::span(scene.car_spots).first(car), [&](const rct3::park::ride::CarSpot& s) { return s.ride == spot.ride && s.train == spot.train; }))
            return std::nullopt;
        return model->second.ride_eyes[std::to_underlying(eye)];
    }

    std::optional<glm::mat4> flat_view(const ParkScene& scene, std::size_t ride, bool fixed, std::size_t at)
    {
        const auto object = scene.ride_seats.find(ride < scene.ride_entities.size() ? scene.ride_entities[ride] : entt::null);
        if (object == scene.ride_seats.end())
            return std::nullopt;
        const save::model::PlacedObject& o = scene.objects[object->second];
        const auto model = scene.models.find(rct3::park::object::model_ref(o));
        const std::vector<glm::mat4>* places = model == scene.models.end() ? nullptr : fixed ? &model->second.view_places : &model->second.seat_places;
        if (places == nullptr || at >= places->size())
            return std::nullopt;
        const auto boned = std::ranges::find(scene.boned_rides, object->second, &ParkScene::BonedRide::object);
        if (boned != scene.boned_rides.end() && fixed && at < boned->views.size())
            return boned->placed * boned->views[at] * (*places)[at];
        if (boned != scene.boned_rides.end() && !fixed && at < boned->seats)
            return scene.seat_carriers[boned->first_seat + at] * (*places)[at];
        return rct3::park::object::placed_transform(o, &model->second, scene.grid, scene.fitted) * (*places)[at];
    }

    void place_cars(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, units::Seconds elapsed)
    {
        const float seconds = elapsed.value;
        // Chosen by eye: the effects a car's splash points spray, by strength, and its wake.
        constexpr std::array<std::string_view, 3> kSplashEffects { "splash2", "splash4", "watersplashbig" };
        constexpr std::string_view kWakeEffect = "waterwakemedium";
        // Where a pool slide's rider comes down in the water, a low spray rises and falls back amid a little foam,
        // and rings spread on the water from there.
        constexpr std::string_view kFoam = kWakeEffect; ///< Chosen by eye: foam spreading on the water
        constexpr float kSprayScale = 0.45f;            ///< Chosen by eye: the spray this small rises below rider high
        constexpr float kDownRings = 0.8f;              ///< Chosen by eye: the rings' strength
        std::erase_if(scene.splashdowns, [&](const std::pair<float, glm::vec3>& down) {
            if (down.first > scene.park_clock)
                return false;
            fire_park_particles(scene.particles, kFoam, down.second, glm::vec3(0, 1, 0));
            fire_park_particles(scene.particles, kSplashEffects[0], down.second, glm::vec3(0, 1, 0), kSprayScale);
            start_rings(scene, down.second, kDownRings);
            return true;
        });
        // Off a pool slide's open end, a fine spray leaves the lip now and then, and where the water comes down rings
        // keep spreading on a pool, or it splashes on the ground, each a little off the last.
        if (scene.park_clock >= scene.pours_next && !scene.slide_pours.empty())
        {
            constexpr float kPourGap = 0.4f;     ///< Chosen by eye: seconds between one spray and ring and the next
            constexpr float kPourSpray = 0.25f;  ///< Chosen by eye: the spray this small, off the lip
            constexpr float kPourRings = 0.35f;  ///< Chosen by eye: the rings' strength where it falls in
            constexpr float kPourSplash = 0.3f;  ///< Chosen by eye: the splash this small on the ground
            constexpr float kPourScatter = 0.5f; ///< Chosen by eye: how far off the landing each comes down
            scene.pours_next = scene.park_clock + kPourGap;
            for (std::size_t k = 0; k < scene.slide_pours.size(); ++k)
            {
                const ParkScene::SlidePour& pour = scene.slide_pours[k];
                // Spread as hash noise of the clock: no state to keep, and different for each end.
                const auto noise = [&](float salt) { return glm::fract(std::sin(scene.park_clock * 12.9898f + static_cast<float>(k) * 78.233f + salt) * 43758.547f); };
                const glm::vec3 lip = glm::mix(pour.left, pour.right, noise(1.0f));
                fire_park_particles(scene.particles, "rswaterspray", lip, glm::normalize(pour.landing - lip), kPourSpray);
                const glm::vec3 at = pour.landing + glm::vec3(noise(2.0f) - 0.5f, 0.0f, noise(3.0f) - 0.5f) * (2 * kPourScatter);
                if (pour.water)
                    start_rings(scene, at, kPourRings);
                else
                    fire_park_particles(scene.particles, "rswaterspray", at, glm::vec3(0, 1, 0), kPourSplash);
            }
        }
        scene.car_waters.resize(scene.cars.size());
        std::vector<float> moved(scene.cars.size()); // Along its course since the park kept it, by car
        for (std::size_t i = 0; i < scene.cars.size(); ++i)
        {
            const std::uint64_t id = scene.cars[i].id;
            const rct3::park::ride::CarSpot spot = scene.car_spots[i];
            glm::mat4 place = scene.saved_cars.at(id);
            const entt::entity ride = scene.simulate && spot.ride < scene.ride_entities.size() ? scene.ride_entities[spot.ride] : entt::null;
            const auto* fleet = ride != entt::null ? scene.sim.registry.try_get<simulation::Trains>(ride) : nullptr;
            const auto* boats = ride != entt::null ? scene.sim.registry.try_get<simulation::Boats>(ride) : nullptr;
            if (boats && spot.train < boats->boats.size() && boats->boats[spot.train].state != simulation::Sailing::Docked)
            {
                // Out on the water: between where its last step began and ended, turned from the way the park kept
                // it to the boat's heading and leant into its turn, at its height.
                const simulation::BoatPose boat = simulation::between_sails(boats->boats[spot.train], scene.sim_time / simulation::kTickSeconds);
                const glm::vec3 kept(place[3]);
                const glm::vec2 kept_way = glm::vec2(place[2].x, place[2].z);
                const float turn = glm::length(kept_way) > 0 ? std::atan2(kept_way.x, kept_way.y) - std::atan2(boat.heading.x, boat.heading.y) : 0.0f;
                const glm::mat4 lean = glm::rotate(glm::mat4(1.0f), boat.lean.value, glm::vec3(boat.heading.x, 0, boat.heading.y));
                place = glm::translate(glm::mat4(1.0f), glm::vec3(boat.at.x, kept.y, boat.at.y)) * lean * glm::rotate(glm::mat4(1.0f), -turn, glm::vec3(0, 1, 0)) * glm::translate(glm::mat4(1.0f), -kept) * place;
            }
            else if (fleet && spot.train < fleet->trains.size() && scene.courses[spot.ride])
            {
                const simulation::Coaster& train = fleet->trains[spot.train];
                const bool aboard = spot.train < fleet->riders.size() && !fleet->riders[spot.train].empty();
                scene.car_waters[i].ridden = aboard || (scene.car_waters[i].ridden && (train.splashed || train.running));
                // Between where its last step began and ended, from where the park kept it. Off a pool slide's
                // end, how long it has flown by now: below 0 while it is still running down to the end in the step
                // it went off in.
                const float along = std::clamp(scene.sim_time / simulation::kTickSeconds, 0.0f, 1.0f);
                const float flown_for = train.flight ? train.flight->off.value - (1 - along) * simulation::kTickSeconds : 0.0f;
                const float at = train.flight ? std::max(train.was_at, train.at + train.flight->speed.value * std::min(flown_for, 0.0f)) : simulation::between_drives(train, along);
                moved[i] = at - scene.rides.rides[spot.ride].trains[spot.train].distance;
                place = rct3::park::track::moved_along(*scene.courses[spot.ride], place, { spot.front, spot.rear }, moved[i]);
                // A slide's car swung up its trough's wall, turned about its shape's pivot.
                if (train.trough && train.lean.value != 0)
                {
                    const glm::vec3 way = glm::normalize(glm::vec3(place[2]));
                    const glm::vec3 pivot = glm::vec3(place[3]) + glm::normalize(glm::vec3(place[1])) * train.trough->pivot;
                    place = glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), -train.lean.value, way) * glm::translate(glm::mat4(1.0f), -pivot) * place;
                }
                // Crashed, each car flies on and tumbles as its wreck does from where it left the track, and is
                // gone with it; a train whose wrecks are gone is off the track till it is back in the station.
                // shortcut: drawn as it was, not burnt; upgrade if a burnt look turns up among the cars' textures.
                // Off a pool slide's end, it flies on and floats on the pool till it is gone, out of sight till it
                // is back at the station; its riders fly on, to splash down in the pool.
                if (!train.splashed)
                    scene.car_waters[i].off_end = scene.car_waters[i].landed = false;
                if (train.splashed)
                {
                    // Between steps, flown again from where it left to now: its flight is the same however it is
                    // stepped, and a step is far too long to draw it standing.
                    std::optional<simulation::Flight> flight;
                    if (const auto& f = train.flight)
                    {
                        flight = simulation::Flight { .from = f->from, .way = f->way, .at = f->from, .velocity = f->way * f->speed.value, .water = f->water, .speed = f->speed, .reach = f->reach };
                        simulation::fly(*flight, std::max(flown_for, 0.0f));
                    }
                    // The car comes down with a splash too, ridden or not.
                    if (flight && flight->afloat && !std::exchange(scene.car_waters[i].landed, true))
                        scene.splashdowns.emplace_back(scene.park_clock, flight->at);
                    if (!std::exchange(scene.car_waters[i].off_end, true))
                        for (const auto [guest, drop, pose] : scene.sim.registry.view<const simulation::Dropped, const simulation::Pose>().each())
                            if (drop.slide == ride && pose.position.y > drop.ground)
                            {
                                // Falling from where they are as the simulation drops them, till they reach the water.
                                const float g = simulation::kTuning.holding.gravity;
                                const float f = drop.fall.value;
                                const float t = (std::sqrt(f * f + 2 * g * (pose.position.y - drop.ground)) - f) / g;
                                glm::vec2 down = glm::vec2 { pose.position.x, pose.position.z } + drop.drift * t;
                                // Short of the pool's far wall, as the simulation stops them there.
                                const auto* door = scene.sim.registry.valid(drop.pool) ? scene.sim.registry.try_get<simulation::PoolDoor>(drop.pool) : nullptr;
                                if (const auto* pool = door != nullptr && scene.sim.registry.valid(door->pool) ? scene.sim.registry.try_get<simulation::SwimPool>(door->pool) : nullptr)
                                    down = simulation::pool_stop(*pool, { pose.position.x, pose.position.z }, down, scene.sim.paths.tile_size);
                                scene.splashdowns.emplace_back(scene.park_clock + t, glm::vec3 { down.x, drop.ground, down.y });
                            }
                    place = flight ? simulation::flown(*flight, place) : glm::mat4(0.0f);
                }
                else if (train.crashed)
                {
                    const auto* wrecks = scene.sim.registry.try_get<simulation::Wrecks>(ride);
                    const std::size_t car = static_cast<std::size_t>(std::ranges::count_if(std::span(scene.car_spots).first(i), [&](const rct3::park::ride::CarSpot& s) { return s.ride == spot.ride && s.train == spot.train; }));
                    const auto* wreck = wrecks != nullptr && spot.train < wrecks->trains.size() && car < wrecks->trains[spot.train].size() ? &wrecks->trains[spot.train][car] : nullptr;
                    if (wreck == nullptr || wreck->state == simulation::WreckState::Gone)
                        place = glm::mat4(0.0f);
                    else
                    {
                        const glm::vec3 left(place[3]);
                        place = glm::translate(glm::mat4(1.0f), left + wreck->at - wreck->from) * glm::mat4_cast(wreck->turned) * glm::translate(glm::mat4(1.0f), -left) * place;
                    }
                }
                else if (const auto model = scene.models.find(rct3::park::object::model_ref(scene.cars[i])); model != scene.models.end())
                {
                    // Through the water: sprays from its splash points as it splashes, a wake behind it now and then.
                    const glm::vec3 velocity = seconds > 0 ? (glm::vec3(place[3]) - glm::vec3(scene.car_places[i][3])) / seconds : glm::vec3(0.0f);
                    // Only cars that spray, and the one ridden for the water on the lens, look for the park's water.
                    const bool looks = !model->second.splash_points.empty() || scene.riding == i;
                    const rct3::park::track::TrackCourse& course = *scene.courses[spot.ride];
                    // shortcut: a splash ride whose track says nothing of water (a sea serpent diving into its
                    // lake) is taken as wet all along, splashing where its fall stops short; upgrade once such a
                    // track's water is found in its files.
                    const auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(ride);
                    const bool dry_splasher = cycle != nullptr && cycle->splash && std::ranges::none_of(course.wet, std::identity { });
                    const bool in_water = dry_splasher || (looks && rct3::park::water::water_above(scene.water.surface, glm::vec3(place[3])).has_value());
                    const rct3::park::CarSplash splash = rct3::park::car_splash(scene.car_waters[i], course, spot.front + moved[i], in_water, velocity, seconds);
                    for (const rct3::park::object::SplashPoint& point : model->second.splash_points)
                        if (point.wake ? splash.wake : splash.splash)
                            fire_park_particles(scene.particles, point.wake ? kWakeEffect : kSplashEffects[std::min<std::size_t>(point.strength, 2)], glm::vec3(place * glm::vec4(point.position, 1.0f)), glm::vec3(place[1]));
                }
            }
            // Pulled down, its cars go with it; a kept train the park does not run (a shuttle runs one) is not shown
            // standing on the course the one it runs goes along.
            if ((spot.ride < scene.rides.rides.size() && scene.rides.rides[spot.ride].base.demolished) || (fleet && !boats && spot.train >= fleet->trains.size()))
                place = glm::mat4(0.0f);
            scene.car_places[i] = place;
        }
        // Riding along, the view looks out from the rider's eyes in the car where it is now, at the track as far
        // ahead as its train looks.
        scene.seat.reset();
        scene.seat_across = rct3::park::kRideWideView;
        if (const auto seat = scene.riding ? ride_eye(scene, *scene.riding, scene.riding_eye) : std::nullopt)
        {
            const std::size_t i = *scene.riding;
            const rct3::park::ride::CarSpot spot = scene.car_spots[i];
            ovl::RideCameraTuning tuning = scene.models.at(rct3::park::object::model_ref(scene.cars[i])).ride_look;
            // On a lift chain the view shakes as the train sets for the chain, from when this car is on it.
            const entt::entity ride = scene.simulate && spot.ride < scene.ride_entities.size() ? scene.ride_entities[spot.ride] : entt::null;
            const auto* fleet = ride != entt::null ? scene.sim.registry.try_get<simulation::Trains>(ride) : nullptr;
            const simulation::Coaster* train = fleet && spot.train < fleet->trains.size() ? &fleet->trains[spot.train] : nullptr;
            if (train && simulation::chain_at(*train, spot.front + moved[i]))
            {
                tuning.shake = tuning.chain_shake;
                tuning.shake_across = tuning.chain_shake_across;
                tuning.shake_up = tuning.chain_shake_up;
            }
            // The nose and front seat look at the track ahead rather than straight out, so a curve stays in view;
            // the nose does not sway. Running backwards, the view looks the way the car faces.
            std::optional<glm::vec3> ahead;
            if (const auto& course = scene.courses[spot.ride]; course && tuning.look_ahead > 0 && scene.riding_eye != rct3::park::object::RideEye::Rear && !(train && train->speed < 0))
                ahead = glm::vec3(rct3::park::track::moved_along(*course, scene.saved_cars.at(scene.cars[i].id), { spot.front, spot.rear }, moved[i] + tuning.look_ahead) * (*seat)[3]);
            const glm::vec2 dice { static_cast<float>(random::unit(scene.ride_dice)) * 2 - 1, static_cast<float>(random::unit(scene.ride_dice)) * 2 - 1 };
            scene.seat = rct3::park::looked_round(rct3::park::ride_look(scene.ride_view, scene.car_places[i] * *seat, ahead, scene.riding_eye != rct3::park::object::RideEye::Nose, tuning, seconds, dice), scene.ride_turn);
        }
        // On a flat ride, from one of its fixed cameras, or from a rider's eyes over their seat, as they move. A
        // seat faces +Z with +Y up, as the people sat on it are modelled.
        constexpr glm::vec3 kSeatEyes { 0, 0.75f, 0.2f }; ///< Where a sitting rider's eyes are, just before their face
        if (const auto seat = scene.riding_flat ? flat_view(scene, scene.riding_flat->first, scene.riding_fixed, scene.riding_flat->second) : std::nullopt)
        {
            scene.seat = rct3::park::looked_round(scene.riding_fixed ? *seat : glm::translate(*seat, kSeatEyes), scene.ride_turn);
            scene.seat_across = scene.riding_fixed ? rct3::park::kRideWideView : rct3::park::kRideNarrowView;
        }
        // With a person, from their eyes, or from behind and over them looking down a little; people are
        // modelled facing +Z with +Y up.
        constexpr float kEyeHeight = 1.6f;    ///< A standing person's eyes over their feet
        constexpr float kBehindHeight = 1.2f; ///< The point followed from behind, over their feet
        constexpr float kBehindPitch = 0.4f;  ///< How far down that view looks, radians
        constexpr float kBehindDistance = 3;  ///< How far back from that point
        if (scene.riding_person && *scene.riding_person < scene.shown_people.size())
        {
            const glm::mat4 place = person_place(scene, scene.shown_people[*scene.riding_person]);
            scene.seat_across = rct3::park::kRideNarrowView;
            if (scene.person_behind)
                scene.seat = rct3::park::looked_round(glm::rotate(glm::translate(place, glm::vec3(0, kBehindHeight, 0)), kBehindPitch, glm::vec3(1, 0, 0)), scene.ride_turn);
            else
                scene.seat = rct3::park::looked_round(glm::translate(place, glm::vec3(0, kEyeHeight, 0)), scene.ride_turn);
        }
        // The wheel pulls the view back off its mount, the farther the more it is turned. Pulled back, it stays
        // over the ground, unless the mount itself is under it, as in a tunnel.
        constexpr float kOverGround = 0.5f; ///< The least a pulled back view keeps over the ground
        constexpr float kGroundLook = 1000; ///< How far up and down the ground is looked for
        if (const float back = scene.ride_pull * scene.ride_pull + (scene.riding_person && scene.person_behind ? kBehindDistance : 0); scene.seat && back > 0)
        {
            const glm::vec3 mount((*scene.seat)[3]);
            scene.seat = glm::translate(*scene.seat, glm::vec3(0, 0, -back));
            glm::vec3 eye((*scene.seat)[3]);
            const auto down = [&](glm::vec3 from) { return rct3::park::object::ground_hit(rct3::park::Ray { from, glm::vec3(0, -1, 0) }, scene.grid, kGroundLook); };
            if (const auto under = down(glm::vec3(eye.x, eye.y + kGroundLook / 2, eye.z)); under && down(mount))
                (*scene.seat)[3].y = std::max(eye.y, eye.y + kGroundLook / 2 - *under + kOverGround);
        }
        // Arms take the angles their track holds where the car is, the joints at rest where it holds none.
        std::size_t joints = 0;
        for (const ParkScene::ArmCar& arm : scene.arm_cars)
            joints += arm.joints.size();
        scene.car_mesh_places.resize(scene.car_places.size() + joints);
        std::ranges::copy(scene.car_places, scene.car_mesh_places.begin());
        // A car that is itself a stand-in rider, a test's dummy, gives way to a real rider from boarding till it
        // stands empty again, off a slide's end and back with them.
        for (std::size_t i = 0; i < scene.car_places.size() && i < scene.car_waters.size(); ++i)
            if (const auto model = scene.car_waters[i].ridden ? scene.models.find(rct3::park::object::model_ref(scene.cars[i])) : scene.models.end(); model != scene.models.end() && model->second.stand_in)
                scene.car_mesh_places[i] = glm::mat4(0.0f);
        // Cars their bones move are drawn apart. A boat out on the water rows, with the oars its turn calls for; one
        // on a course rows with both while its train moves.
        for (ParkScene::BonedCar& boned : scene.boned_cars)
        {
            scene.car_mesh_places[boned.car] = glm::mat4(0.0f);
            const rct3::park::object::ObjectModel& model = scene.models.at(rct3::park::object::model_ref(scene.cars[boned.car]));
            const rct3::park::ride::CarSpot spot = scene.car_spots[boned.car];
            const entt::entity ride = scene.simulate && spot.ride < scene.ride_entities.size() ? scene.ride_entities[spot.ride] : entt::null;
            const auto* boats = ride != entt::null ? scene.sim.registry.try_get<simulation::Boats>(ride) : nullptr;
            const simulation::Boat* boat = boats && spot.train < boats->boats.size() && boats->boats[spot.train].state != simulation::Sailing::Docked ? &boats->boats[spot.train] : nullptr;
            const auto* fleet = ride != entt::null ? scene.sim.registry.try_get<simulation::Trains>(ride) : nullptr;
            const bool coursing = fleet && spot.train < fleet->trains.size() && fleet->trains[spot.train].running && fleet->trains[spot.train].speed != 0;
            const auto motion = [&](simulation::Stroke stroke) { return (*model.rowing)[std::to_underlying(stroke)]; };
            const float length = boned.stroke ? model.rig->motions[motion(*boned.stroke)].seconds : 0.0f;
            if (!boned.stroke || scene.park_clock - boned.since >= length)
            {
                // On from the end of the last stroke, unless that is long past (the car rested, or was not drawn).
                boned.since = boned.stroke && scene.park_clock - boned.since < 2 * length ? boned.since + length : scene.park_clock;
                boned.stroke = boat ? std::optional(simulation::stroke(boat->turning)) : coursing ? std::optional(simulation::Stroke::Both) : std::nullopt;
            }
            const std::vector<glm::mat4> moves = rct3::park::bone_moves(*model.rig, boned.stroke ? motion(*boned.stroke) : model.rig->motions.size(), scene.park_clock - boned.since);
            std::vector<glm::mat4> places(moves.size());
            for (std::size_t b = 0; b < places.size(); ++b)
                places[b] = scene.car_places[boned.car] * moves[b];
            rct3::park::move_meshes(boned.mesh, places);
        }
        auto joint_place = scene.car_mesh_places.begin() + static_cast<std::ptrdiff_t>(scene.car_places.size());
        for (ParkScene::ArmCar& arm : scene.arm_cars)
        {
            const rct3::park::ride::CarSpot spot = scene.car_spots[arm.car];
            std::optional<rct3::park::track::ArmAngles> angles;
            if (const save::model::Ride& ride = scene.rides.rides[spot.ride]; ride.track && scene.courses[spot.ride])
            {
                std::vector<std::optional<rct3::park::track::ArmAngles>> held;
                for (const save::model::TrackPiece& piece : std::span(ride.track->pieces).subspan(std::min(scene.courses[spot.ride]->first_piece, ride.track->pieces.size())))
                    held.push_back(piece.arm_angles);
                angles = rct3::park::track::arm_angles_at(*scene.courses[spot.ride], held, (spot.front + spot.rear) / 2 + moved[arm.car]);
            }
            const std::vector<glm::mat4> moves = rct3::park::arm_moves(arm.joints, angles ? std::span<const float>(*angles) : std::span<const float> { });
            for (const glm::mat4& move : moves)
                *joint_place++ = scene.car_places[arm.car] * move;
            arm.seats = moves.empty() ? glm::mat4(1.0f) : moves.back();
        }
        profiling::section("Rides", [&] { swing_rides(scene, eye, view); });
        profiling::section("Mesh", [&] {
            rct3::park::move_meshes(scene.car_meshes, scene.car_mesh_places);
            scene.rider_places.resize(scene.rider_cars.size());
            for (std::size_t k = 0; k < scene.rider_cars.size(); ++k)
                scene.rider_places[k] = carrier_place(scene, scene.rider_cars[k]);
            rct3::park::move_meshes(scene.riders, scene.rider_places);
        });
    }

    /// Builds the people meshes again as often as they need, all beside the frame: the moving ones as soon as
    /// the last are done, the riders a few times a second, the still ones when a person's level of detail
    /// changes. The still ones' levels are only taken on with them, so the moving ones keep to the same levels.
    void refresh_people(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, units::Seconds elapsed)
    {
        if (!scene.person_models)
            return;
        scene.since_people_levels += elapsed;
        if (ready(scene.people_build))
        {
            PeopleBuild built = scene.people_build.get();
            if (built.simulate == scene.simulate)
            {
                scene.people_levels = std::move(built.levels);
                scene.still_people = std::move(built.mesh);
            }
            else
                scene.people_levels.clear(); // Built before the switch changed; build again
        }
        if (scene.since_people_levels >= kObjectRebuildInterval && !scene.people_build.valid())
        {
            scene.since_people_levels = { };
            // Guests who came in at the gate since: dressed from the wardrobe for their age and sex, and kept
            // with the park's people like those it was saved with. Here, as no build reads the shown people.
            auto& r = scene.sim.registry;
            for (const auto [e, visitor, mood] : r.view<const simulation::Visitor, const simulation::Mood>(entt::exclude<simulation::Left>).each())
            {
                if (!scene.simulate || scene.wardrobe.empty() || !r.all_of<simulation::Stride>(e) || !scene.known_guests.insert(e).second)
                    continue;
                // A VIP wears their own clothes.
                const auto* vip = r.try_get<simulation::Vip>(e);
                const std::uint8_t celebrity = vip != nullptr ? vip->who : 0;
                const auto type = vip != nullptr ? save::model::PersonType::Special : static_cast<save::model::PersonType>(std::to_underlying(visitor.age)); // The same numbers
                const save::model::Sex sex = visitor.female && vip == nullptr ? save::model::Sex::Female : save::model::Sex::Male;
                // Clothes, then swimwear for the body and legs.
                std::array<const Outfit*, 5> worn { };
                for (std::size_t k = 0; k < worn.size(); ++k)
                {
                    const bool swim = k >= 3;
                    const std::size_t section = swim ? k - 2 : k;
                    std::vector<const Outfit*> fits;
                    for (const Outfit& o : scene.wardrobe)
                        if (o.type == type && o.sex == sex && o.vip == celebrity && o.section == section && o.swim == swim)
                            fits.push_back(&o);
                    if (!fits.empty())
                        worn[k] = fits[random::below(scene.looks, fits.size())];
                }
                if (std::ranges::contains(std::span(worn).first(3), nullptr))
                    continue;
                save::model::Guest& kept = scene.people.guests.emplace_back();
                kept.in_park = true;
                kept.mood_now.happiness = mood.happiness.value;
                kept.person.type = type;
                kept.person.sub_type = vip != nullptr ? celebrity : std::to_underlying(sex);
                kept.person.name = visitor.name.empty() ? std::format("Guest {}", scene.people.guests.size()) : visitor.name;
                save::model::Appearance& a = kept.person.appearance;
                a.head = { worn[0]->id };
                a.body = { worn[1]->id };
                a.legs = { worn[2]->id };
                if (worn[3] != nullptr)
                    a.swim_body = { worn[3]->id };
                if (worn[4] != nullptr)
                    a.swim_legs = { worn[4]->id };
                ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .saved = scene.people.guests.size() - 1, .guest = e, .swim_body = worn[3] != nullptr ? worn[3]->set : nullptr, .swim_legs = worn[4] != nullptr ? worn[4]->set : nullptr });
                rct3::park::person::PersonLook& look = shown.pose.look;
                look.head = worn[0]->set;
                look.body = worn[1]->set;
                look.legs = worn[2]->set;
                for (std::size_t i = 0; i < look.colours.size(); ++i)
                    look.colours[i] = a.palettes[i] = static_cast<std::uint8_t>(random::below(scene.looks, kColourCells));
                shown.pose.motion = rct3::park::person::motion::kWalk;
            }
            // Keepers hired with animal houses since, and lifeguards come with lifeguard chairs, in their jobs' clothes.
            for (const auto [e, staff, at] : r.view<const simulation::Staff, const simulation::Pose>(entt::exclude<simulation::Stride>).each())
            {
                const bool keeper = r.all_of<simulation::KeeperDuty>(e);
                if (!scene.simulate || (!keeper && staff.role != simulation::StaffRole::Other) || !scene.known_guests.insert(e).second)
                    continue;
                const auto type = keeper ? save::model::PersonType::AnimalKeeper : save::model::PersonType::LifeGuard;
                std::array<const Outfit*, 3> worn { };
                for (const Outfit& o : scene.wardrobe)
                    if (o.type == type && !o.swim && worn[o.section] == nullptr)
                        worn[o.section] = &o;
                if (std::ranges::contains(worn, nullptr))
                    continue;
                save::model::Staff& kept = scene.people.staff.emplace_back();
                kept.kind = keeper ? save::model::StaffKind::AnimalKeeper : save::model::StaffKind::LifeGuard;
                kept.person.type = type;
                kept.person.name = std::format("{} {}", keeper ? "Keeper" : "Lifeguard", scene.people.staff.size());
                kept.task_flags = staff.tasks;
                if (keeper)
                    kept.work = save::model::KeeperWork { };
                save::model::Appearance& a = kept.person.appearance;
                a.head = { worn[0]->id };
                a.body = { worn[1]->id };
                a.legs = { worn[2]->id };
                ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .staff = true, .saved = scene.people.staff.size() - 1, .guest = e });
                shown.pose.look.head = worn[0]->set;
                shown.pose.look.body = worn[1]->set;
                shown.pose.look.legs = worn[2]->set;
                shown.pose.motion = keeper ? rct3::park::person::motion::kWalk : rct3::park::person::motion::kIdle;
                shown.pose.world = glm::translate(glm::mat4(1.0f), at.position); // Where they stand while the park is still
            }
            // Sellers, as the game keeps them: one stands at each stall and ride entrance whose model has a place
            // for them, from when it is built, and goes when it is pulled down or moved.
            if (scene.simulate)
            {
                std::unordered_set<std::uint64_t> posts;
                for (const save::model::Stall& stall : scene.rides.stalls)
                    if (!stall.base.demolished && stall.base.spot && stall.base.spot->object != 0)
                        posts.insert(stall.base.spot->object);
                for (const save::model::Ride& ride : scene.rides.rides)
                    for (const save::model::Station& station : ride.stations)
                        if (!ride.base.demolished && station.entrance_spot && station.entrance_spot->object != 0)
                            posts.insert(station.entrance_spot->object);
                // Each seller's post, by their place in shown_people, and the objects of all posts.
                std::vector<std::pair<std::size_t, std::uint64_t>> sellers;
                for (std::size_t i = 0; i < scene.shown_people.size(); ++i)
                {
                    const ShownPerson& p = scene.shown_people[i];
                    if (const auto* work = p.staff && p.saved < scene.people.staff.size() ? std::get_if<save::model::VendorWork>(&scene.people.staff[p.saved].work) : nullptr; work != nullptr && !work->scenery_item.is_none())
                        sellers.emplace_back(i, work->scenery_item.id);
                }
                std::unordered_set<std::uint64_t> wanted = posts;
                for (const auto& [i, post] : sellers)
                    wanted.insert(post);
                std::unordered_map<std::uint64_t, const save::model::PlacedObject*> found;
                for (const save::model::PlacedObject& o : scene.objects)
                    if (wanted.contains(o.id))
                        found.emplace(o.id, &o);
                // A seller goes with their object.
                std::unordered_set<std::uint64_t> served;
                for (const auto& [i, post] : sellers | std::views::reverse)
                {
                    if (const auto at = found.find(post); at != found.end() && !at->second->hidden)
                        served.insert(post);
                    else
                        fire(scene, i);
                }
                std::int32_t number = 0;
                for (const save::model::Staff& s : scene.people.staff)
                    if (s.kind == save::model::StaffKind::Vendor)
                        number = std::max(number, s.type_id);
                for (const std::uint64_t post : posts)
                {
                    const auto object = found.find(post);
                    const auto model = object != found.end() && !object->second->hidden ? scene.models.find(rct3::park::object::model_key(*object->second)) : scene.models.end();
                    if (served.contains(post) || model == scene.models.end() || !model->second.vendor_place)
                        continue;
                    const save::model::Sex sex = random::below(scene.looks, 2) == 0 ? save::model::Sex::Male : save::model::Sex::Female;
                    std::array<const Outfit*, 3> worn { };
                    for (const Outfit& o : scene.wardrobe)
                        if (o.type == save::model::PersonType::Vendor && o.sex == sex && !o.swim && worn[o.section] == nullptr)
                            worn[o.section] = &o;
                    if (std::ranges::contains(worn, nullptr))
                        continue;
                    const glm::mat4 place = rct3::park::object::placed_transform(*object->second, &model->second, scene.grid, scene.fitted) * *model->second.vendor_place;
                    const entt::entity e = simulation::add_vendor(scene.sim, glm::vec3(place[3]));
                    scene.known_guests.insert(e);
                    save::model::Staff& kept = scene.people.staff.emplace_back();
                    kept.kind = save::model::StaffKind::Vendor;
                    kept.type_id = ++number;
                    kept.work = save::model::VendorWork { .scenery_item = { post } };
                    kept.person.type = save::model::PersonType::Vendor;
                    kept.person.sub_type = std::to_underlying(sex);
                    kept.person.name = std::format("%ME% {}", number); // As the game names them, after their job
                    kept.person.position = { place[3].x, place[3].y, place[3].z };
                    save::model::Appearance& a = kept.person.appearance;
                    a.head = { worn[0]->id };
                    a.body = { worn[1]->id };
                    a.legs = { worn[2]->id };
                    ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .staff = true, .saved = scene.people.staff.size() - 1, .guest = e });
                    shown.pose.look.head = worn[0]->set;
                    shown.pose.look.body = worn[1]->set;
                    shown.pose.look.legs = worn[2]->set;
                    for (std::size_t i = 0; i < shown.pose.look.colours.size(); ++i)
                        shown.pose.look.colours[i] = a.palettes[i] = static_cast<std::uint8_t>(random::below(scene.looks, kColourCells));
                    shown.pose.world = place;
                    // Stored a row per axis, which is a column each here.
                    for (glm::length_t c = 0; c < 4; ++c)
                        for (glm::length_t r = 0; r < 4; ++r)
                            kept.person.world[static_cast<std::size_t>(c * 4 + r)] = place[c][r];
                }
            }
            // The pace of each shown person's walk, measured once for each legs set.
            if (scene.person_models)
            {
                for (ShownPerson& shown : scene.shown_people)
                {
                    const rct3::park::person::PartSet* legs = shown.pose.look.legs;
                    if (shown.paces[0] > 0 || legs == nullptr)
                        continue;
                    auto [paces, fresh] = scene.walk_paces.try_emplace(legs);
                    if (fresh)
                        for (std::size_t gait = 0; gait < kGaits; ++gait)
                            for (const std::size_t piece : legs->levels[0].fixed)
                                if (const auto measured = rct3::park::person::walk_pace(scene.person_models->piece(piece), kGaitMotions[gait]); measured && paces->second[gait] == 0)
                                    paces->second[gait] = *measured;
                    shown.paces = paces->second;
                }
            }
            rct3::park::object::ObjectLevels levels(scene.shown_people.size(), rct3::park::object::kNotDrawn);
            for (std::size_t i = 0; i < levels.size(); ++i)
            {
                if (const ShownPerson& p = scene.shown_people[i]; scene.simulate && p.guest != entt::null && scene.sim.registry.all_of<simulation::Left>(p.guest))
                    continue; // Gone home
                const float distance = glm::distance(eye, standing_at(scene, scene.shown_people[i]));
                // Scaled as objects' levels are: detail for the finer ones, draw distance for the last.
                const rct3::park::object::LevelScale scale = scene.level_scales[static_cast<std::size_t>(rct3::park::DetailKind::People)];
                const float last = kPersonDistances.back() * scale.distance;
                for (std::size_t k = 0; k < kPersonDistances.size(); ++k)
                {
                    if (distance <= (k + 1 < kPersonDistances.size() ? std::min(kPersonDistances[k] * scale.detail, last) : last))
                    {
                        levels[i] = static_cast<std::int8_t>(k);
                        break;
                    }
                }
            }
            if (levels != scene.people_levels)
            {
                scene.people_build = std::async(std::launch::async, [&scene, levels = std::move(levels), view, simulate = scene.simulate] {
                    profiling::register_thread("PeopleWorker");
                    PeopleBuild built;
                    rct3::park::person::PersonRuns runs;
                    built.mesh = profiling::section("Still", [&] { return rct3::park::person::person_mesh(*scene.person_models, place_people(scene, levels, 0, false, view, simulate), runs); });
                    built.levels = levels;
                    built.simulate = simulate;
                    profiling::unregister_thread();
                    return built;
                });
            }
        }
        // Into or out of their swimwear: in a changing room, out of sight, it just happens; anywhere else, as when
        // the player lets a guest go into a pool or lifts one out, a puff of sparkles hides the change.
        if (scene.simulate)
        {
            constexpr std::string_view kChangePuff = "magicsparkles";
            constexpr float kPuffHeight = 0.9f; ///< Chosen by eye: above their feet, round their middle
            const auto& r = scene.sim.registry;
            for (std::size_t i = 0; i < scene.shown_people.size(); ++i)
            {
                ShownPerson& shown = scene.shown_people[i];
                if (shown.guest == entt::null || !r.valid(shown.guest) || !r.all_of<simulation::Pose>(shown.guest))
                    continue;
                const auto* swimmer = r.try_get<simulation::Swimmer>(shown.guest);
                const bool changing = swimmer != nullptr && swimmer->changing.value > 0;
                const bool swimwear = simulation::in_swimwear(r, shown.guest);
                if (swimwear != shown.swimwear && !changing && !shown.changing && i < scene.people_levels.size() && scene.people_levels[i] != rct3::park::object::kNotDrawn)
                    fire_park_particles(scene.particles, kChangePuff, r.get<simulation::Pose>(shown.guest).position + glm::vec3 { 0, kPuffHeight, 0 }, glm::vec3 { 0, 1, 0 });
                shown.swimwear = swimwear;
                shown.changing = changing;
                // Into or out of the water, but for a dive; a diver going under throws up a little spray, and rings
                // spread on the water from there.
                if (swimmer == nullptr || changing)
                    continue;
                const glm::vec3 at = r.get<simulation::Pose>(shown.guest).position;
                if (swimmer->dive == simulation::DiveStep::Surface && shown.dive == simulation::DiveStep::Fall)
                {
                    const auto* errand = r.try_get<simulation::Errand>(shown.guest);
                    const auto* door = errand != nullptr && r.valid(errand->target) ? r.try_get<simulation::PoolDoor>(errand->target) : nullptr;
                    const auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<simulation::SwimPool>(door->pool) : nullptr;
                    const bool high = pool != nullptr && swimmer->board && static_cast<std::size_t>(*swimmer->board) < pool->boards.size() && pool->boards[static_cast<std::size_t>(*swimmer->board)].high;
                    constexpr std::string_view kSpray = "splash2"; ///< Chosen by eye: a low spray of drops
                    constexpr float kSmallSpray = 0.35f;           ///< Chosen by eye: its size off a small board
                    constexpr float kHighSpray = 0.6f;             ///< Chosen by eye: and off a high one
                    constexpr float kSmallRings = 0.6f;            ///< Chosen by eye: rings' strength off a small board; full off a high one
                    fire_park_particles(scene.particles, kSpray, at, glm::vec3 { 0, 1, 0 }, high ? kHighSpray : kSmallSpray);
                    start_rings(scene, at, high ? 1.0f : kSmallRings);
                }
                if (swimmer->edge == simulation::PoolEdge::DiveIn && shown.edge != simulation::PoolEdge::DiveIn)
                {
                    constexpr float kEdgeRings = 0.7f; ///< Chosen by eye: rings' strength diving in off the edge
                    constexpr float kEdgeDelay = 1.0f; ///< Chosen by eye: seconds into the dive the body meets the water
                    start_rings(scene, swimmer->edge_to, kEdgeRings, kEdgeDelay);
                }
                shown.edge = swimmer->edge;
                shown.dive = swimmer->dive;
            }
        }
        // The people in motion are posed away from the frame, again as soon as the last posing is done.
        if (ready(scene.moving_build))
            scene.moving_people = scene.moving_build.get();
        if (!scene.moving_build.valid())
        {
            const profiling::Scope zone("Moving");
            scene.moving_build = std::async(std::launch::async, [&library = *scene.person_models, &runs = scene.moving_runs, placed = place_people(scene, scene.people_levels, scene.park_clock, true, view, scene.simulate)] { return rct3::park::person::person_mesh(library, placed, runs); });
        }
        if (ready(scene.riders_build))
        {
            RidersBuild built = scene.riders_build.get();
            scene.riders = std::move(built.riders);
            scene.rider_cars = std::move(built.cars);
        }
        scene.since_riders += elapsed;
        if (scene.simulate && scene.since_riders >= kRidersRebuildInterval && !scene.riders_build.valid())
        {
            scene.since_riders = { };
            scene.riders_build = profiling::section("Riders", [&] { return pose_riders(scene); });
        }
    }

    /// The objects' meshes at the levels seen from `eye`, or nothing when those are `shown`; without the
    /// supports unless `supports`. Reads only what is set when the park loads besides its arguments, so it
    /// may run beside the frame.
    rct3::park::object::FlowFades flow_fades(const ParkScene& scene)
    {
        return rct3::park::water::flow_fades(scene.rides, scene.objects, scene.models, [&](std::size_t ride) { return ride < scene.track_rules.size() && scene.track_rules[ride].pool_ride; });
    }

    std::optional<ObjectBuild> build_objects(const ParkScene& scene, const glm::vec3& eye, const rct3::park::object::LevelPlan& shown, float band, const rct3::park::object::LevelScales& scales, bool supports, const rct3::park::object::Broken& broken, const rct3::park::object::Chains& chains, const rct3::park::object::FlowFades& fades)
    {
        ObjectBuild built;
        built.plan = profiling::section("Levels", [&] { return rct3::park::object::plan_levels(scene.objects, scene.models, scene.anchors, eye, band, scales); });
        const auto leave_out = [&](std::size_t i) {
            built.plan.steady[i] = rct3::park::object::kNotDrawn;
            built.plan.finer[i] = rct3::park::object::kNotDrawn;
            built.plan.coarser[i] = rct3::park::object::kNotDrawn;
        };
        std::vector<std::size_t> moving;
        for (const ParkScene::BonedRide& boned : scene.boned_rides)
            moving.push_back(boned.object);
        // Drawn apart, moving; a twin the save keeps of one would stand still in it.
        for (const auto& list : { moving, rct3::park::twins(scene.objects, moving) })
            for (const std::size_t i : list)
                leave_out(i);
        for (std::size_t i = 0; !supports && i < scene.objects.size() && i < built.plan.steady.size(); ++i)
        {
            if (scene.objects[i].overlay == rct3::park::support::kSupportOverlay)
                leave_out(i);
        }
        if (built.plan == shown)
            return std::nullopt;
        for (auto [shapes, levels] : { std::pair { &built.steady, &built.plan.steady }, std::pair { &built.finer, &built.plan.finer }, std::pair { &built.coarser, &built.plan.coarser } })
        {
            shapes->mesh = profiling::section("Mesh", [&] { return rct3::park::object::object_mesh(scene.objects, scene.models, scene.grid, *levels, { }, scene.leans, scene.fitted, broken, chains, fades); });
            shapes->billboards = profiling::section("Gather", [&] { return rct3::park::object::gather_billboards(scene.objects, scene.models, scene.anchors, *levels); });
        }
        return built;
    }

    /// `vertices` with the bands of `bands`, for the crossing shaders.
    std::vector<CrossingVertex> crossing_vertices(std::span<const rct3::park::GroundVertex> vertices, std::span<const std::pair<glm::vec4, glm::vec2>> bands)
    {
        std::vector<CrossingVertex> out;
        out.reserve(vertices.size());
        for (std::size_t i = 0; i < vertices.size() && i < bands.size(); ++i)
            out.push_back({ vertices[i], bands[i].first, bands[i].second });
        return out;
    }

    /// Puts `built` in the object buffers.
    void show_objects(render::Device& device, ParkScene& scene, ObjectBuild&& built)
    {
        scene.objects_shown = built.plan.steady.size();
        scene.levels = std::move(built.plan);
        show_shapes(device, scene, scene.steady_objects, std::move(built.steady), scene.levels);
        const float side = scene.tint_crossing ? 2.0f : 1.0f; // Twice as far tints them (lit.slang)
        show_shapes(device, scene, scene.finer_objects, std::move(built.finer), scene.levels, -side);
        show_shapes(device, scene, scene.coarser_objects, std::move(built.coarser), scene.levels, side);
        scene.billboards_eye.reset();
    }

    /// Takes on the objects' meshes once built, and looks at their levels again every
    /// kObjectRebuildInterval, building the meshes again beside the frame when one differs.
    void refresh_objects(render::Device& device, ParkScene& scene, const glm::vec3& eye, units::Seconds elapsed)
    {
        if (ready(scene.object_build))
        {
            if (auto built = scene.object_build.get())
                show_objects(device, scene, std::move(*built));
        }
        if (const std::array now { scene.objects_shown, scene.objects.size() }; scene.fresh_for != now)
        {
            const profiling::Scope zone("Fresh");
            // shortcut: only objects added show at once; pieces built over or hidden in place, and new billboards, wait for the next build. Upgrade when those feel slow too.
            const std::size_t from = std::min(now[0], now[1]);
            std::vector<std::size_t> moving;
            for (const ParkScene::BonedRide& boned : scene.boned_rides)
                moving.push_back(boned.object);
            moving.append_range(rct3::park::twins(scene.objects, moving));
            // Drawn as the build would: moving ones apart, supports only when shown.
            std::vector<std::int8_t> levels(now[1] - from, 0);
            for (std::size_t i = from; i < now[1]; ++i)
            {
                if (std::ranges::contains(moving, i) || (!scene.show_supports && scene.objects[i].overlay == rct3::park::support::kSupportOverlay))
                    levels[i - from] = rct3::park::object::kNotDrawn;
            }
            scene.fresh_objects = rct3::park::object::object_mesh(std::span(scene.objects).subspan(from), scene.models, scene.grid, levels, { }, scene.leans, scene.fitted, scene.broken_objects);
            add_textures(device, scene, scene.fresh_objects.batches);
            scene.fresh_for = now;
        }
        scene.since_rebuild += elapsed;
        if (scene.since_rebuild < kObjectRebuildInterval || scene.object_build.valid() || scene.supports.stale)
            return; // Stale supports change the objects first (refresh_supports)
        scene.since_rebuild = { };
        scene.object_build = std::async(std::launch::async, [&scene, eye, shown = scene.levels, band = scene.blend_band, scales = scene.level_scales, supports = scene.show_supports, broken = scene.broken_objects, chains = rct3::park::chain_runs(scene.rides, scene.objects, scene.models), fades = flow_fades(scene)] {
            profiling::register_thread("ObjectWorker");
            auto built = profiling::section("Objects", [&] { return build_objects(scene, eye, shown, band, scales, supports, broken, chains, fades); });
            profiling::unregister_thread();
            return built;
        });
    }

    /// Waits for the meshes being built beside the frame, dropping them.
    void finish_builds(ParkScene& scene)
    {
        if (scene.object_build.valid())
            scene.object_build.wait();
        if (scene.people_build.valid())
            scene.people_build.wait();
        if (scene.moving_build.valid())
            scene.moving_build.wait();
        if (scene.riders_build.valid())
            scene.riders_build.wait();
    }

    float scale_for(const render::Device& device, const ParkScene& scene)
    {
        return ui::interface_scale(static_cast<float>(device.extent().height), scene.scale, device.render_scale());
    }

    float aspect(const render::Device& device)
    {
        const render::Extent e = device.extent();
        return e.height > 0 ? static_cast<float>(e.width) / static_cast<float>(e.height) : 1.0f;
    }

} // namespace rct3::scene::park
