// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_refresh.hpp"

#include "logging/log.hpp"
#include "park/chain_lifts.hpp"
#include "park/object_models.hpp"
#include "park/object_pick.hpp"
#include "park/person_motions.hpp"
#include "park/ride_cars.hpp"
#include "park/staff_motions.hpp"
#include "park/water_flow.hpp"
#include "profiling/profiler.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/park_patrol.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/crowds.hpp"
#include "simulation/import_park.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
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

namespace rct3::scene
{

    namespace
    {

        /// Starts rings spreading on the pool water at `at` in `delay` seconds, where someone falls in with
        /// `strength` (park::entry_rings). When there are already the most, they take the place of the lowest, if
        /// those are lower than the new ones start.
        void start_rings(ParkScene& scene, glm::vec3 at, float strength, float delay = 0)
        {
            constexpr std::size_t kMostRings = park::kMaxPoolWaves / 2; ///< Chosen by eye: room left for the machines' and drawn waves
            const auto rings = park::entry_rings(scene.pools, scene.grid, at, strength, scene.park_clock + delay);
            if (!rings)
                return;
            if (scene.rings.size() < kMostRings)
            {
                scene.rings.push_back(*rings);
                return;
            }
            const auto crest = [&](const park::PoolWave& w) { return w.start > scene.park_clock ? w.strength * park::kPoolWaveLook.ring_height : park::ring_crest(w, scene.park_clock); };
            if (const auto lowest = std::ranges::min_element(scene.rings, { }, crest); crest(*lowest) < rings->strength * park::kPoolWaveLook.ring_height)
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
                const save::PlacedObject& o = scene.objects[object->second];
                const auto model = scene.models.find(park::model_ref(o));
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
                const auto model = scene.models.find(park::model_ref(scene.cars[car]));
                const std::size_t seats = model != scene.models.end() ? model->second.seat_places.size() : 0;
                if (seat < seats)
                    return std::pair { car, model->second.seat_places[seat] };
                seat -= seats;
            }
            return std::nullopt;
        }

        /// `shown` as they look now: in their swimwear inside a pool complex, riding its slides or held or
        /// falling out of it, their clothes elsewhere (simulation::in_swimwear).
        park::PlacedPerson dressed(const ShownPerson& shown)
        {
            park::PlacedPerson pose = shown.pose;
            if (shown.swimwear)
            {
                pose.look.swimwear = true;
                pose.look.body = shown.swim_body != nullptr ? shown.swim_body : pose.look.body;
                pose.look.legs = shown.swim_legs != nullptr ? shown.swim_legs : pose.look.legs;
            }
            return pose;
        }

        /// Where simulated guest `guest` sits in the park while riding, as seat_of finds it.
        std::optional<glm::mat4> seat_place(const ParkScene& scene, entt::entity guest)
        {
            if (const auto seat = seat_of(scene, guest))
                return seat->first == kNoCar ? seat->second : carrier_place(scene, seat->first) * seat->second;
            return std::nullopt;
        }

        /// Where a guest sits on a bench in the park, or stands to fire a water cannon: the seat their place on it
        /// comes to on its model. Nothing once the item is gone, or when its model has no such seat.
        /// shortcut: looks for the bench among all the park's objects, upgrade to a lookup if sitters show in profiles.
        std::optional<glm::mat4> bench_seat(const ParkScene& scene, entt::entity guest)
        {
            const auto& r = scene.sim.registry;
            const auto* linger = r.try_get<simulation::Linger>(guest);
            const auto* item = linger != nullptr && r.valid(linger->bench) ? r.try_get<simulation::PathItem>(linger->bench) : nullptr;
            if (item == nullptr || linger->at == simulation::PathItemKind::Sight || linger->at == simulation::PathItemKind::PhotoSpot)
                return std::nullopt;
            const auto o = std::ranges::find(scene.objects, item->object, &save::PlacedObject::id);
            const auto model = o != scene.objects.end() && !o->hidden ? scene.models.find(park::model_ref(*o)) : scene.models.end();
            if (model == scene.models.end() || linger->seat >= model->second.seat_places.size())
                return std::nullopt;
            return park::placed_transform(*o, &model->second, scene.grid, scene.fitted) * model->second.seat_places[linger->seat];
        }

        /// Where path item `item` stands in the park, as its placed object; nothing once it is gone.
        std::optional<glm::vec3> item_at(const ParkScene& scene, entt::entity item)
        {
            const auto& r = scene.sim.registry;
            const auto* path_item = r.valid(item) ? r.try_get<simulation::PathItem>(item) : nullptr;
            const auto o = path_item != nullptr ? std::ranges::find(scene.objects, path_item->object, &save::PlacedObject::id) : scene.objects.end();
            if (o == scene.objects.end() || o->hidden)
                return std::nullopt;
            const auto model = scene.models.find(park::model_ref(*o));
            return glm::vec3(park::placed_transform(*o, model != scene.models.end() ? &model->second : nullptr, scene.grid, scene.fitted)[3]);
        }

        /// How guests sit on a bench, as the game's motions name them: down onto it, sitting, turned to talk to
        /// someone on the seat to one side or the other, and up again.
        const std::int32_t kSitDownMotion = park::motion_number("sitbenchin").value_or(park::motion::kIdle);
        const std::int32_t kSitMotion = park::motion_number("sitbenchloop").value_or(park::motion::kIdle);
        const std::array<std::int32_t, 2> kSitChatMotions { park::motion_number("sitbenchconvl").value_or(park::motion::kIdle), park::motion_number("sitbenchconvr").value_or(park::motion::kIdle) };
        const std::int32_t kStandUpMotion = park::motion_number("sitbenchout").value_or(park::motion::kIdle);
        const std::int32_t kSitEatMotion = park::motion_number("sitbencheatlhand").value_or(park::motion::kIdle);
        const std::int32_t kSitDrinkMotion = park::motion_number("sitbenchdrinkl").value_or(park::motion::kIdle);
        constexpr float kStepToSeat = 0.8f; ///< Chosen by eye: seconds from the path to the seat and back
        /// How guests fire a water cannon, as the game's motions name them: taking hold, firing, letting go.
        const std::int32_t kCannonInMotion = park::motion_number("watercannonin").value_or(park::motion::kIdle);
        const std::int32_t kCannonMotion = park::motion_number("watercannonloop").value_or(park::motion::kIdle);
        const std::int32_t kCannonOutMotion = park::motion_number("watercannonout").value_or(park::motion::kIdle);
        /// How a party poses at a view, as the game's motions name them: one filming the rest, teens slouching.
        const std::int32_t kVideoInMotion = park::motion_number("videoin").value_or(park::motion::kIdle);
        const std::int32_t kVideoMotion = park::motion_number("videoloop").value_or(park::motion::kIdle);
        const std::int32_t kVideoOutMotion = park::motion_number("videoout").value_or(park::motion::kIdle);
        const std::int32_t kSlouchInMotion = park::motion_number("slouchin").value_or(park::motion::kIdle);
        const std::int32_t kSlouchMotion = park::motion_number("slouchloop").value_or(park::motion::kIdle);
        const std::int32_t kSlouchOutMotion = park::motion_number("slouchout").value_or(park::motion::kIdle);

        /// What a guest holds of what they carry from a stall: it while there is some left, then its empty; nothing
        /// once only empties in their pockets are left, or when its look is not known.
        const park::ObjectModel* held_model(const ParkScene& scene, const simulation::Carried& carried)
        {
            const auto& r = scene.sim.registry;
            const auto* shop = r.valid(carried.stall) ? r.try_get<simulation::Shop>(carried.stall) : nullptr;
            const auto look = shop != nullptr && carried.ware < shop->menu.size() ? scene.carried_looks.find(shop->menu[carried.ware].symbol) : scene.carried_looks.end();
            const auto* model = look == scene.carried_looks.end() || !carried.held ? nullptr : carried.bites > 0 ? &look->second.full : &look->second.empty;
            return model != nullptr && *model ? &**model : nullptr;
        }

