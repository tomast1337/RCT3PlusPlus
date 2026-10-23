// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_refresh.hpp"

#include "logging/log.hpp"
#include "park/object_models.hpp"
#include "park/person_motions.hpp"
#include "park/ride_cars.hpp"
#include "profiling/profiler.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/park_patrol.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "simulation/components.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "ui/scale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <future>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rct3::scene
{

    namespace
    {

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
                const save::PlacedObject& o = scene.objects[object->second];
                const auto model = scene.models.find(park::model_key(o));
                if (model == scene.models.end() || seat >= model->second.seat_places.size())
                    return std::nullopt;
                // On a ride its bones move, the seat's bone carries the seat.
                if (const auto boned = std::ranges::find(scene.boned_rides, object->second, &ParkScene::BonedRide::object); boned != scene.boned_rides.end() && seat < boned->seats)
                    return std::pair { scene.cars.size() + boned->first_seat + seat, model->second.seat_places[seat] };
                return std::pair { kNoCar, park::placed_transform(o, &model->second, scene.grid, scene.fitted) * model->second.seat_places[seat] };
            }
            const auto cars = scene.ride_cars.find(errand.target);
            if (cars == scene.ride_cars.end())
                return std::nullopt;
            for (const std::size_t car : cars->second)
            {
                if (fleet != nullptr && scene.car_spots[car].train != train)
                    continue;
                const auto model = scene.models.find(park::model_key(scene.cars[car]));
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

        /// How guests walk holding an umbrella up, as the game's motions name it.
        const std::int32_t kUmbrellaMotion = park::motion_number("walkumbrella").value_or(park::motion::kWalk);
        /// How a guest hands over their ticket at a ride's entrance, as the game's motions name it.
        const std::int32_t kTicketMotion = park::motion_number("handticket").value_or(park::motion::kIdle);
        /// How riders sit in a car, as the game's motions name it.
        const std::int32_t kRideMotion = park::motion_number("ridecar").value_or(park::motion::kIdle);
        /// What party members standing together in a ring do, in turns: chatting, laughing, nodding, pointing.
        const std::array<std::int32_t, 4> kChatMotions { park::motion::kIdle, park::motion_number("laugh").value_or(park::motion::kIdle), park::motion_number("nodhead").value_or(park::motion::kIdle), park::motion_number("point").value_or(park::motion::kIdle) };
        constexpr float kChatTurnSeconds = 4; ///< How long one of them keeps at one thing

        /// What guests who have queued for ages do now and then: look at their watch, tap a foot.
        const std::array<std::int32_t, 2> kImpatientMotions { park::motion_number("lookwatch").value_or(park::motion::kIdle), park::motion_number("tapfoot").value_or(park::motion::kIdle) };
        constexpr std::size_t kImpatientEvery = 3; ///< Of their turns, one is impatient; the others idle

        /// The motion of a simulated guest standing still: something from kChatMotions while their party
        /// stands together, now and then something impatient after queueing for ages, else idling.
        std::int32_t chat_motion(const ParkScene& scene, const ShownPerson& shown)
        {
            const auto& r = scene.sim.registry;
            // Each by turns, offset by who they are so they do not all move at once.
            const auto turn = static_cast<std::size_t>(scene.clock / kChatTurnSeconds) + entt::to_integral(shown.guest);
            if (const auto& errand = r.get<simulation::Errand>(shown.guest); errand.kind == simulation::ErrandKind::Queueing && errand.queued.value >= simulation::kLongQueue)
                return turn % kImpatientEvery == 0 ? kImpatientMotions[turn / kImpatientEvery % kImpatientMotions.size()] : park::motion::kIdle;
            const auto* member = r.try_get<simulation::Member>(shown.guest);
            if (member == nullptr || r.get<simulation::Party>(member->party).huddle == 0)
                return park::motion::kIdle;
            return kChatMotions[turn % kChatMotions.size()];
        }

        /// What vendors do: take the money from a rider handing over their ticket within kServeReach, else stand
        /// bored, now and then stacking the shelves.
        const std::int32_t kTakeMoneyMotion = park::motion_number("takemoney").value_or(park::motion::kIdle);
        const std::int32_t kBoredMotion = park::motion_number("idlebored").value_or(park::motion::kIdle);
        const std::int32_t kShelvesMotion = park::motion_number("idlestackshelfs").value_or(park::motion::kIdle);
        constexpr float kServeReach = 3;         ///< From a vendor to the ticket spot they serve
        constexpr float kVendorTurnSeconds = 5;  ///< How long a vendor keeps at one idle thing
        constexpr std::size_t kShelvesEvery = 6; ///< Of a vendor's idle turns, one stacks the shelves

        /// The motion of vendor `shown`, standing at `at`, and how far into it they are.
        std::pair<std::int32_t, float> vendor_motion(const ParkScene& scene, const ShownPerson& shown, const glm::vec3& at)
        {
            for (const auto [g, walk, pose] : scene.sim.registry.view<const simulation::RideWalk, const simulation::Pose>().each())
                if (!walk.off && walk.next == 1 && walk.pause.value > 0 && glm::distance(pose.position, at) < kServeReach)
                    return { kTakeMoneyMotion, simulation::kTuning.queues.ticket.value - walk.pause.value };
            const float turn = scene.clock / kVendorTurnSeconds + static_cast<float>(entt::to_integral(shown.guest));
            const auto whole = static_cast<std::size_t>(turn);
            return { whole % kShelvesEvery == 0 ? kShelvesMotion : kBoredMotion, (turn - static_cast<float>(whole)) * kVendorTurnSeconds };
        }

        /// Places the people near enough to see that stand still, or those that move and may be in `view`, as
        /// they are at `clock`, at `levels`. With `simulate`, the simulated guests count as moving and stand
        /// where the simulation has them. The still ones read only what is set when the park loads besides the
        /// arguments, so they may be placed beside the frame; the moving ones only on it.
        std::vector<park::PlacedPerson> place_people(const ParkScene& scene, const park::ObjectLevels& levels, float clock, bool moving, const glm::mat4& view, bool simulate)
        {
            std::vector<park::PlacedPerson> placed;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                const bool simulated = simulate && shown.guest != entt::null;
                if (levels[i] == park::kNotDrawn || (simulated || !shown.walking) != moving)
                    continue;
                park::PlacedPerson pose = shown.pose;
                pose.level = static_cast<std::size_t>(levels[i]);
                pose.time = shown.walking ? shown.saved_time : shown.saved_time + clock * shown.speed;
                if (simulated)
                {
                    const auto& r = scene.sim.registry;
                    const simulation::Pose at = simulation::between_steps(r.get<simulation::Pose>(shown.guest), scene.sim_time / simulation::kTickSeconds);
                    if (r.all_of<simulation::Left>(shown.guest))
                        continue; // Gone home since the levels were taken
                    if (const auto* staff = r.try_get<simulation::Staff>(shown.guest); staff != nullptr && staff->role == simulation::StaffRole::Vendor)
                    {
                        std::tie(pose.motion, pose.time) = vendor_motion(scene, shown, glm::vec3(pose.world[3]));
                        if (!moving || maybe_in_view(view, glm::vec3(pose.world[3])))
                            placed.push_back(pose);
                        continue;
                    }
                    const auto& stride = r.get<simulation::Stride>(shown.guest);
                    const auto kind = r.get<simulation::Errand>(shown.guest).kind;
                    pose.time = shown.saved_time + clock * shown.speed;
                    const auto* ride_walk = r.try_get<simulation::RideWalk>(shown.guest);
                    if (kind == simulation::ErrandKind::Riding && ride_walk == nullptr)
                        continue; // Seated riders go with their cars (pose_riders); the rest wait out of sight
                    const simulation::Pose& now = r.get<simulation::Pose>(shown.guest);
                    const bool at_ticket = ride_walk != nullptr && !ride_walk->off && ride_walk->next == 1 && ride_walk->pause.value > 0;
                    const bool walking = !at_ticket && (ride_walk != nullptr || stride.from != stride.to || now.was != now.position);
                    // The walk goes as far into its motion as the ground covered allows, so the feet stay put
                    // however fast the guest goes.
                    if (walking && shown.pace > 0)
                        pose.time = at.walked.value / shown.pace;
                    // People are modelled facing +z, which the heading turns towards the way they go.
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = walking ? park::motion::kWalk : chat_motion(scene, shown);
                    if (at_ticket)
                    {
                        pose.motion = kTicketMotion;
                        pose.time = simulation::kTuning.queues.ticket.value - ride_walk->pause.value;
                    }
                    if (const auto* gesture = r.try_get<simulation::Gesture>(shown.guest))
                    {
                        // By the names the people's meshes give these motions, in GestureKind's order.
                        constexpr std::array<std::string_view, 6> kGestureMotions { "jumpjoy", "laugh", "nodhead", "shakehead", "dizzy", "vomit" };
                        pose.motion = park::motion_number(kGestureMotions[std::to_underlying(gesture->kind)]).value_or(park::motion::kIdle);
                        pose.time = simulation::kTuning.gestures.shown.value - gesture->left.value + scene.sim_time;
                    }
                    // An umbrella held up in the rain.
                    // shortcut: put away, out of sight, when dry; draw it carried closed if guests should be seen
                    // with one then.
                    const auto* weather = r.ctx().find<simulation::Weather>();
                    if (weather != nullptr && weather->rain && r.all_of<simulation::Umbrella>(shown.guest))
                    {
                        const auto* visitor = r.try_get<simulation::Visitor>(shown.guest);
                        const auto& umbrella = scene.umbrellas[visitor != nullptr && visitor->age == simulation::AgeGroup::Child ? 1 : 0];
                        pose.held = umbrella ? &*umbrella : nullptr;
                        // shortcut: standing guests hold it up in the walk's first pose, legs mid-step; upgrade if a
                        // standing motion with an umbrella turns up.
                        pose.motion = kUmbrellaMotion;
                        if (!walking)
                            pose.time = 0;
                    }
                }
                if (moving && !maybe_in_view(view, glm::vec3(pose.world[3])))
                    continue;
                placed.push_back(pose);
            }
            return placed;
        }

        /// Starts posing the riders seated in the ride cars away from the frame, each car's in the car's own
        /// space; place_cars moves them with their cars. Riders drawn at no level of detail are left out.
        std::future<RidersBuild> pose_riders(const ParkScene& scene)
        {
            std::map<std::size_t, std::vector<park::PlacedPerson>> by_car;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < scene.people_levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                if (shown.guest == entt::null || scene.people_levels[i] == park::kNotDrawn)
                    continue;
                const auto seat = seat_of(scene, shown.guest);
                if (!seat)
                    continue;
                park::PlacedPerson pose = shown.pose;
                pose.level = static_cast<std::size_t>(scene.people_levels[i]);
                pose.time = shown.saved_time + scene.clock * shown.speed;
                pose.world = seat->second;
                pose.motion = kRideMotion;
                by_car[seat->first].push_back(pose);
            }
            return std::async(std::launch::async, [&library = *scene.person_models, by_car = std::move(by_car)] {
                std::vector<park::ObjectMesh> alone;
                RidersBuild built;
                for (const auto& [car, riders] : by_car)
                {
                    alone.push_back(park::person_mesh(library, riders));
                    built.cars.push_back(car);
                }
                built.riders = park::gathered_meshes(alone);
                return built;
            });
        }

        constexpr float kGateHold = 3; ///< How long a ride's gate stays open after its last rider walks through

        /// Turns and moves the bones of the boned flat rides as far into their motions as their rides are, and
        /// the seats they carry.
        void swing_rides(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view)
        {
            for (ParkScene::BonedRide& boned : scene.boned_rides)
            {
                const glm::vec3 at(boned.placed[3]);
                if ((boned.ride == entt::null || boned.gate) && (glm::distance(eye, at) > kPropMoveDistance || !maybe_in_view(view, at, boned.reach)))
                    continue;
                const auto model = scene.models.find(park::model_key(scene.objects[boned.object]));
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
                                boned.open_until = scene.clock + kGateHold;
                    const std::size_t now = (*boned.gate)[boned.step];
                    if (scene.clock - boned.since >= (now < seconds.size() ? seconds[now] : 0.0f))
                    {
                        boned.step = park::next_gate_motion(boned.step, scene.clock < boned.open_until);
                        boned.since = scene.clock;
                    }
                    playing = { (*boned.gate)[boned.step], scene.clock - boned.since };
                }
                else
                {
                    const auto* cycle = scene.simulate && boned.ride != entt::null ? scene.sim.registry.try_get<simulation::CycleRide>(boned.ride) : nullptr;
                    const auto into = cycle != nullptr ? simulation::seconds_into_cycle(*cycle) : std::nullopt;
                    const float length = cycle != nullptr ? simulation::cycle_seconds(*cycle) : 0.0f;
                    playing = park::flat_ride_motion(seconds, length, into ? std::min(*into + scene.sim_time, length) : 0.0f, into.has_value(), scene.clock);
                }
                const auto [motion, time] = playing;
                const std::vector<glm::mat4> moves = park::bone_moves(*model->second.rig, motion, time);
                std::vector<glm::mat4> places(boned.bones.size());
                for (std::size_t p = 0; p < places.size(); ++p)
                    places[p] = boned.placed * moves[static_cast<std::size_t>(boned.bones[p])];
                park::move_meshes(boned.mesh, places);
                const auto& seat_bones = model->second.seat_bones;
                for (std::size_t seat = 0; seat < std::min(boned.seats, seat_bones.size()); ++seat)
                    if (seat_bones[seat] >= 0 && static_cast<std::size_t>(seat_bones[seat]) < moves.size())
                        scene.seat_carriers[boned.first_seat + seat] = boned.placed * moves[static_cast<std::size_t>(seat_bones[seat])];
            }
        }

        /// The band and side of each of `owners`' vertices: `side` 0 for objects drawn whole.
        std::vector<std::pair<glm::vec4, glm::vec2>> crossing_bands(const ParkScene& scene, const park::LevelPlan& plan, std::span<const std::uint32_t> owners, float side)
        {
            std::vector<std::pair<glm::vec4, glm::vec2>> out;
            out.reserve(owners.size());
            for (const std::uint32_t o : owners)
                out.push_back({ glm::vec4(scene.anchors[o], plan.bands[o].x), glm::vec2(plan.bands[o].y, side) });
            return out;
        }

        /// Puts `shapes` in `shown`'s buffers, dropping what they held.
        void show_shapes(render::Device& device, ParkScene& scene, ShownObjects& shown, ObjectShapes&& shapes, const park::LevelPlan& plan, float side = 0)
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

    /// Where `shown` stands now: where the simulation has walked it while that runs, else where it was saved.
    /// Reads the simulation, so only on the frame's thread.
    bool maybe_in_view(const glm::mat4& transform, const glm::vec3& at, float reach)
    {
        constexpr float kSpare = 1.3f;
        const glm::vec4 clip = transform * glm::vec4(at + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
        const float edge = clip.w * kSpare + reach;
        return clip.w > -reach && std::abs(clip.x) <= edge && std::abs(clip.y) <= edge;
    }

    glm::vec3 standing_at(const ParkScene& scene, const ShownPerson& shown)
    {
        if (scene.simulate && shown.guest != entt::null)
            if (const auto seat = seat_place(scene, shown.guest))
                return glm::vec3((*seat)[3]);
        if (scene.simulate && shown.guest != entt::null)
            return simulation::between_steps(scene.sim.registry.get<simulation::Pose>(shown.guest), scene.sim_time / simulation::kTickSeconds).position;
        return glm::vec3(shown.pose.world[3]);
    }

    /// Moves the ride cars to where the simulation has their trains, or back to where the park kept them
    /// while it does not run.
    void place_cars(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view)
    {
        std::vector<float> moved(scene.cars.size()); // Along its course since the park kept it, by car
        for (std::size_t i = 0; i < scene.cars.size(); ++i)
        {
            const std::uint64_t id = scene.cars[i].id;
            const park::CarSpot spot = scene.car_spots[i];
            glm::mat4 place = scene.saved_cars.at(id);
            const entt::entity ride = scene.simulate && spot.ride < scene.ride_entities.size() ? scene.ride_entities[spot.ride] : entt::null;
            const auto* fleet = ride != entt::null ? scene.sim.registry.try_get<simulation::Trains>(ride) : nullptr;
            const auto* boats = ride != entt::null ? scene.sim.registry.try_get<simulation::Boats>(ride) : nullptr;
            if (boats && spot.train < boats->boats.size() && boats->boats[spot.train].state != simulation::Sailing::Docked)
            {
                // Out on the water: turned from the way the park kept it to the boat's heading, at its height.
                const simulation::Boat& boat = boats->boats[spot.train];
                const glm::vec2 at = boat.at + boat.heading * boat.speed * scene.sim_time;
                const glm::vec3 kept(place[3]);
                const glm::vec2 kept_way = glm::vec2(place[2].x, place[2].z);
                const float turn = glm::length(kept_way) > 0 ? std::atan2(kept_way.x, kept_way.y) - std::atan2(boat.heading.x, boat.heading.y) : 0.0f;
                place = glm::translate(glm::mat4(1.0f), glm::vec3(at.x, kept.y, at.y)) * glm::rotate(glm::mat4(1.0f), -turn, glm::vec3(0, 1, 0)) * glm::translate(glm::mat4(1.0f), -kept) * place;
            }
            else if (fleet && spot.train < fleet->trains.size() && scene.courses[spot.ride])
            {
                const simulation::Coaster& train = fleet->trains[spot.train];
                // Between ticks, on as far as the train's speed takes it, from where the park kept it.
                const float at = train.at + (train.running ? train.speed * scene.sim_time : 0.0f);
                moved[i] = at - scene.rides.rides[spot.ride].trains[spot.train].distance;
                place = park::moved_along(*scene.courses[spot.ride], place, { spot.front, spot.rear }, moved[i]);
            }
            scene.car_places[i] = place;
        }
        // Arms take the angles their track holds where the car is, the joints at rest where it holds none.
        std::size_t joints = 0;
        for (const ParkScene::ArmCar& arm : scene.arm_cars)
            joints += arm.joints.size();
        scene.car_mesh_places.resize(scene.car_places.size() + joints);
        std::ranges::copy(scene.car_places, scene.car_mesh_places.begin());
        auto joint_place = scene.car_mesh_places.begin() + static_cast<std::ptrdiff_t>(scene.car_places.size());
        for (ParkScene::ArmCar& arm : scene.arm_cars)
        {
            const park::CarSpot spot = scene.car_spots[arm.car];
            std::optional<park::ArmAngles> angles;
            if (const save::Ride& ride = scene.rides.rides[spot.ride]; ride.track && scene.courses[spot.ride])
            {
                std::vector<std::optional<park::ArmAngles>> held;
                for (const save::TrackPiece& piece : std::span(ride.track->pieces).subspan(std::min(scene.courses[spot.ride]->first_piece, ride.track->pieces.size())))
                    held.push_back(piece.arm_angles);
                angles = park::arm_angles_at(*scene.courses[spot.ride], held, (spot.front + spot.rear) / 2 + moved[arm.car]);
            }
            const std::vector<glm::mat4> moves = park::arm_moves(arm.joints, angles ? std::span<const float>(*angles) : std::span<const float> { });
            for (const glm::mat4& move : moves)
                *joint_place++ = scene.car_places[arm.car] * move;
            arm.seats = moves.empty() ? glm::mat4(1.0f) : moves.back();
        }
        profiling::section("Rides", [&] { swing_rides(scene, eye, view); });
        profiling::section("Mesh", [&] {
            park::move_meshes(scene.car_meshes, scene.car_mesh_places);
            scene.rider_places.resize(scene.rider_cars.size());
            for (std::size_t k = 0; k < scene.rider_cars.size(); ++k)
                scene.rider_places[k] = carrier_place(scene, scene.rider_cars[k]);
            park::move_meshes(scene.riders, scene.rider_places);
        });
    }

    /// Builds the people meshes again as often as they need, all beside the frame: the moving ones as soon as
    /// the last are done, the riders a few times a second, the still ones when a person's level of detail
    /// changes. The still ones' levels are only taken on with them, so the moving ones keep to the same levels.
    void refresh_people(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, float elapsed)
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
            scene.since_people_levels = 0;
            // Guests who came in at the gate since: dressed from the wardrobe for their age and sex, and kept
            // with the park's people like those it was saved with. Here, as no build reads the shown people.
            auto& r = scene.sim.registry;
            for (const auto [e, visitor, mood] : r.view<const simulation::Visitor, const simulation::Mood>(entt::exclude<simulation::Left>).each())
            {
                if (!scene.simulate || scene.wardrobe.empty() || !r.all_of<simulation::Stride>(e) || !scene.known_guests.insert(e).second)
                    continue;
                const auto type = static_cast<save::PersonType>(std::to_underlying(visitor.age)); // The same numbers
                const save::Sex sex = visitor.female ? save::Sex::Female : save::Sex::Male;
                std::array<const Outfit*, 3> worn { };
                for (std::size_t section = 0; section < worn.size(); ++section)
                {
                    std::vector<const Outfit*> fits;
                    for (const Outfit& o : scene.wardrobe)
                        if (o.type == type && o.sex == sex && o.section == section)
                            fits.push_back(&o);
                    if (!fits.empty())
                        worn[section] = fits[random::below(scene.looks, fits.size())];
                }
                if (std::ranges::contains(worn, nullptr))
                    continue;
                save::Guest& kept = scene.people.guests.emplace_back();
                kept.in_park = true;
                kept.mood_now.happiness = mood.happiness.value;
                kept.person.type = type;
                kept.person.sub_type = std::to_underlying(sex);
                kept.person.name = visitor.name.empty() ? std::format("Guest {}", scene.people.guests.size()) : visitor.name;
                save::Appearance& a = kept.person.appearance;
                a.head = { worn[0]->id };
                a.body = { worn[1]->id };
                a.legs = { worn[2]->id };
                ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .saved = scene.people.guests.size() - 1, .guest = e });
                park::PersonLook& look = shown.pose.look;
                look.head = worn[0]->set;
                look.body = worn[1]->set;
                look.legs = worn[2]->set;
                for (std::size_t i = 0; i < look.colours.size(); ++i)
                    look.colours[i] = a.palettes[i] = static_cast<std::uint8_t>(random::below(scene.looks, kColourCells));
                shown.pose.motion = park::motion::kWalk;
            }
            // The pace of each shown person's walk, measured once for each legs set.
            if (scene.person_models)
            {
                for (ShownPerson& shown : scene.shown_people)
                {
                    const park::PartSet* legs = shown.pose.look.legs;
                    if (shown.pace > 0 || legs == nullptr)
                        continue;
                    auto [pace, fresh] = scene.walk_paces.try_emplace(legs, 0.0f);
                    if (fresh)
                        for (const std::size_t piece : legs->levels[0].fixed)
                            if (const auto measured = park::walk_pace(scene.person_models->piece(piece)); measured && pace->second == 0)
                                pace->second = *measured;
                    shown.pace = pace->second;
                }
            }
            park::ObjectLevels levels(scene.shown_people.size(), park::kNotDrawn);
            for (std::size_t i = 0; i < levels.size(); ++i)
            {
                if (const ShownPerson& p = scene.shown_people[i]; scene.simulate && p.guest != entt::null && scene.sim.registry.all_of<simulation::Left>(p.guest))
                    continue; // Gone home
                const float distance = glm::distance(eye, standing_at(scene, scene.shown_people[i]));
                const auto level = std::ranges::find_if(kPersonDistances, [&](float d) { return distance <= d; });
                if (level != kPersonDistances.end())
                    levels[i] = static_cast<std::int8_t>(level - kPersonDistances.begin());
            }
            if (levels != scene.people_levels)
            {
                scene.people_build = std::async(std::launch::async, [&scene, levels = std::move(levels), view, simulate = scene.simulate] {
                    profiling::register_thread("PeopleWorker");
                    PeopleBuild built;
                    built.mesh = profiling::section("Still", [&] { return park::person_mesh(*scene.person_models, place_people(scene, levels, 0, false, view, simulate)); });
                    built.levels = levels;
                    built.simulate = simulate;
                    profiling::unregister_thread();
                    return built;
                });
            }
        }
        // The people in motion are posed away from the frame, again as soon as the last posing is done.
        if (ready(scene.moving_build))
            scene.moving_people = scene.moving_build.get();
        if (!scene.moving_build.valid())
        {
            const profiling::Scope zone("Moving");
            scene.moving_build = std::async(std::launch::async, [&library = *scene.person_models, placed = place_people(scene, scene.people_levels, scene.clock, true, view, scene.simulate)] { return park::person_mesh(library, placed); });
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
            scene.since_riders = 0;
            scene.riders_build = profiling::section("Riders", [&] { return pose_riders(scene); });
        }
    }

    /// The objects' meshes at the levels seen from `eye`, or nothing when those are `shown`; without the
    /// supports unless `supports`. Reads only what is set when the park loads besides its arguments, so it
    /// may run beside the frame.
    std::optional<ObjectBuild> build_objects(const ParkScene& scene, const glm::vec3& eye, const park::LevelPlan& shown, float band, bool supports)
    {
        ObjectBuild built;
        built.plan = profiling::section("Levels", [&] { return park::plan_levels(scene.objects, scene.models, scene.anchors, eye, band); });
        const auto leave_out = [&](std::size_t i) {
            built.plan.steady[i] = park::kNotDrawn;
            built.plan.finer[i] = park::kNotDrawn;
            built.plan.coarser[i] = park::kNotDrawn;
        };
        std::vector<std::size_t> moving;
        for (const ParkScene::BonedRide& boned : scene.boned_rides)
            moving.push_back(boned.object);
        // Drawn apart, moving; a twin the save keeps of one would stand still in it.
        for (const auto& list : { moving, park::twins(scene.objects, moving) })
            for (const std::size_t i : list)
                leave_out(i);
        for (std::size_t i = 0; !supports && i < scene.objects.size() && i < built.plan.steady.size(); ++i)
        {
            if (scene.objects[i].overlay == park::kSupportOverlay)
                leave_out(i);
        }
        if (built.plan == shown)
            return std::nullopt;
        for (auto [shapes, levels] : { std::pair { &built.steady, &built.plan.steady }, std::pair { &built.finer, &built.plan.finer }, std::pair { &built.coarser, &built.plan.coarser } })
        {
            shapes->mesh = profiling::section("Mesh", [&] { return park::object_mesh(scene.objects, scene.models, scene.grid, *levels, { }, scene.leans, scene.fitted); });
            shapes->billboards = profiling::section("Gather", [&] { return park::gather_billboards(scene.objects, scene.models, scene.anchors, *levels); });
        }
        return built;
    }

    /// `vertices` with the bands of `bands`, for the crossing shaders.
    std::vector<CrossingVertex> crossing_vertices(std::span<const park::GroundVertex> vertices, std::span<const std::pair<glm::vec4, glm::vec2>> bands)
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
        scene.levels = std::move(built.plan);
        show_shapes(device, scene, scene.steady_objects, std::move(built.steady), scene.levels);
        const float side = scene.tint_crossing ? 2.0f : 1.0f; // Twice as far tints them (lit.slang)
        show_shapes(device, scene, scene.finer_objects, std::move(built.finer), scene.levels, -side);
        show_shapes(device, scene, scene.coarser_objects, std::move(built.coarser), scene.levels, side);
        scene.billboards_eye.reset();
    }

    /// Takes on the objects' meshes once built, and looks at their levels again every
    /// kObjectRebuildInterval, building the meshes again beside the frame when one differs.
    void refresh_objects(render::Device& device, ParkScene& scene, const glm::vec3& eye, float elapsed)
    {
        if (ready(scene.object_build))
        {
            if (auto built = scene.object_build.get())
                show_objects(device, scene, std::move(*built));
        }
        scene.since_rebuild += elapsed;
        if (scene.since_rebuild < kObjectRebuildInterval || scene.object_build.valid())
            return;
        scene.since_rebuild = 0;
        scene.object_build = std::async(std::launch::async, [&scene, eye, shown = scene.levels, band = scene.blend_band, supports = scene.show_supports] {
            profiling::register_thread("ObjectWorker");
            auto built = profiling::section("Objects", [&] { return build_objects(scene, eye, shown, band, supports); });
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
        return ui::interface_scale(static_cast<float>(device.extent().height), scene.scale);
    }

    float aspect(const render::Device& device)
    {
        const render::Extent e = device.extent();
        return e.height > 0 ? static_cast<float>(e.width) / static_cast<float>(e.height) : 1.0f;
    }

} // namespace rct3::scene