        /// How guests go on foot in each gait (simulation::Gait), as the game's motions name them.
        const std::array<std::int32_t, kGaits> kGaitMotions { park::motion::kWalk, park::motion_number("run").value_or(park::motion::kWalk), park::motion_number("walktired").value_or(park::motion::kWalk), park::motion_number("walksick").value_or(park::motion::kWalk), park::motion_number("walktoilet").value_or(park::motion::kWalk) };
        /// How guests walk holding an umbrella up, as the game's motions name it.
        const std::int32_t kUmbrellaMotion = park::motion_number("walkumbrella").value_or(park::motion::kWalk);
        /// How guests carry food or drink: walking with it held up, taking a bite or a sip on the way, and
        /// standing holding it, as the game's motions name them.
        const std::int32_t kCarryMotion = park::motion_number("walkumbrella").value_or(park::motion::kWalk);
        const std::int32_t kEatMotion = park::motion_number("walkeatlhand").value_or(park::motion::kWalk);
        const std::int32_t kDrinkMotion = park::motion_number("walkdrinklhand").value_or(park::motion::kWalk);
        const std::int32_t kHoldMotion = park::motion_number("holditem").value_or(park::motion::kIdle);
        constexpr float kBiteSeconds = 1.5f; ///< Chosen by eye: how long a bite or a sip shows
        /// How a guest hands over their ticket at a ride's entrance, as the game's motions name it.
        const std::int32_t kTicketMotion = park::motion_number("handticket").value_or(park::motion::kIdle);
        /// How a guest counts the cash a cash machine gave them, as the game's motions name it.
        const std::int32_t kCountCashMotion = park::motion_number("checkchange").value_or(park::motion::kIdle);
        /// How a person picked up hangs, enjoying it or cross, as the game's motions name it.
        const std::int32_t kDangleHappyMotion = park::motion_number("danglehappy").value_or(park::motion::kIdle);
        const std::int32_t kDangleCrossMotion = park::motion_number("dangleangry").value_or(park::motion::kIdle);
        /// How riders sit in a car, as the game's motions name it.
        const std::int32_t kRideMotion = park::motion_number("ridecar").value_or(park::motion::kIdle);
        /// Swimming along, and treading water while still.
        const std::int32_t kSwimMotion = park::motion_number("swimloop").value_or(park::motion::kIdle);
        const std::int32_t kTreadMotion = park::motion_number("swimtreadwaterloop").value_or(park::motion::kIdle);
        /// Sitting on their own float, still or paddling it along.
        const std::int32_t kFloatSitMotion = park::motion_number("inflatablesitloop").value_or(park::motion::kIdle);
        const std::int32_t kFloatPaddleMotion = park::motion_number("inflatablepaddleloop").value_or(park::motion::kIdle);
        const std::int32_t kTreadInMotion = park::motion_number("swimtreadwaterin").value_or(park::motion::kIdle);
        /// Diving in off the pool's edge or a small board, and climbing out over its wall or by a ladder.
        const std::int32_t kDiveMotion = park::motion_number("swimdive").value_or(park::motion::kIdle);
        const std::int32_t kClimbOutMotion = park::motion_number("swimout").value_or(park::motion::kIdle);
        const std::int32_t kOutByLadderMotion = park::motion_number("swimoutladder").value_or(park::motion::kIdle);
        /// Up a high diving board's ladder, and off its end: springing, falling and coming back up.
        const std::int32_t kClimbLadderMotion = park::motion_number("swimclimbladderloop").value_or(park::motion::kIdle);
        const std::int32_t kHighDiveInMotion = park::motion_number("swimdivehiin").value_or(park::motion::kIdle);
        const std::int32_t kHighDiveMotion = park::motion_number("swimdivehiloop").value_or(park::motion::kIdle);
        const std::int32_t kHighDiveOutMotion = park::motion_number("swimdivehiout").value_or(park::motion::kIdle);
        /// Lying down on a sun lounger, lying there and getting up.
        const std::int32_t kLieDownMotion = park::motion_number("sunloungerin").value_or(park::motion::kIdle);
        const std::int32_t kLoungeMotion = park::motion_number("sunloungerloop").value_or(park::motion::kIdle);
        const std::int32_t kGetUpMotion = park::motion_number("sunloungerout").value_or(park::motion::kIdle);
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
            const auto turn = static_cast<std::size_t>(scene.park_clock / kChatTurnSeconds) + entt::to_integral(shown.guest);
            if (const auto& errand = r.get<simulation::Errand>(shown.guest); errand.kind == simulation::ErrandKind::Queueing && errand.queued.value >= simulation::kLongQueue)
                return turn % kImpatientEvery == 0 ? kImpatientMotions[turn / kImpatientEvery % kImpatientMotions.size()] : park::motion::kIdle;
            const auto* member = r.try_get<simulation::Member>(shown.guest);
            if (member == nullptr || r.get<simulation::Party>(member->party).huddle == 0)
                return park::motion::kIdle;
            return kChatMotions[turn % kChatMotions.size()];
        }

        /// How long a guest at a stall's counter has been there, by what is left of their turn.
        float served_for(const simulation::Errand& errand)
        {
            return simulation::kTuning.facilities.served.value - static_cast<float>(errand.rest) * simulation::kTickSeconds;
        }

        /// What vendors do: serve a guest at their counter within kServeReach, then take their money; take the
        /// money from a rider handing over their ticket; else stand bored, now and then stacking the shelves.
        const std::int32_t kServeMotion = park::motion_number("serve").value_or(park::motion::kIdle);
        const std::int32_t kTakeMoneyMotion = park::motion_number("takemoney").value_or(park::motion::kIdle);
        const std::int32_t kBoredMotion = park::motion_number("idlebored").value_or(park::motion::kIdle);
        const std::int32_t kShelvesMotion = park::motion_number("idlestackshelfs").value_or(park::motion::kIdle);
        constexpr float kServeReach = 3;         ///< From a vendor to the ticket spot they serve
        constexpr float kVendorTurnSeconds = 5;  ///< How long a vendor keeps at one idle thing
        constexpr std::size_t kShelvesEvery = 6; ///< Of a vendor's idle turns, one stacks the shelves

        /// The motion of vendor `shown`, standing at `at`, and how far into it they are.
        std::pair<std::int32_t, float> vendor_motion(const ParkScene& scene, const ShownPerson& shown, const glm::vec3& at)
        {
            for (const auto [g, errand, pose] : scene.sim.registry.view<const simulation::Errand, const simulation::Pose>().each())
                if (errand.kind == simulation::ErrandKind::Buying && glm::distance(pose.position, at) < kServeReach)
                {
                    const float served = served_for(errand);
                    const float half = simulation::kTuning.facilities.served.value / 2;
                    return served < half ? std::pair { kServeMotion, served } : std::pair { kTakeMoneyMotion, served - half };
                }
            for (const auto [g, walk, pose] : scene.sim.registry.view<const simulation::RideWalk, const simulation::Pose>().each())
                if (!walk.off && walk.next == 1 && walk.pause.value > 0 && glm::distance(pose.position, at) < kServeReach)
                    return { kTakeMoneyMotion, simulation::kTuning.queues.ticket.value - walk.pause.value };
            const float turn = scene.park_clock / kVendorTurnSeconds + static_cast<float>(entt::to_integral(shown.guest));
            const auto whole = static_cast<std::size_t>(turn);
            return { whole % kShelvesEvery == 0 ? kShelvesMotion : kBoredMotion, (turn - static_cast<float>(whole)) * kVendorTurnSeconds };
        }

        /// Whether their swimwear shows a float of their own, an optional body piece whose picture names it.
        bool own_float(const park::PersonLibrary& library, const park::PlacedPerson& person)
        {
            if (!person.look.swimwear || person.look.body == nullptr)
                return false;
            const park::PartLevel& pieces = person.look.body->swim_levels[0];
            for (std::size_t k = 0; k < pieces.optional.size() && k < 32; ++k)
            {
                if ((person.look.optionals[1] >> k & 1u) != 0 && pieces.optional[k] != park::kNoPiece && library.piece(pieces.optional[k]).texture.contains("inflatable"))
                    return true;
            }
            return false;
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
                if (levels[i] == park::kNotDrawn || ((simulate && shown.guest != entt::null) || !shown.walking) != moving)
                    continue;
                std::optional<park::PlacedPerson> pose = pose_person(scene, i, clock, simulate);
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
            std::map<std::size_t, std::vector<park::PlacedPerson>> by_car;
            std::map<entt::entity, ParkScene::RiderTurn> turns;
            for (std::size_t i = 0; i < scene.shown_people.size() && i < scene.people_levels.size(); ++i)
            {
                const ShownPerson& shown = scene.shown_people[i];
                if (shown.guest == entt::null || scene.people_levels[i] == park::kNotDrawn)
                    continue;
                const auto seat = seat_of(scene, shown.guest);
                if (!seat)
                    continue;
                park::PlacedPerson pose = dressed(shown);
                pose.level = static_cast<std::size_t>(scene.people_levels[i]);
                pose.time = shown.saved_time + scene.park_clock * shown.speed;
                pose.world = seat->second;
                pose.motion = kRideMotion;
                // They sit as their car seats them, or on a flat ride as the ride does, reacting to the ride.
                std::optional<std::uint8_t> seating;
                if (seat->first < scene.cars.size())
                {
                    if (const auto model = scene.models.find(park::model_ref(scene.cars[seat->first])); model != scene.models.end())
                        seating = model->second.seating;
                }
                else if (const auto flat = scene.ride_seatings.find(scene.sim.registry.get<simulation::Errand>(shown.guest).target); flat != scene.ride_seatings.end())
                    seating = flat->second;
                if (const auto motions = seating ? park::rider_motions(*seating) : std::nullopt)
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
                    std::inplace_vector<park::Clip, 3> clips;
                    const auto add = [&](RideReaction reaction, const std::optional<std::int32_t>& hands, const std::optional<std::int32_t>& eyes) {
                        if (const auto& once = reaction == RideReaction::HandsUp ? hands : reaction == RideReaction::CoverEyes ? eyes : std::nullopt; once && turn.was != turn.now)
                        {
                            pose.motion = *once;
                            clips.push_back({ *once, park::motion_seconds(*scene.person_models, pose) });
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
                        const park::Clip at = park::clip_at(clips, scene.park_clock - turn.since);
                        pose.motion = at.motion;
                        pose.time = at.seconds;
                    }
                }
                by_car[seat->first].push_back(pose);
            }
            scene.rider_turns = std::move(turns);
            return std::async(std::launch::async, [&library = *scene.person_models, &runs = scene.rider_runs, &gathered = scene.rider_gathered, by_car = std::move(by_car)] {
                std::vector<std::span<const park::PlacedPerson>> groups;
                RidersBuild built;
                for (const auto& [car, riders] : by_car)
                {
                    groups.emplace_back(riders);
                    built.cars.push_back(car);
                }
                built.riders = park::carried_people_meshes(library, groups, runs, gathered);
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
                const auto model = scene.models.find(park::model_ref(scene.objects[boned.object]));
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
                        boned.step = park::next_gate_motion(boned.step, scene.park_clock < boned.open_until);
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
                    const float idle = scene.park_clock + (boned.ride == entt::null ? scene.objects[boned.object].motion_time : 0.0f);
                    playing = park::flat_ride_motion(seconds, length, into ? std::min(*into + scene.sim_time, length) : 0.0f, into.has_value(), idle);
                }
                const auto [motion, time] = playing;
                std::vector<glm::mat4> moves = park::bone_moves(*model->second.rig, motion, time);
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
                        const park::BoneRig& rig = *model->second.rig;
                        const auto [turret, nozzle] = *aim;
                        const glm::vec3 pivot(rig.rest_world[static_cast<std::size_t>(turret)][3]);
                        const glm::vec3 muzzle(rig.rest_world[static_cast<std::size_t>(nozzle)][3]);
                        const glm::vec3 ahead = glm::normalize(glm::vec3(muzzle.x - pivot.x, 0, muzzle.z - pivot.z));
                        constexpr float kSpeed = 11;     ///< Chosen by eye: metres a second the water leaves at
                        constexpr float kTurn = 2.5f;    ///< Chosen by eye: radians a second the gun turns
                        constexpr float kHighest = 0.8f; ///< Chosen by eye: most the gun tips up, and down
                        constexpr float kLowest = -0.35f;
                        const float swing = simulation::kTuning.surroundings.cannon_swing;
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
                                const float g = park::kJetGravity;
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
                park::move_meshes(boned.mesh, places);
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
                            if (const auto k = std::ranges::find(scene.objects, item.object, &save::PlacedObject::id); k != scene.objects.end())
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
                    const auto a = scene.models.find(park::model_ref(scene.objects[from]));
                    const auto b = scene.models.find(park::model_ref(scene.objects[to]));
                    if (a == scene.models.end() || b == scene.models.end() || b->second.nozzles.empty())
                        return;
                    const glm::mat4 at = park::placed_transform(scene.objects[from], &a->second, scene.grid, scene.fitted);
                    const glm::mat4 into = park::placed_transform(scene.objects[to], &b->second, scene.grid, scene.fitted);
                    const glm::vec3 step = glm::vec3(into[3]) - glm::vec3(at[3]);
                    for (const glm::vec3& nozzle : a->second.nozzles)
                    {
                        const glm::vec3 out(at * glm::vec4(nozzle, 1));
                        const auto landing = std::ranges::min(b->second.nozzles | std::views::transform([&](const glm::vec3& n) { return glm::vec3(into * glm::vec4(n, 1)); }), { }, [&](const glm::vec3& n) { return glm::distance(n, out + step); });
                        if (const auto thrown = park::throw_to(out, landing, kJumpAngle))
                        {
                            // Chosen by eye: a thin, even stream.
                            ParkScene::Jet& jet = scene.jets.emplace_back(ParkScene::Jet { .object = from, .to = to, .pours = scene.park_clock + kPour, .lands = scene.park_clock + thrown->seconds + kPour, .water = park::WaterJet(thrown->seconds + kPour, 0.04f, 0) });
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

    std::string_view slide_motion(float heading, glm::vec2 drift, bool staff)
    {
        if (staff)
            return "dangleangry"; // Staff meshes have no sliding motions
        const float ahead = glm::length(drift) > 0 ? glm::dot(glm::vec2 { std::sin(heading), std::cos(heading) }, glm::normalize(drift)) : 0.0f;
        constexpr float kFacing = 0.5f; ///< Chosen by eye: how squarely they face down or up the slope to slide on their front or back
        return ahead > kFacing ? "frontslide" : ahead < -kFacing ? "backslide" : "sitslide";
    }

    std::optional<park::PlacedPerson> pose_person(const ParkScene& scene, std::size_t i, float clock, bool simulate)
    {
        const ShownPerson& shown = scene.shown_people[i];
        const bool simulated = simulate && shown.guest != entt::null;
        park::PlacedPerson pose = dressed(shown);
        pose.time = shown.walking ? shown.saved_time : shown.saved_time + clock * shown.speed;
        if (simulated)
        {
            const auto& r = scene.sim.registry;
            const simulation::Pose at = simulation::between_steps(r.get<simulation::Pose>(shown.guest), scene.sim_time / simulation::kTickSeconds);
            if (r.all_of<simulation::Left>(shown.guest))
                return std::nullopt; // Gone home
            // A keeper walks about their enclosure off the paths.
            if (r.all_of<simulation::KeeperDuty>(shown.guest))
            {
                const auto job = r.get<simulation::KeeperDuty>(shown.guest).job;
                const simulation::Pose& now = r.get<simulation::Pose>(shown.guest);
                const bool walking = now.was != now.position;
                pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                pose.motion = park::motion_number(park::keeper_motion(job, walking)).value_or(walking ? park::motion::kWalk : park::motion::kIdle);
                pose.time = walking && shown.paces[0] > 0 ? at.walked.value / shown.paces[0] : shown.saved_time + clock * shown.speed;
                return pose;
            }
            // A lifeguard sits in their chair, looking about now and then.
            if (const auto* staff = r.try_get<simulation::Staff>(shown.guest); staff != nullptr && staff->role == simulation::StaffRole::Other && !r.all_of<simulation::Stride>(shown.guest))
            {
                constexpr float kLookSeconds = 6;     ///< Chosen by eye: how long a lifeguard keeps to one idle motion
                constexpr std::size_t kLookEvery = 3; ///< Chosen by eye: one idle motion in so many looks about them
                const float turn = scene.park_clock / kLookSeconds + static_cast<float>(entt::to_integral(shown.guest));
                const auto whole = static_cast<std::size_t>(turn);
                pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                pose.motion = park::motion_number(whole % kLookEvery == 0 ? "lifeguardidle01" : "lifeguardidlestandard").value_or(park::motion::kIdle);
                pose.time = (turn - static_cast<float>(whole)) * kLookSeconds;
                return pose;
            }
            if (const auto* staff = r.try_get<simulation::Staff>(shown.guest); staff != nullptr && staff->role == simulation::StaffRole::Vendor)
            {
                std::tie(pose.motion, pose.time) = vendor_motion(scene, shown, glm::vec3(pose.world[3]));
                return pose;
            }
            // An entertainer in a show, standing where they began it: each move as their costume names its motion.
            if (const auto* show = r.try_get<simulation::Show>(shown.guest); show != nullptr && !show->moves.empty() && !r.any_of<simulation::Held, simulation::Dropped>(shown.guest))
            {
                // In ShowMove's order.
                constexpr std::array<std::string_view, 9> kShowMotions { "wave", "idle1", "idle2", "dancelevel1", "dancelevel2", "dancelevel3", "falloverin", "falloverout", "dejection" };
                const auto [move, into] = simulation::show_move_at(*show);
                pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                pose.motion = park::motion_number(kShowMotions[std::to_underlying(move)]).value_or(park::motion::kIdle);
                pose.time = into + scene.sim_time;
                return pose;
            }
            pose.time = shown.saved_time + clock * shown.speed;
            // A gesture they make, as their mesh names its motion.
            const auto gestured = [&](const simulation::Gesture* gesture) {
                if (gesture == nullptr)
                    return;
                // By the names the people's meshes give these motions, in GestureKind's order.
                constexpr std::array<std::string_view, 13> kGestureMotions { "jumpjoy", "laugh", "nodhead", "shakehead", "dizzy", "vomit", "knockdownloop", "handticket", "applause", "lookwatch", "disapproval", "cry", "horror" };
                pose.motion = park::motion_number(kGestureMotions[std::to_underlying(gesture->kind)]).value_or(park::motion::kIdle);
                pose.time = simulation::kTuning.gestures.shown.value - gesture->left.value + scene.sim_time;
                // Rubbish into a bin: a quick step over to it, facing it, and back.
                if (const auto bin = gesture->kind == simulation::GestureKind::HandOver ? item_at(scene, gesture->at) : std::nullopt)
                {
                    constexpr float kBinReach = 0.45f; ///< Chosen by eye: from a bin to where one using it stands
                    constexpr float kBinStep = 0.3f;   ///< Chosen by eye: seconds of the step over to a bin and back
                    const glm::vec2 to { bin->x - at.position.x, bin->z - at.position.z };
                    const float part = std::clamp(std::min(pose.time, gesture->left.value - scene.sim_time) / kBinStep, 0.0f, 1.0f);
                    const float reach = std::max(glm::length(to) - kBinReach, 0.0f) * part;
                    const glm::vec2 way = glm::length(to) > 0 ? glm::normalize(to) : glm::vec2 { 0.0f };
                    const float turn = glm::length(to) > 0 ? std::remainder(std::atan2(to.x, to.y) - at.heading.value, glm::two_pi<float>()) : 0.0f;
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position + glm::vec3(way.x, 0.0f, way.y) * reach), at.heading.value + turn * part, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                // Clapping: hands up, clapping a while, hands down for the last of it.
                constexpr float kClapEnds = 0.33f; ///< Seconds the applause motions take to raise and lower the hands
                if (gesture->kind == simulation::GestureKind::Clap)
                {
                    const float into = simulation::kTuning.shows.clap.value - gesture->left.value + scene.sim_time;
                    const float left = gesture->left.value - scene.sim_time;
                    pose.motion = park::motion_number(into < kClapEnds ? "applausein" : left < kClapEnds ? "applauseout" : "applause").value_or(park::motion::kIdle);
                    pose.time = into < kClapEnds ? into : left < kClapEnds ? kClapEnds - left : into - kClapEnds;
                }
                // Knocked over, they go down, lie there, and get up for the last of it.
                constexpr float kGettingUp = 1.5f; ///< Seconds of it a knocked over guest takes getting up
                if (gesture->kind == simulation::GestureKind::Down)
                {
                    const float down = simulation::kTuning.crashes.knocked.value - gesture->left.value + scene.sim_time;
                    const bool up = gesture->left.value - scene.sim_time < kGettingUp;
                    pose.motion = park::motion_number(down < kGettingUp ? "knockdownin" : up ? "knockdownout" : "knockdownloop").value_or(park::motion::kIdle);
                    pose.time = down < kGettingUp ? down : up ? kGettingUp - gesture->left.value + scene.sim_time : down - kGettingUp;
                }
            };
            // Held by the player they hang where the pointer has them; let go, they fall, then walk to the paths.
            // Thrown by a crash, they fly, and lie where they land till they get up.
            if (const auto* held = r.try_get<simulation::Held>(shown.guest); held != nullptr || r.all_of<simulation::Dropped>(shown.guest))
            {
                const simulation::Pose& now = r.get<simulation::Pose>(shown.guest);
                if (const auto* drop = held == nullptr ? &r.get<simulation::Dropped>(shown.guest) : nullptr; drop != nullptr && drop->sliding)
                {
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = park::motion_number(slide_motion(at.heading.value, drop->drift, shown.staff)).value_or(park::motion::kIdle);
                    return pose;
                }
                const bool falling = held == nullptr && now.position.y > r.get<simulation::Dropped>(shown.guest).ground;
                pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), held != nullptr ? now.position : at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                pose.motion = held != nullptr ? (held->happy ? kDangleHappyMotion : kDangleCrossMotion) : falling ? kDangleCrossMotion : park::motion::kWalk;
                // Falling into a pool they tip over as they go, to land in the water.
                // Off the end of a pool slide, they splash down as they rode it, on their back or in their ring.
                if (const auto* drop = held == nullptr ? &r.get<simulation::Dropped>(shown.guest) : nullptr; falling && drop->pool != entt::null)
                {
                    const auto cars = drop->slide != entt::null ? scene.ride_cars.find(drop->slide) : scene.ride_cars.end();
                    const auto model = cars != scene.ride_cars.end() && !cars->second.empty() ? scene.models.find(park::model_ref(scene.cars[cars->second.front()])) : scene.models.end();
                    const bool body = (model != scene.models.end() && model->second.seating == park::kSlideBodySeating) || (r.valid(drop->slide) && r.all_of<simulation::ClimbSlide>(drop->slide));
                    pose.motion = park::motion_number(drop->slide == entt::null ? "knockdownin" : body ? "slidebodybacksplashdownin" : "slideringsplashdownin").value_or(pose.motion);
                    pose.time = drop->fall.value / simulation::kTuning.holding.gravity;
                }
                if (held == nullptr && !falling && shown.paces[0] > 0)
                    pose.time = at.walked.value / shown.paces[0];
                if (held == nullptr && !falling)
                    gestured(r.try_get<simulation::Gesture>(shown.guest));
                return pose;
            }
            // At a pool: out of sight while changing, then swimming, or walking round the water.
            if (const auto* swimmer = r.try_get<simulation::Swimmer>(shown.guest))
            {
                // Out of sight changing; seated on a pool slide, they go with their car (pose_riders). Floating round a
                // lazy river, they tread water in their ring, or swim paddling it along.
                const auto* floating = r.try_get<simulation::Floating>(shown.guest);
                // In the water they ride the pool's waves up and down, as its surface shows them.
                const auto bobbed = [&] {
                    const glm::vec2 at_xz { at.position.x, at.position.z };
                    const int col = static_cast<int>(std::floor((at_xz.x - scene.grid.origin_x) / scene.grid.tile_size_x));
                    const int row = static_cast<int>(std::floor((at_xz.y - scene.grid.origin_z) / scene.grid.tile_size_z));
                    const auto square = scene.pools.at(col, row);
                    if (!square || square->kind != park::PoolKind::Water || scene.grid.tile_size_x <= 0 || scene.grid.tile_size_z <= 0)
                        return at.position;
                    const auto water = [&](int dc, int dr) {
                        const auto next = scene.pools.at(col + dc, row + dr);
                        return next && next->kind == park::PoolKind::Water && next->surface == square->surface;
                    };
                    const float room = park::pool_wall_room(scene.grid, at_xz - glm::vec2 { scene.grid.world_x(col), scene.grid.world_z(row) }, water);
                    return at.position + glm::vec3(0.0f, park::pool_wave_lift(park::pool_wave_height(scene.water.pool_waves, at_xz, clock, at.position.y), room), 0.0f);
                };
                // Up a slide climbed on foot they walk, and slide down it on their backs.
                if (const auto* climbing = r.try_get<simulation::Climbing>(shown.guest))
                {
                    const auto* slide = r.valid(climbing->slide) ? r.try_get<simulation::ClimbSlide>(climbing->slide) : nullptr;
                    const bool down = slide != nullptr && climbing->way < slide->ways.size() && climbing->along >= slide->ways[climbing->way].top;
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = down ? park::motion_number("slidebodybackloop").value_or(park::motion::kWalk) : park::motion::kWalk;
                    pose.time = scene.sim_time;
                    return pose;
                }
                if (const auto* errand = r.try_get<simulation::Errand>(shown.guest); swimmer->changing.value > 0 || (errand != nullptr && errand->kind == simulation::ErrandKind::Riding && floating == nullptr))
                    return std::nullopt;
                if (floating != nullptr)
                {
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), bobbed()), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                    const bool on_float = scene.person_models && own_float(*scene.person_models, pose);
                    pose.motion = floating->paddling ? (on_float ? kFloatPaddleMotion : kSwimMotion) : on_float ? kFloatSitMotion : kTreadMotion;
                    return pose;
                }
                const simulation::Pose& now = r.get<simulation::Pose>(shown.guest);
                const bool moving = now.was != now.position;
                const bool lounging = swimmer->place >= 0 && swimmer->use == simulation::PoolUse::Lounge;
                pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), swimmer->in_water && !swimmer->to_spa ? bobbed() : at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                pose.motion = swimmer->in_water ? (moving ? kSwimMotion : kTreadMotion) : moving ? park::motion::kWalk : park::motion::kIdle;
                if (!swimmer->in_water && moving && shown.paces[0] > 0)
                    pose.time = at.walked.value / shown.paces[0];
                const auto length = [&](std::int32_t motion) {
                    park::PlacedPerson probe = pose;
                    probe.motion = motion;
                    return park::motion_seconds(*scene.person_models, probe);
                };
                // Off a diving board: up its ladder, out along it, off its end and through the air, up again after.
                if (swimmer->dive != simulation::DiveStep::None && swimmer->dive != simulation::DiveStep::Walk)
                {
                    const auto* errand = r.try_get<simulation::Errand>(shown.guest);
                    const auto* door = errand != nullptr && r.valid(errand->target) ? r.try_get<simulation::PoolDoor>(errand->target) : nullptr;
                    const auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<simulation::SwimPool>(door->pool) : nullptr;
                    const bool high = pool != nullptr && swimmer->board >= 0 && static_cast<std::size_t>(swimmer->board) < pool->boards.size() && pool->boards[static_cast<std::size_t>(swimmer->board)].high;
                    const float into = swimmer->diving.value + scene.sim_time;
                    using enum simulation::DiveStep;
                    switch (swimmer->dive)
                    {
                        case Climb:
                            // The ladder climb is made facing a quarter turn to the right of the way they walk: turned
                            // back, they face the rungs.
                            pose.motion = kClimbLadderMotion;
                            pose.world = glm::rotate(pose.world, glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
                            break;
                        case Jump:
                            pose.motion = high ? kHighDiveInMotion : kDiveMotion;
                            break;
                        case Fall:
                            pose.motion = high ? kHighDiveMotion : kDiveMotion;
                            break;
                        default:
                            pose.motion = high ? kHighDiveOutMotion : kTreadInMotion;
                            break;
                    }
                    // A small board's one dive motion runs on from springing off into the fall.
                    pose.time = swimmer->dive == Fall && !high ? simulation::kTuning.dives.jump.value + into : into;
                    if (swimmer->dive != Climb && swimmer->dive != Fall)
                        pose.time = std::min(pose.time, length(pose.motion));
                    return pose;
                }
                // Into the water off its edge in a dive, or down a ladder (its climb out played backwards); out, climbing
                // over its wall or up a ladder. The motion carries them over from where they stand.
                if (swimmer->edge != simulation::PoolEdge::None)
                {
                    using enum simulation::PoolEdge;
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), swimmer->edge_from), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = swimmer->edge == DiveIn ? kDiveMotion : swimmer->edge == ClimbOut ? kClimbOutMotion : kOutByLadderMotion;
                    const float into = swimmer->edging.value + scene.sim_time;
                    pose.time = std::clamp(swimmer->edge == LadderIn ? length(pose.motion) - into : into, 0.0f, length(pose.motion));
                    return pose;
                }
                // Lying down on a lounger, or sitting down in a spa pool as on a bench: down, staying a while, and up
                // for the last of it.
                const bool spa = swimmer->to_spa && swimmer->in_water && !moving;
                if (lounging || spa)
                {
                    const float stay = lounging ? simulation::kTuning.pools.lounging.value : swimmer->stayed.value + swimmer->idle.value;
                    const float into = (lounging ? stay - swimmer->idle.value : swimmer->stayed.value) + scene.sim_time;
                    const std::int32_t down = lounging ? kLieDownMotion : kSitDownMotion;
                    const std::int32_t up = lounging ? kGetUpMotion : kStandUpMotion;
                    const std::array clips { park::Clip { down, length(down) }, park::Clip { lounging ? kLoungeMotion : kSitMotion, std::max(stay - length(down) - length(up), 0.0f) }, park::Clip { up, length(up) } };
                    const park::Clip step = park::clip_at(clips, std::min(into, stay));
                    pose.motion = step.motion;
                    pose.time = step.seconds;
                }
                return pose;
            }
            // At a view: the party in a row on the path behind it, facing out the way it looks, and one of them at the
            // row's end turned to film the rest. Teens slouch now and then; the rest stand.
            if (const auto* linger = r.try_get<simulation::Linger>(shown.guest); linger != nullptr && linger->at == simulation::PathItemKind::PhotoSpot)
            {
                const auto* item = r.valid(linger->bench) ? r.try_get<simulation::PathItem>(linger->bench) : nullptr;
                const auto o = item != nullptr ? std::ranges::find(scene.objects, item->object, &save::PlacedObject::id) : scene.objects.end();
                if (o != scene.objects.end() && !o->hidden)
                {
                    constexpr float kRowGap = 0.6f;  ///< Chosen by eye: between two in the row
                    constexpr float kRowBack = 0.8f; ///< Chosen by eye: from the view to the row
                    constexpr float kFilming = 1.2f; ///< Chosen by eye: from the row's end to the one filming it
                    const auto model = scene.models.find(park::model_ref(*o));
                    const glm::mat4 placed = park::placed_transform(*o, model != scene.models.end() ? &model->second : nullptr, scene.grid, scene.fitted);
                    // It looks out from the path, from the middle of the path's tile across the item.
                    const glm::vec3 outward = glm::vec3(placed[3]) - scene.sim.paths.centre(item->node);
                    const glm::vec3 ahead = glm::length(glm::vec2(outward.x, outward.z)) > 0.01f ? glm::normalize(glm::vec3(outward.x, 0.0f, outward.z)) : glm::normalize(glm::vec3(placed[2].x, 0.0f, placed[2].z));
                    const glm::vec3 side { ahead.z, 0.0f, -ahead.x };
                    std::size_t row = 0;    // How many stand in the row, of every party there, those filming not counted
                    std::size_t before = 0; // Of them, how many stand before this one
                    for (const auto& [them, l] : r.view<const simulation::Linger>().each())
                        if (l.bench == linger->bench && !l.camera)
                        {
                            ++row;
                            before += them < shown.guest ? 1 : 0;
                        }
                    const glm::vec3 middle = glm::vec3(placed[3]) - ahead * kRowBack;
                    const float place = static_cast<float>(before) - static_cast<float>(std::max<std::size_t>(row, 1) - 1) / 2;
                    const float reach = static_cast<float>(std::max<std::size_t>(row, 1) - 1) / 2 * kRowGap; // From the row's middle to its end
                    const glm::vec3 stand = linger->camera ? middle + side * (reach + kFilming) : middle + side * place * kRowGap;
                    const float facing = linger->camera ? std::atan2(-side.x, -side.z) : std::atan2(ahead.x, ahead.z);
                    const float watch = simulation::kTuning.facilities.watch.value;
                    const float into = watch - linger->left.value + scene.sim_time;
                    const float off = std::min(into, watch - into);
                    const float part = std::clamp(off / kStepToSeat, 0.0f, 1.0f);
                    const float turn = std::remainder(facing - at.heading.value, glm::two_pi<float>());
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), glm::mix(at.position, glm::vec3(stand.x, at.position.y, stand.z), part)), at.heading.value + turn * part, glm::vec3(0.0f, 1.0f, 0.0f));
                    if (off < kStepToSeat)
                    {
                        pose.motion = park::motion::kWalk;
                        if (shown.paces[0] > 0)
                            pose.time = off * glm::distance(at.position, stand) / kStepToSeat / shown.paces[0];
                        return pose;
                    }
                    const auto length = [&](std::int32_t motion) {
                        park::PlacedPerson probe = pose;
                        probe.motion = motion;
                        return park::motion_seconds(*scene.person_models, probe);
                    };
                    const auto* visitor = r.try_get<simulation::Visitor>(shown.guest);
                    const bool slouch = !linger->camera && visitor != nullptr && visitor->age == simulation::AgeGroup::Teen && entt::to_integral(shown.guest) % 2 == 0;
                    const std::int32_t in = linger->camera ? kVideoInMotion : slouch ? kSlouchInMotion : park::motion::kIdle;
                    const std::int32_t out = linger->camera ? kVideoOutMotion : slouch ? kSlouchOutMotion : park::motion::kIdle;
                    const float stay = watch - 2 * kStepToSeat;
                    const float begin = in == park::motion::kIdle ? 0.0f : length(in);
                    const float end = out == park::motion::kIdle ? 0.0f : length(out);
                    const std::array clips { park::Clip { in, begin }, park::Clip { linger->camera ? kVideoMotion : slouch ? kSlouchMotion : park::motion::kIdle, std::max(stay - begin - end, 0.0f) }, park::Clip { out, end } };
                    const park::Clip step = park::clip_at(clips, std::min(into - kStepToSeat, stay));
                    pose.motion = step.motion;
                    pose.time = step.seconds;
                    if (!linger->camera)
                        gestured(r.try_get<simulation::Gesture>(shown.guest));
                    return pose;
                }
            }
            // On a bench: a step over to their seat, down onto it, sitting there, turned now and then to talk to one
            // sitting beside them, and up and back to the path.
            if (const auto seat = bench_seat(scene, shown.guest))
            {
                const auto& linger = r.get<simulation::Linger>(shown.guest);
                // Of a step between the path and the seat, `part` of the way to the seat.
                const auto stepping = [&](float part) {
                    const glm::vec3 to { (*seat)[3] };
                    const float facing = std::atan2((*seat)[2][0], (*seat)[2][2]);
                    const float turn = std::remainder(facing - at.heading.value, glm::two_pi<float>());
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), glm::mix(at.position, to, part)), at.heading.value + turn * part, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = park::motion::kWalk;
                    if (shown.paces[0] > 0)
                        pose.time = part * glm::distance(at.position, to) / shown.paces[0];
                };
                const auto length = [&](std::int32_t motion) {
                    park::PlacedPerson probe = pose;
                    probe.motion = motion;
                    return park::motion_seconds(*scene.person_models, probe);
                };
                pose.world = *seat;
                // At a water cannon: a step over behind it, taking hold, firing a while, letting go and a step back.
                // shortcut: they stand as the cannon rests; upgrade with its swing once the cannon aims.
                if (linger.at == simulation::PathItemKind::WaterCannon)
                {
                    const float firing = simulation::kTuning.surroundings.firing.value;
                    const float into = firing - linger.left.value + scene.sim_time;
                    if (const float off = std::min(into, firing - into); off < kStepToSeat)
                    {
                        stepping(std::max(off, 0.0f) / kStepToSeat);
                        return pose;
                    }
                    const float hold = length(kCannonInMotion);
                    const float release = length(kCannonOutMotion);
                    const std::array clips { park::Clip { kCannonInMotion, hold }, park::Clip { kCannonMotion, std::max(firing - 2 * kStepToSeat - hold - release, 0.0f) }, park::Clip { kCannonOutMotion, release } };
                    const park::Clip step = park::clip_at(clips, into - kStepToSeat);
                    pose.motion = step.motion;
                    pose.time = step.seconds;
                    return pose;
                }
                if (linger.rising)
                {
                    const float up = simulation::kTuning.facilities.rise.value - linger.left.value + scene.sim_time;
                    const float standing = length(kStandUpMotion);
                    pose.motion = kStandUpMotion;
                    pose.time = up;
                    if (up > standing)
                        stepping(1 - std::min((up - standing) / kStepToSeat, 1.0f));
                    return pose;
                }
                const float since = linger.sat.value + scene.sim_time;
                if (since < kStepToSeat)
                {
                    stepping(since / kStepToSeat);
                    return pose;
                }
                const float down = since - kStepToSeat;
                const float sitting = length(kSitDownMotion);
                pose.motion = down < sitting ? kSitDownMotion : kSitMotion;
                pose.time = down < sitting ? down : down - sitting;
                // One on the seat to either side, sat down and not getting up: every other turn they talk.
                const auto beside = [&](int side) {
                    return std::ranges::any_of(r.view<const simulation::Linger>().each(), [&](const auto& other) {
                        const auto& [them, l] = other;
                        return them != shown.guest && l.bench == linger.bench && l.seat == linger.seat + side && !l.rising && l.sat.value > kStepToSeat;
                    });
                };
                const auto turn = static_cast<std::size_t>(scene.park_clock / kChatTurnSeconds) + entt::to_integral(shown.guest);
                if (down >= sitting && turn % 2 == 0)
                    if (const bool before = beside(-1), after = beside(1); before || after)
                        pose.motion = kSitChatMotions[after && (!before || turn / 2 % 2 == 0) ? 1 : 0];
                // What they bought in their left hand, a bite or a sip of it now and then.
                if (const auto* carried = r.try_get<simulation::Carried>(shown.guest))
                {
                    pose.held = held_model(scene, *carried);
                    if (pose.held != nullptr && down >= sitting && carried->bites > 0 && carried->since.value < kBiteSeconds)
                    {
                        pose.motion = carried->food ? kSitEatMotion : kSitDrinkMotion;
                        pose.time = carried->since.value;
                    }
                }
                return pose;
            }
            const auto& stride = r.get<simulation::Stride>(shown.guest);
            const auto kind = r.get<simulation::Errand>(shown.guest).kind;
            const auto* ride_walk = r.try_get<simulation::RideWalk>(shown.guest);
            if (kind == simulation::ErrandKind::Riding && ride_walk == nullptr)
                return std::nullopt; // Seated riders go with their cars (pose_riders); the rest wait out of sight
            const simulation::Pose& now = r.get<simulation::Pose>(shown.guest);
            const bool at_ticket = ride_walk != nullptr && !ride_walk->off && ride_walk->next == 1 && ride_walk->pause.value > 0;
            const bool watching = r.all_of<simulation::Watching>(shown.guest); // Mid-step, stopped to watch
            const bool walking = !at_ticket && (ride_walk != nullptr || (stride.from != stride.to && !watching) || now.was != now.position);
            // Out on the paths they go in their gait, running or dragging their feet; on the way on to a ride
            // or in its queue they walk.
            const bool own_gait = kind == simulation::ErrandKind::Wandering || kind == simulation::ErrandKind::Heading || kind == simulation::ErrandKind::Waiting || kind == simulation::ErrandKind::Following;
            const auto gait = std::to_underlying(own_gait && ride_walk == nullptr ? stride.gait : simulation::Gait::Walk);
            const std::size_t way = shown.paces[gait] > 0 ? gait : 0;
            // The walk goes as far into its motion as the ground covered allows, so the feet stay put
            // however fast the guest goes.
            if (walking && shown.paces[way] > 0)
                pose.time = at.walked.value / shown.paces[way];
            // People are modelled facing +z, which the heading turns towards the way they go.
            pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
            pose.motion = walking ? kGaitMotions[way] : chat_motion(scene, shown);
            if (at_ticket)
            {
                pose.motion = kTicketMotion;
                pose.time = simulation::kTuning.queues.ticket.value - ride_walk->pause.value;
            }
            // At a stall's counter: waiting while served, then paying. At a cash machine counting the money out. A
            // toilet or first aid they step into, out of sight while there, and step out of for the last of it.
            if (kind == simulation::ErrandKind::Buying && !walking)
            {
                const auto& errand = r.get<simulation::Errand>(shown.guest);
                const auto* shop = r.valid(errand.target) ? r.try_get<simulation::Shop>(errand.target) : nullptr;
                const auto product = shop != nullptr ? shop->product : simulation::Product::Drink;
                const float left = static_cast<float>(errand.rest) * simulation::kTickSeconds;
                const float into = errand.turn.value - left;
                if (product == simulation::Product::Toilet || product == simulation::Product::FirstAid)
                {
                    constexpr float kIndoors = 0.8f; ///< Chosen by eye: from the counter in through the door
                    const float off = std::min(into, left);
                    if (off >= kStepToSeat)
                        return std::nullopt;
                    const float part = std::max(off, 0.0f) / kStepToSeat;
                    const glm::vec3 ahead { std::sin(at.heading.value), 0.0f, std::cos(at.heading.value) };
                    const float facing = at.heading.value + (left < into ? glm::pi<float>() : 0.0f);
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position + ahead * kIndoors * part), facing, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = park::motion::kWalk;
                    pose.time = shown.paces[0] > 0 ? kIndoors * part / shown.paces[0] : 0.0f;
                }
                else if (product == simulation::Product::CashMachine)
                {
                    pose.motion = kCountCashMotion;
                    pose.time = into;
                }
                else if (const float paying = served_for(errand) - simulation::kTuning.facilities.served.value / 2; paying >= 0)
                {
                    pose.motion = kTicketMotion;
                    pose.time = paying;
                }
            }
            // At work where they stand: the job's motions in turn, any tool in hand.
            const auto* staff = r.try_get<simulation::Staff>(shown.guest);
            if (staff != nullptr && staff->job != simulation::StaffJob::None && staff->busy.value > 0 && !walking)
            {
                const auto* upkeep = r.valid(staff->ride) ? r.try_get<simulation::Upkeep>(staff->ride) : nullptr;
                const park::WorkLook look = park::work_look(staff->job, upkeep != nullptr ? upkeep->kind : simulation::Breakdown::None);
                // Played once, the start and the end as long as they last; the rest of the job repeats its loop.
                const auto once = [&](std::string_view name) -> std::optional<park::Clip> {
                    const auto number = name.empty() ? std::nullopt : park::motion_number(name);
                    if (!number)
                        return std::nullopt;
                    park::PlacedPerson probe = pose;
                    probe.motion = *number;
                    return park::Clip { *number, park::motion_seconds(*scene.person_models, probe) };
                };
                const auto start = once(look.start);
                const auto finish = once(look.finish);
                const float length = staff->job_length.value;
                std::vector<park::Clip> clips;
                if (start)
                    clips.push_back(*start);
                clips.push_back({ park::motion_number(look.loop).value_or(park::motion::kIdle), std::max(length - (start ? start->seconds : 0.0f) - (finish ? finish->seconds : 0.0f), 0.0f) });
                if (finish)
                    clips.push_back(*finish);
                const park::Clip step = park::clip_at(clips, std::min(length - staff->busy.value + scene.sim_time, length));
                pose.motion = step.motion;
                pose.time = step.seconds;
                const auto tool = scene.staff_tools.find(look.tool);
                pose.tool = tool != scene.staff_tools.end() ? &tool->second : nullptr;
            }
            // Guards have no plain standing motion; they tap a foot instead.
            else if (staff != nullptr && staff->role == simulation::StaffRole::Guard && pose.motion == park::motion::kIdle)
                pose.motion = park::motion_number("tapfoot").value_or(park::motion::kIdle);
            const bool idle = pose.motion == park::motion::kIdle;
            gestured(r.try_get<simulation::Gesture>(shown.guest));
            // Food or drink from a stall in their left hand, or its empty once it is gone.
            if (const auto* carried = r.try_get<simulation::Carried>(shown.guest))
            {
                pose.held = held_model(scene, *carried);
                const bool biting = carried->bites > 0 && carried->since.value < kBiteSeconds;
                if (pose.held != nullptr && walking && way == 0 && !r.all_of<simulation::Gesture>(shown.guest))
                {
                    pose.motion = biting ? (carried->food ? kEatMotion : kDrinkMotion) : kCarryMotion;
                    if (biting)
                        pose.time = carried->since.value;
                }
                else if (pose.held != nullptr && idle && !r.all_of<simulation::Gesture>(shown.guest))
                    pose.motion = kHoldMotion;
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
                else if (shown.paces[0] > 0)
                    pose.time = at.walked.value / shown.paces[0]; // Their walk's step, whatever their gait
            }
            // Glancing at something as they go: the head turns to it and back, as far as a neck goes.
            if (const auto* glance = r.try_get<simulation::Glance>(shown.guest))
                if (const auto seen = item_at(scene, glance->at))
                {
                    constexpr float kNeckTurn = glm::radians(70.0f); ///< Chosen by eye: the furthest a head turns
                    const float length = simulation::kTuning.surroundings.glancing.value;
                    const float into = std::clamp(length - glance->left.value + scene.sim_time, 0.0f, length);
                    const float turn = std::remainder(std::atan2(seen->x - at.position.x, seen->z - at.position.z) - at.heading.value, glm::two_pi<float>());
                    pose.head_turn = std::clamp(turn, -kNeckTurn, kNeckTurn) * std::sin(glm::pi<float>() * into / length);
                }
        }
        return pose;
    }

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
    std::optional<glm::mat4> ride_eye(const ParkScene& scene, std::size_t car, park::RideEye eye)
    {
        const auto model = car < scene.cars.size() ? scene.models.find(park::model_ref(scene.cars[car])) : scene.models.end();
        if (model == scene.models.end() || car >= scene.car_spots.size())
            return std::nullopt;
        const park::CarSpot spot = scene.car_spots[car];
        if (eye == park::RideEye::Nose && std::ranges::any_of(std::span(scene.car_spots).first(car), [&](const park::CarSpot& s) { return s.ride == spot.ride && s.train == spot.train; }))
            return std::nullopt;
        return model->second.ride_eyes[std::to_underlying(eye)];
    }

    std::optional<glm::mat4> flat_view(const ParkScene& scene, std::size_t ride, bool fixed, std::size_t at)
    {
        const auto object = scene.ride_seats.find(ride < scene.ride_entities.size() ? scene.ride_entities[ride] : entt::null);
        if (object == scene.ride_seats.end())
            return std::nullopt;
        const save::PlacedObject& o = scene.objects[object->second];
        const auto model = scene.models.find(park::model_ref(o));
        const std::vector<glm::mat4>* places = model == scene.models.end() ? nullptr : fixed ? &model->second.view_places : &model->second.seat_places;
        if (places == nullptr || at >= places->size())
            return std::nullopt;
        const auto boned = std::ranges::find(scene.boned_rides, object->second, &ParkScene::BonedRide::object);
        if (boned != scene.boned_rides.end() && fixed && at < boned->views.size())
            return boned->placed * boned->views[at] * (*places)[at];
        if (boned != scene.boned_rides.end() && !fixed && at < boned->seats)
            return scene.seat_carriers[boned->first_seat + at] * (*places)[at];
        return park::placed_transform(o, &model->second, scene.grid, scene.fitted) * (*places)[at];
    }

    void place_cars(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, float elapsed)
    {
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
            const park::CarSpot spot = scene.car_spots[i];
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
                const glm::mat4 lean = glm::rotate(glm::mat4(1.0f), boat.lean, glm::vec3(boat.heading.x, 0, boat.heading.y));
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
                const float flown_for = train.flight ? train.flight->off - (1 - along) * simulation::kTickSeconds : 0.0f;
                const float at = train.flight ? std::max(train.was_at, train.at + train.flight->speed * std::min(flown_for, 0.0f)) : simulation::between_drives(train, along);
                moved[i] = at - scene.rides.rides[spot.ride].trains[spot.train].distance;
                place = park::moved_along(*scene.courses[spot.ride], place, { spot.front, spot.rear }, moved[i]);
                // A slide's car swung up its trough's wall, turned about its shape's pivot.
                if (train.trough && train.lean != 0)
                {
                    const glm::vec3 way = glm::normalize(glm::vec3(place[2]));
                    const glm::vec3 pivot = glm::vec3(place[3]) + glm::normalize(glm::vec3(place[1])) * train.trough->pivot;
                    place = glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), -train.lean, way) * glm::translate(glm::mat4(1.0f), -pivot) * place;
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
                        flight = simulation::Flight { .from = f->from, .way = f->way, .at = f->from, .velocity = f->way * f->speed, .water = f->water, .speed = f->speed, .reach = f->reach };
                        simulation::fly(*flight, std::max(flown_for, 0.0f));
                    }
                    // The car comes down with a splash too, ridden or not.
                    if (flight && flight->afloat >= 0 && !std::exchange(scene.car_waters[i].landed, true))
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
                    const std::size_t car = static_cast<std::size_t>(std::ranges::count_if(std::span(scene.car_spots).first(i), [&](const park::CarSpot& s) { return s.ride == spot.ride && s.train == spot.train; }));
                    const auto* wreck = wrecks != nullptr && spot.train < wrecks->trains.size() && car < wrecks->trains[spot.train].size() ? &wrecks->trains[spot.train][car] : nullptr;
                    if (wreck == nullptr || wreck->state == simulation::WreckState::Gone)
                        place = glm::mat4(0.0f);
                    else
                    {
                        const glm::vec3 left(place[3]);
                        place = glm::translate(glm::mat4(1.0f), left + wreck->at - wreck->from) * glm::mat4_cast(wreck->turned) * glm::translate(glm::mat4(1.0f), -left) * place;
                    }
                }
                else if (const auto model = scene.models.find(park::model_ref(scene.cars[i])); model != scene.models.end())
                {
                    // Through the water: sprays from its splash points as it splashes, a wake behind it now and then.
                    const glm::vec3 velocity = elapsed > 0 ? (glm::vec3(place[3]) - glm::vec3(scene.car_places[i][3])) / elapsed : glm::vec3(0.0f);
                    // Only cars that spray, and the one ridden for the water on the lens, look for the park's water.
                    const bool looks = !model->second.splash_points.empty() || scene.riding == i;
                    const park::TrackCourse& course = *scene.courses[spot.ride];
                    // shortcut: a splash ride whose track says nothing of water (a sea serpent diving into its
                    // lake) is taken as wet all along, splashing where its fall stops short; upgrade once such a
                    // track's water is found in its files.
                    const auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(ride);
                    const bool dry_splasher = cycle != nullptr && cycle->splash && std::ranges::none_of(course.wet, std::identity { });
                    const bool in_water = dry_splasher || (looks && park::water_above(scene.water.surface, glm::vec3(place[3])).has_value());
                    const park::CarSplash splash = park::car_splash(scene.car_waters[i], course, spot.front + moved[i], in_water, velocity, elapsed);
                    for (const park::SplashPoint& point : model->second.splash_points)
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
        scene.seat_across = park::kRideWideView;
        if (const auto seat = scene.riding ? ride_eye(scene, *scene.riding, scene.riding_eye) : std::nullopt)
        {
            const std::size_t i = *scene.riding;
            const park::CarSpot spot = scene.car_spots[i];
            ovl::RideCameraTuning tuning = scene.models.at(park::model_ref(scene.cars[i])).ride_look;
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
            if (const auto& course = scene.courses[spot.ride]; course && tuning.look_ahead > 0 && scene.riding_eye != park::RideEye::Rear && !(train && train->speed < 0))
                ahead = glm::vec3(park::moved_along(*course, scene.saved_cars.at(scene.cars[i].id), { spot.front, spot.rear }, moved[i] + tuning.look_ahead) * (*seat)[3]);
            const glm::vec2 dice { static_cast<float>(random::unit(scene.ride_dice)) * 2 - 1, static_cast<float>(random::unit(scene.ride_dice)) * 2 - 1 };
            scene.seat = park::looked_round(park::ride_look(scene.ride_view, scene.car_places[i] * *seat, ahead, scene.riding_eye != park::RideEye::Nose, tuning, elapsed, dice), scene.ride_turn);
        }
        // On a flat ride, from one of its fixed cameras, or from a rider's eyes over their seat, as they move. A
        // seat faces +Z with +Y up, as the people sat on it are modelled.
        constexpr glm::vec3 kSeatEyes { 0, 0.75f, 0.2f }; ///< Where a sitting rider's eyes are, just before their face
        if (const auto seat = scene.riding_flat ? flat_view(scene, scene.riding_flat->first, scene.riding_fixed, scene.riding_flat->second) : std::nullopt)
        {
            scene.seat = park::looked_round(scene.riding_fixed ? *seat : glm::translate(*seat, kSeatEyes), scene.ride_turn);
            scene.seat_across = scene.riding_fixed ? park::kRideWideView : park::kRideNarrowView;
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
            scene.seat_across = park::kRideNarrowView;
            if (scene.person_behind)
                scene.seat = park::looked_round(glm::rotate(glm::translate(place, glm::vec3(0, kBehindHeight, 0)), kBehindPitch, glm::vec3(1, 0, 0)), scene.ride_turn);
            else
                scene.seat = park::looked_round(glm::translate(place, glm::vec3(0, kEyeHeight, 0)), scene.ride_turn);
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
            const auto down = [&](glm::vec3 from) { return park::ground_hit(park::Ray { from, glm::vec3(0, -1, 0) }, scene.grid, kGroundLook); };
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
            if (const auto model = scene.car_waters[i].ridden ? scene.models.find(park::model_ref(scene.cars[i])) : scene.models.end(); model != scene.models.end() && model->second.stand_in)
                scene.car_mesh_places[i] = glm::mat4(0.0f);
        // Cars their bones move are drawn apart. A boat out on the water rows, with the oars its turn calls for; one
        // on a course rows with both while its train moves.
        for (ParkScene::BonedCar& boned : scene.boned_cars)
        {
            scene.car_mesh_places[boned.car] = glm::mat4(0.0f);
            const park::ObjectModel& model = scene.models.at(park::model_ref(scene.cars[boned.car]));
            const park::CarSpot spot = scene.car_spots[boned.car];
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
            const std::vector<glm::mat4> moves = park::bone_moves(*model.rig, boned.stroke ? motion(*boned.stroke) : model.rig->motions.size(), scene.park_clock - boned.since);
            std::vector<glm::mat4> places(moves.size());
            for (std::size_t b = 0; b < places.size(); ++b)
                places[b] = scene.car_places[boned.car] * moves[b];
            park::move_meshes(boned.mesh, places);
        }
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
                // A VIP wears their own clothes.
                const auto* vip = r.try_get<simulation::Vip>(e);
                const std::uint8_t celebrity = vip != nullptr ? vip->who : 0;
                const auto type = vip != nullptr ? save::PersonType::Special : static_cast<save::PersonType>(std::to_underlying(visitor.age)); // The same numbers
                const save::Sex sex = visitor.female && vip == nullptr ? save::Sex::Female : save::Sex::Male;
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
                save::Guest& kept = scene.people.guests.emplace_back();
                kept.in_park = true;
                kept.mood_now.happiness = mood.happiness.value;
                kept.person.type = type;
                kept.person.sub_type = vip != nullptr ? celebrity : std::to_underlying(sex);
                kept.person.name = visitor.name.empty() ? std::format("Guest {}", scene.people.guests.size()) : visitor.name;
                save::Appearance& a = kept.person.appearance;
                a.head = { worn[0]->id };
                a.body = { worn[1]->id };
                a.legs = { worn[2]->id };
                if (worn[3] != nullptr)
                    a.swim_body = { worn[3]->id };
                if (worn[4] != nullptr)
                    a.swim_legs = { worn[4]->id };
                ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .saved = scene.people.guests.size() - 1, .guest = e, .swim_body = worn[3] != nullptr ? worn[3]->set : nullptr, .swim_legs = worn[4] != nullptr ? worn[4]->set : nullptr });
                park::PersonLook& look = shown.pose.look;
                look.head = worn[0]->set;
                look.body = worn[1]->set;
                look.legs = worn[2]->set;
                for (std::size_t i = 0; i < look.colours.size(); ++i)
                    look.colours[i] = a.palettes[i] = static_cast<std::uint8_t>(random::below(scene.looks, kColourCells));
                shown.pose.motion = park::motion::kWalk;
            }
            // Keepers hired with animal houses since, and lifeguards come with lifeguard chairs, in their jobs' clothes.
            for (const auto [e, staff, at] : r.view<const simulation::Staff, const simulation::Pose>(entt::exclude<simulation::Stride>).each())
            {
                const bool keeper = r.all_of<simulation::KeeperDuty>(e);
                if (!scene.simulate || (!keeper && staff.role != simulation::StaffRole::Other) || !scene.known_guests.insert(e).second)
                    continue;
                const auto type = keeper ? save::PersonType::AnimalKeeper : save::PersonType::LifeGuard;
                std::array<const Outfit*, 3> worn { };
                for (const Outfit& o : scene.wardrobe)
                    if (o.type == type && !o.swim && worn[o.section] == nullptr)
                        worn[o.section] = &o;
                if (std::ranges::contains(worn, nullptr))
                    continue;
                save::Staff& kept = scene.people.staff.emplace_back();
                kept.kind = keeper ? save::StaffKind::AnimalKeeper : save::StaffKind::LifeGuard;
                kept.person.type = type;
                kept.person.name = std::format("{} {}", keeper ? "Keeper" : "Lifeguard", scene.people.staff.size());
                kept.task_flags = staff.tasks;
                if (keeper)
                    kept.work = save::KeeperWork { };
                save::Appearance& a = kept.person.appearance;
                a.head = { worn[0]->id };
                a.body = { worn[1]->id };
                a.legs = { worn[2]->id };
                ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .staff = true, .saved = scene.people.staff.size() - 1, .guest = e });
                shown.pose.look.head = worn[0]->set;
                shown.pose.look.body = worn[1]->set;
                shown.pose.look.legs = worn[2]->set;
                shown.pose.motion = keeper ? park::motion::kWalk : park::motion::kIdle;
                shown.pose.world = glm::translate(glm::mat4(1.0f), at.position); // Where they stand while the park is still
            }
            // Sellers, as the game keeps them: one stands at each stall and ride entrance whose model has a place
            // for them, from when it is built, and goes when it is pulled down or moved.
            if (scene.simulate)
            {
                std::unordered_set<std::uint64_t> posts;
                for (const save::Stall& stall : scene.rides.stalls)
                    if (!stall.base.demolished && stall.base.spot && stall.base.spot->object != 0)
                        posts.insert(stall.base.spot->object);
                for (const save::Ride& ride : scene.rides.rides)
                    for (const save::Station& station : ride.stations)
                        if (!ride.base.demolished && station.entrance_spot && station.entrance_spot->object != 0)
                            posts.insert(station.entrance_spot->object);
                // Each seller's post, by their place in shown_people, and the objects of all posts.
                std::vector<std::pair<std::size_t, std::uint64_t>> sellers;
                for (std::size_t i = 0; i < scene.shown_people.size(); ++i)
                {
                    const ShownPerson& p = scene.shown_people[i];
                    if (const auto* work = p.staff && p.saved < scene.people.staff.size() ? std::get_if<save::VendorWork>(&scene.people.staff[p.saved].work) : nullptr; work != nullptr && !work->scenery_item.is_none())
                        sellers.emplace_back(i, work->scenery_item.id);
                }
                std::unordered_set<std::uint64_t> wanted = posts;
                for (const auto& [i, post] : sellers)
                    wanted.insert(post);
                std::unordered_map<std::uint64_t, const save::PlacedObject*> found;
                for (const save::PlacedObject& o : scene.objects)
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
                for (const save::Staff& s : scene.people.staff)
                    if (s.kind == save::StaffKind::Vendor)
                        number = std::max(number, s.type_id);
                for (const std::uint64_t post : posts)
                {
                    const auto object = found.find(post);
                    const auto model = object != found.end() && !object->second->hidden ? scene.models.find(park::model_key(*object->second)) : scene.models.end();
                    if (served.contains(post) || model == scene.models.end() || !model->second.vendor_place)
                        continue;
                    const save::Sex sex = random::below(scene.looks, 2) == 0 ? save::Sex::Male : save::Sex::Female;
                    std::array<const Outfit*, 3> worn { };
                    for (const Outfit& o : scene.wardrobe)
                        if (o.type == save::PersonType::Vendor && o.sex == sex && !o.swim && worn[o.section] == nullptr)
                            worn[o.section] = &o;
                    if (std::ranges::contains(worn, nullptr))
                        continue;
                    const glm::mat4 place = park::placed_transform(*object->second, &model->second, scene.grid, scene.fitted) * *model->second.vendor_place;
                    const entt::entity e = simulation::add_vendor(scene.sim, glm::vec3(place[3]));
                    scene.known_guests.insert(e);
                    save::Staff& kept = scene.people.staff.emplace_back();
                    kept.kind = save::StaffKind::Vendor;
                    kept.type_id = ++number;
                    kept.work = save::VendorWork { .scenery_item = { post } };
                    kept.person.type = save::PersonType::Vendor;
                    kept.person.sub_type = std::to_underlying(sex);
                    kept.person.name = std::format("%ME% {}", number); // As the game names them, after their job
                    kept.person.position = { place[3].x, place[3].y, place[3].z };
                    save::Appearance& a = kept.person.appearance;
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
                    const park::PartSet* legs = shown.pose.look.legs;
                    if (shown.paces[0] > 0 || legs == nullptr)
                        continue;
                    auto [paces, fresh] = scene.walk_paces.try_emplace(legs);
                    if (fresh)
                        for (std::size_t gait = 0; gait < kGaits; ++gait)
                            for (const std::size_t piece : legs->levels[0].fixed)
                                if (const auto measured = park::walk_pace(scene.person_models->piece(piece), kGaitMotions[gait]); measured && paces->second[gait] == 0)
                                    paces->second[gait] = *measured;
                    shown.paces = paces->second;
                }
            }
            park::ObjectLevels levels(scene.shown_people.size(), park::kNotDrawn);
            for (std::size_t i = 0; i < levels.size(); ++i)
            {
                if (const ShownPerson& p = scene.shown_people[i]; scene.simulate && p.guest != entt::null && scene.sim.registry.all_of<simulation::Left>(p.guest))
                    continue; // Gone home
                const float distance = glm::distance(eye, standing_at(scene, scene.shown_people[i]));
                // Scaled as objects' levels are: detail for the finer ones, draw distance for the last.
                const park::LevelScale scale = scene.level_scales[static_cast<std::size_t>(park::DetailKind::People)];
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
                    park::PersonRuns runs;
                    built.mesh = profiling::section("Still", [&] { return park::person_mesh(*scene.person_models, place_people(scene, levels, 0, false, view, simulate), runs); });
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
                if (swimwear != shown.swimwear && !changing && !shown.changing && i < scene.people_levels.size() && scene.people_levels[i] != park::kNotDrawn)
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
                    const bool high = pool != nullptr && swimmer->board >= 0 && static_cast<std::size_t>(swimmer->board) < pool->boards.size() && pool->boards[static_cast<std::size_t>(swimmer->board)].high;
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
            scene.moving_build = std::async(std::launch::async, [&library = *scene.person_models, &runs = scene.moving_runs, placed = place_people(scene, scene.people_levels, scene.park_clock, true, view, scene.simulate)] { return park::person_mesh(library, placed, runs); });
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
    park::FlowFades flow_fades(const ParkScene& scene)
    {
        return park::flow_fades(scene.rides, scene.objects, scene.models, [&](std::size_t ride) { return ride < scene.track_rules.size() && scene.track_rules[ride].pool_ride; });
    }

    std::optional<ObjectBuild> build_objects(const ParkScene& scene, const glm::vec3& eye, const park::LevelPlan& shown, float band, const park::LevelScales& scales, bool supports, const park::Broken& broken, const park::Chains& chains, const park::FlowFades& fades)
    {
        ObjectBuild built;
        built.plan = profiling::section("Levels", [&] { return park::plan_levels(scene.objects, scene.models, scene.anchors, eye, band, scales); });
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
            shapes->mesh = profiling::section("Mesh", [&] { return park::object_mesh(scene.objects, scene.models, scene.grid, *levels, { }, scene.leans, scene.fitted, broken, chains, fades); });
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
    void refresh_objects(render::Device& device, ParkScene& scene, const glm::vec3& eye, float elapsed)
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
            moving.append_range(park::twins(scene.objects, moving));
            // Drawn as the build would: moving ones apart, supports only when shown.
            std::vector<std::int8_t> levels(now[1] - from, 0);
            for (std::size_t i = from; i < now[1]; ++i)
            {
                if (std::ranges::contains(moving, i) || (!scene.show_supports && scene.objects[i].overlay == park::kSupportOverlay))
                    levels[i - from] = park::kNotDrawn;
            }
            scene.fresh_objects = park::object_mesh(std::span(scene.objects).subspan(from), scene.models, scene.grid, levels, { }, scene.leans, scene.fitted, scene.broken_objects);
            add_textures(device, scene, scene.fresh_objects.batches);
            scene.fresh_for = now;
        }
        scene.since_rebuild += elapsed;
        if (scene.since_rebuild < kObjectRebuildInterval || scene.object_build.valid() || scene.supports.stale)
            return; // Stale supports change the objects first (refresh_supports)
        scene.since_rebuild = 0;
        scene.object_build = std::async(std::launch::async, [&scene, eye, shown = scene.levels, band = scene.blend_band, scales = scene.level_scales, supports = scene.show_supports, broken = scene.broken_objects, chains = park::chain_runs(scene.rides, scene.objects, scene.models), fades = flow_fades(scene)] {
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

} // namespace rct3::scene
