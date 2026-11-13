// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/poses.hpp"

#include "park/object/models.hpp"
#include "park/person/motions.hpp"
#include "park/ride/cars.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/park/scene.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/crowds.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/motions.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {
        /// Where path item `item` stands in the park, as its placed object; nothing once it is gone.
        std::optional<glm::vec3> item_at(const ParkScene& scene, entt::entity item)
        {
            const auto& r = scene.sim.registry;
            const auto* path_item = r.valid(item) ? r.try_get<simulation::PathItem>(item) : nullptr;
            const auto o = path_item != nullptr ? std::ranges::find(scene.objects, path_item->object, &save::model::PlacedObject::id) : scene.objects.end();
            if (o == scene.objects.end() || o->hidden)
                return std::nullopt;
            const auto model = scene.models.find(rct3::park::object::model_ref(*o));
            return glm::vec3(rct3::park::object::placed_transform(*o, model != scene.models.end() ? &model->second : nullptr, scene.grid, scene.fitted)[3]);
        }

        /// How guests sit on a bench, as the game's motions name them: down onto it, sitting, turned to talk to
        /// someone on the seat to one side or the other, and up again.
        const std::int32_t kSitDownMotion = rct3::park::person::motion_number("sitbenchin").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kSitMotion = rct3::park::person::motion_number("sitbenchloop").value_or(rct3::park::person::motion::kIdle);
        const std::array<std::int32_t, 2> kSitChatMotions { rct3::park::person::motion_number("sitbenchconvl").value_or(rct3::park::person::motion::kIdle), rct3::park::person::motion_number("sitbenchconvr").value_or(rct3::park::person::motion::kIdle) };
        const std::int32_t kStandUpMotion = rct3::park::person::motion_number("sitbenchout").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kSitEatMotion = rct3::park::person::motion_number("sitbencheatlhand").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kSitDrinkMotion = rct3::park::person::motion_number("sitbenchdrinkl").value_or(rct3::park::person::motion::kIdle);
        constexpr float kStepToSeat = 0.8f; ///< Chosen by eye: seconds from the path to the seat and back
        /// How guests fire a water cannon, as the game's motions name them: taking hold, firing, letting go.
        const std::int32_t kCannonInMotion = rct3::park::person::motion_number("watercannonin").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kCannonMotion = rct3::park::person::motion_number("watercannonloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kCannonOutMotion = rct3::park::person::motion_number("watercannonout").value_or(rct3::park::person::motion::kIdle);
        /// How a party poses at a view, as the game's motions name them: one filming the rest, teens slouching.
        const std::int32_t kVideoInMotion = rct3::park::person::motion_number("videoin").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kVideoMotion = rct3::park::person::motion_number("videoloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kVideoOutMotion = rct3::park::person::motion_number("videoout").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kSlouchInMotion = rct3::park::person::motion_number("slouchin").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kSlouchMotion = rct3::park::person::motion_number("slouchloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kSlouchOutMotion = rct3::park::person::motion_number("slouchout").value_or(rct3::park::person::motion::kIdle);

        /// What a guest holds of what they carry from a stall: it while there is some left, then its empty; nothing
        /// once only empties in their pockets are left, or when its look is not known.
        const rct3::park::object::ObjectModel* held_model(const ParkScene& scene, const simulation::Carried& carried)
        {
            const auto& r = scene.sim.registry;
            const auto* shop = r.valid(carried.stall) ? r.try_get<simulation::Shop>(carried.stall) : nullptr;
            const auto look = shop != nullptr && carried.ware < shop->menu.size() ? scene.carried_looks.find(shop->menu[carried.ware].symbol) : scene.carried_looks.end();
            const auto* model = look == scene.carried_looks.end() || !carried.held ? nullptr : carried.bites > 0 ? &look->second.full : &look->second.empty;
            return model != nullptr && *model ? &**model : nullptr;
        }

        /// How guests walk holding an umbrella up, as the game's motions name it.
        const std::int32_t kUmbrellaMotion = rct3::park::person::motion_number("walkumbrella").value_or(rct3::park::person::motion::kWalk);
        /// How guests carry food or drink: walking with it held up, taking a bite or a sip on the way, and
        /// standing holding it, as the game's motions name them.
        const std::int32_t kCarryMotion = rct3::park::person::motion_number("walkumbrella").value_or(rct3::park::person::motion::kWalk);
        const std::int32_t kEatMotion = rct3::park::person::motion_number("walkeatlhand").value_or(rct3::park::person::motion::kWalk);
        const std::int32_t kDrinkMotion = rct3::park::person::motion_number("walkdrinklhand").value_or(rct3::park::person::motion::kWalk);
        const std::int32_t kHoldMotion = rct3::park::person::motion_number("holditem").value_or(rct3::park::person::motion::kIdle);
        constexpr float kBiteSeconds = 1.5f; ///< Chosen by eye: how long a bite or a sip shows
        /// How a guest hands over their ticket at a ride's entrance, as the game's motions name it.
        const std::int32_t kTicketMotion = rct3::park::person::motion_number("handticket").value_or(rct3::park::person::motion::kIdle);
        /// How a guest counts the cash a cash machine gave them, as the game's motions name it.
        const std::int32_t kCountCashMotion = rct3::park::person::motion_number("checkchange").value_or(rct3::park::person::motion::kIdle);
        /// How a person picked up hangs, enjoying it or cross, as the game's motions name it.
        const std::int32_t kDangleHappyMotion = rct3::park::person::motion_number("danglehappy").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kDangleCrossMotion = rct3::park::person::motion_number("dangleangry").value_or(rct3::park::person::motion::kIdle);
        /// Swimming along, and treading water while still.
        const std::int32_t kSwimMotion = rct3::park::person::motion_number("swimloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kTreadMotion = rct3::park::person::motion_number("swimtreadwaterloop").value_or(rct3::park::person::motion::kIdle);
        /// Sitting on their own float, still or paddling it along.
        const std::int32_t kFloatSitMotion = rct3::park::person::motion_number("inflatablesitloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kFloatPaddleMotion = rct3::park::person::motion_number("inflatablepaddleloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kTreadInMotion = rct3::park::person::motion_number("swimtreadwaterin").value_or(rct3::park::person::motion::kIdle);
        /// Diving in off the pool's edge or a small board, and climbing out over its wall or by a ladder.
        const std::int32_t kDiveMotion = rct3::park::person::motion_number("swimdive").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kClimbOutMotion = rct3::park::person::motion_number("swimout").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kOutByLadderMotion = rct3::park::person::motion_number("swimoutladder").value_or(rct3::park::person::motion::kIdle);
        /// Up a high diving board's ladder, and off its end: springing, falling and coming back up.
        const std::int32_t kClimbLadderMotion = rct3::park::person::motion_number("swimclimbladderloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kHighDiveInMotion = rct3::park::person::motion_number("swimdivehiin").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kHighDiveMotion = rct3::park::person::motion_number("swimdivehiloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kHighDiveOutMotion = rct3::park::person::motion_number("swimdivehiout").value_or(rct3::park::person::motion::kIdle);
        /// Lying down on a sun lounger, lying there and getting up.
        const std::int32_t kLieDownMotion = rct3::park::person::motion_number("sunloungerin").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kLoungeMotion = rct3::park::person::motion_number("sunloungerloop").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kGetUpMotion = rct3::park::person::motion_number("sunloungerout").value_or(rct3::park::person::motion::kIdle);
        /// What party members standing together in a ring do, in turns: chatting, laughing, nodding, pointing.
        const std::array<std::int32_t, 4> kChatMotions { rct3::park::person::motion::kIdle, rct3::park::person::motion_number("laugh").value_or(rct3::park::person::motion::kIdle), rct3::park::person::motion_number("nodhead").value_or(rct3::park::person::motion::kIdle), rct3::park::person::motion_number("point").value_or(rct3::park::person::motion::kIdle) };
        constexpr float kChatTurnSeconds = 4; ///< How long one of them keeps at one thing

        /// What guests who have queued for ages do now and then: look at their watch, tap a foot.
        const std::array<std::int32_t, 2> kImpatientMotions { rct3::park::person::motion_number("lookwatch").value_or(rct3::park::person::motion::kIdle), rct3::park::person::motion_number("tapfoot").value_or(rct3::park::person::motion::kIdle) };
        constexpr std::size_t kImpatientEvery = 3; ///< Of their turns, one is impatient; the others idle

        /// The motion of a simulated guest standing still: something from kChatMotions while their party
        /// stands together, now and then something impatient after queueing for ages, else idling.
        std::int32_t chat_motion(const ParkScene& scene, const ShownPerson& shown)
        {
            const auto& r = scene.sim.registry;
            // Each by turns, offset by who they are so they do not all move at once.
            const auto turn = static_cast<std::size_t>(scene.park_clock / kChatTurnSeconds) + entt::to_integral(shown.guest);
            if (const auto& errand = r.get<simulation::Errand>(shown.guest); errand.kind == simulation::ErrandKind::Queueing && errand.queued.value >= simulation::kLongQueue)
                return turn % kImpatientEvery == 0 ? kImpatientMotions[turn / kImpatientEvery % kImpatientMotions.size()] : rct3::park::person::motion::kIdle;
            const auto* member = r.try_get<simulation::Member>(shown.guest);
            if (member == nullptr || r.get<simulation::Party>(member->party).huddle == 0)
                return rct3::park::person::motion::kIdle;
            return kChatMotions[turn % kChatMotions.size()];
        }

        /// How long a guest at a stall's counter has been there, by what is left of their turn.
        float served_for(const simulation::Errand& errand)
        {
            return simulation::kTuning.facilities.served.value - static_cast<float>(errand.rest) * simulation::kTickSeconds;
        }

        /// What vendors do: serve a guest at their counter within kServeReach, then take their money; take the
        /// money from a rider handing over their ticket; else stand bored, now and then stacking the shelves.
        const std::int32_t kServeMotion = rct3::park::person::motion_number("serve").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kTakeMoneyMotion = rct3::park::person::motion_number("takemoney").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kBoredMotion = rct3::park::person::motion_number("idlebored").value_or(rct3::park::person::motion::kIdle);
        const std::int32_t kShelvesMotion = rct3::park::person::motion_number("idlestackshelfs").value_or(rct3::park::person::motion::kIdle);
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
    } // namespace

    const std::array<std::int32_t, kGaits> kGaitMotions { rct3::park::person::motion::kWalk, rct3::park::person::motion_number("run").value_or(rct3::park::person::motion::kWalk), rct3::park::person::motion_number("walktired").value_or(rct3::park::person::motion::kWalk), rct3::park::person::motion_number("walksick").value_or(rct3::park::person::motion::kWalk), rct3::park::person::motion_number("walktoilet").value_or(rct3::park::person::motion::kWalk) };

    rct3::park::person::PlacedPerson dressed(const ShownPerson& shown)
    {
        rct3::park::person::PlacedPerson pose = shown.pose;
        if (shown.swimwear)
        {
            pose.look.swimwear = true;
            pose.look.body = shown.swim_body != nullptr ? shown.swim_body : pose.look.body;
            pose.look.legs = shown.swim_legs != nullptr ? shown.swim_legs : pose.look.legs;
        }
        return pose;
    }

    std::optional<glm::mat4> bench_seat(const ParkScene& scene, entt::entity guest)
    {
        const auto& r = scene.sim.registry;
        const auto* linger = r.try_get<simulation::Linger>(guest);
        const auto* item = linger != nullptr && r.valid(linger->bench) ? r.try_get<simulation::PathItem>(linger->bench) : nullptr;
        if (item == nullptr || linger->at == simulation::PathItemKind::Sight || linger->at == simulation::PathItemKind::PhotoSpot)
            return std::nullopt;
        const auto o = std::ranges::find(scene.objects, item->object, &save::model::PlacedObject::id);
        const auto model = o != scene.objects.end() && !o->hidden ? scene.models.find(rct3::park::object::model_ref(*o)) : scene.models.end();
        if (model == scene.models.end() || linger->seat >= model->second.seat_places.size())
            return std::nullopt;
        return rct3::park::object::placed_transform(*o, &model->second, scene.grid, scene.fitted) * model->second.seat_places[linger->seat];
    }

    bool own_float(const rct3::park::person::PersonLibrary& library, const rct3::park::person::PlacedPerson& person)
    {
        if (!person.look.swimwear || person.look.body == nullptr)
            return false;
        const rct3::park::person::PartLevel& pieces = person.look.body->swim_levels[0];
        for (std::size_t k = 0; k < pieces.optional.size() && k < 32; ++k)
        {
            if ((person.look.optionals[1] >> k & 1u) != 0 && pieces.optional[k] != rct3::park::person::kNoPiece && library.piece(pieces.optional[k]).texture.contains("inflatable"))
                return true;
        }
        return false;
    }

    std::string_view slide_motion(float heading, glm::vec2 drift, bool staff)
    {
        if (staff)
            return "dangleangry"; // Staff meshes have no sliding motions
        const float ahead = glm::length(drift) > 0 ? glm::dot(glm::vec2 { std::sin(heading), std::cos(heading) }, glm::normalize(drift)) : 0.0f;
        constexpr float kFacing = 0.5f; ///< Chosen by eye: how squarely they face down or up the slope to slide on their front or back
        return ahead > kFacing ? "frontslide" : ahead < -kFacing ? "backslide" : "sitslide";
    }

    std::optional<rct3::park::person::PlacedPerson> pose_person(const ParkScene& scene, std::size_t i, float clock, bool simulate)
    {
        const ShownPerson& shown = scene.shown_people[i];
        const bool simulated = simulate && shown.guest != entt::null;
        rct3::park::person::PlacedPerson pose = dressed(shown);
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
                pose.motion = rct3::park::person::motion_number(simulation::staff::keeper_motion(job, walking)).value_or(walking ? rct3::park::person::motion::kWalk : rct3::park::person::motion::kIdle);
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
                pose.motion = rct3::park::person::motion_number(whole % kLookEvery == 0 ? "lifeguardidle01" : "lifeguardidlestandard").value_or(rct3::park::person::motion::kIdle);
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
                pose.motion = rct3::park::person::motion_number(kShowMotions[std::to_underlying(move)]).value_or(rct3::park::person::motion::kIdle);
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
                pose.motion = rct3::park::person::motion_number(kGestureMotions[std::to_underlying(gesture->kind)]).value_or(rct3::park::person::motion::kIdle);
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
                    pose.motion = rct3::park::person::motion_number(into < kClapEnds ? "applausein" : left < kClapEnds ? "applauseout" : "applause").value_or(rct3::park::person::motion::kIdle);
                    pose.time = into < kClapEnds ? into : left < kClapEnds ? kClapEnds - left : into - kClapEnds;
                }
                // Knocked over, they go down, lie there, and get up for the last of it.
                constexpr float kGettingUp = 1.5f; ///< Seconds of it a knocked over guest takes getting up
                if (gesture->kind == simulation::GestureKind::Down)
                {
                    const float down = simulation::kTuning.crashes.knocked.value - gesture->left.value + scene.sim_time;
                    const bool up = gesture->left.value - scene.sim_time < kGettingUp;
                    pose.motion = rct3::park::person::motion_number(down < kGettingUp ? "knockdownin" : up ? "knockdownout" : "knockdownloop").value_or(rct3::park::person::motion::kIdle);
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
                    pose.motion = rct3::park::person::motion_number(slide_motion(at.heading.value, drop->drift, shown.staff)).value_or(rct3::park::person::motion::kIdle);
                    return pose;
                }
                const bool falling = held == nullptr && now.position.y > r.get<simulation::Dropped>(shown.guest).ground;
                pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), held != nullptr ? now.position : at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                pose.motion = held != nullptr ? (held->happy ? kDangleHappyMotion : kDangleCrossMotion) : falling ? kDangleCrossMotion : rct3::park::person::motion::kWalk;
                // Falling into a pool they tip over as they go, to land in the water.
                // Off the end of a pool slide, they splash down as they rode it, on their back or in their ring.
                if (const auto* drop = held == nullptr ? &r.get<simulation::Dropped>(shown.guest) : nullptr; falling && drop->pool != entt::null)
                {
                    const auto cars = drop->slide != entt::null ? scene.ride_cars.find(drop->slide) : scene.ride_cars.end();
                    const auto model = cars != scene.ride_cars.end() && !cars->second.empty() ? scene.models.find(rct3::park::object::model_ref(scene.cars[cars->second.front()])) : scene.models.end();
                    const bool body = (model != scene.models.end() && model->second.seating == rct3::park::person::kSlideBodySeating) || (r.valid(drop->slide) && r.all_of<simulation::ClimbSlide>(drop->slide));
                    pose.motion = rct3::park::person::motion_number(drop->slide == entt::null ? "knockdownin" : body ? "slidebodybacksplashdownin" : "slideringsplashdownin").value_or(pose.motion);
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
                    if (!square || square->kind != rct3::park::pools::PoolKind::Water || scene.grid.tile_size_x <= 0 || scene.grid.tile_size_z <= 0)
                        return at.position;
                    const auto water = [&](int dc, int dr) {
                        const auto next = scene.pools.at(col + dc, row + dr);
                        return next && next->kind == rct3::park::pools::PoolKind::Water && next->surface == square->surface;
                    };
                    const float room = rct3::park::pool_wall_room(scene.grid, at_xz - glm::vec2 { scene.grid.world_x(col), scene.grid.world_z(row) }, water);
                    return at.position + glm::vec3(0.0f, rct3::park::pools::pool_wave_lift(rct3::park::pools::pool_wave_height(scene.water.pool_waves, at_xz, clock, at.position.y), room), 0.0f);
                };
                // Up a slide climbed on foot they walk, and slide down it on their backs.
                if (const auto* climbing = r.try_get<simulation::Climbing>(shown.guest))
                {
                    const auto* slide = r.valid(climbing->slide) ? r.try_get<simulation::ClimbSlide>(climbing->slide) : nullptr;
                    const bool down = slide != nullptr && climbing->way < slide->ways.size() && climbing->along >= slide->ways[climbing->way].top;
                    pose.world = glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f));
                    pose.motion = down ? rct3::park::person::motion_number("slidebodybackloop").value_or(rct3::park::person::motion::kWalk) : rct3::park::person::motion::kWalk;
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
                pose.motion = swimmer->in_water ? (moving ? kSwimMotion : kTreadMotion) : moving ? rct3::park::person::motion::kWalk : rct3::park::person::motion::kIdle;
                if (!swimmer->in_water && moving && shown.paces[0] > 0)
                    pose.time = at.walked.value / shown.paces[0];
                const auto length = [&](std::int32_t motion) {
                    rct3::park::person::PlacedPerson probe = pose;
                    probe.motion = motion;
                    return rct3::park::person::motion_seconds(*scene.person_models, probe);
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
                    const std::array clips { rct3::park::person::Clip { down, length(down) }, rct3::park::person::Clip { lounging ? kLoungeMotion : kSitMotion, std::max(stay - length(down) - length(up), 0.0f) }, rct3::park::person::Clip { up, length(up) } };
                    const rct3::park::person::Clip step = rct3::park::person::clip_at(clips, std::min(into, stay));
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
                const auto o = item != nullptr ? std::ranges::find(scene.objects, item->object, &save::model::PlacedObject::id) : scene.objects.end();
                if (o != scene.objects.end() && !o->hidden)
                {
                    constexpr float kRowGap = 0.6f;  ///< Chosen by eye: between two in the row
                    constexpr float kRowBack = 0.8f; ///< Chosen by eye: from the view to the row
                    constexpr float kFilming = 1.2f; ///< Chosen by eye: from the row's end to the one filming it
                    const auto model = scene.models.find(rct3::park::object::model_ref(*o));
                    const glm::mat4 placed = rct3::park::object::placed_transform(*o, model != scene.models.end() ? &model->second : nullptr, scene.grid, scene.fitted);
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
                        pose.motion = rct3::park::person::motion::kWalk;
                        if (shown.paces[0] > 0)
                            pose.time = off * glm::distance(at.position, stand) / kStepToSeat / shown.paces[0];
                        return pose;
                    }
                    const auto length = [&](std::int32_t motion) {
                        rct3::park::person::PlacedPerson probe = pose;
                        probe.motion = motion;
                        return rct3::park::person::motion_seconds(*scene.person_models, probe);
                    };
                    const auto* visitor = r.try_get<simulation::Visitor>(shown.guest);
                    const bool slouch = !linger->camera && visitor != nullptr && visitor->age == simulation::AgeGroup::Teen && entt::to_integral(shown.guest) % 2 == 0;
                    const std::int32_t in = linger->camera ? kVideoInMotion : slouch ? kSlouchInMotion : rct3::park::person::motion::kIdle;
                    const std::int32_t out = linger->camera ? kVideoOutMotion : slouch ? kSlouchOutMotion : rct3::park::person::motion::kIdle;
                    const float stay = watch - 2 * kStepToSeat;
                    const float begin = in == rct3::park::person::motion::kIdle ? 0.0f : length(in);
                    const float end = out == rct3::park::person::motion::kIdle ? 0.0f : length(out);
                    const std::array clips { rct3::park::person::Clip { in, begin }, rct3::park::person::Clip { linger->camera ? kVideoMotion : slouch ? kSlouchMotion : rct3::park::person::motion::kIdle, std::max(stay - begin - end, 0.0f) }, rct3::park::person::Clip { out, end } };
                    const rct3::park::person::Clip step = rct3::park::person::clip_at(clips, std::min(into - kStepToSeat, stay));
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
                    pose.motion = rct3::park::person::motion::kWalk;
                    if (shown.paces[0] > 0)
                        pose.time = part * glm::distance(at.position, to) / shown.paces[0];
                };
                const auto length = [&](std::int32_t motion) {
                    rct3::park::person::PlacedPerson probe = pose;
                    probe.motion = motion;
                    return rct3::park::person::motion_seconds(*scene.person_models, probe);
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
                    const std::array clips { rct3::park::person::Clip { kCannonInMotion, hold }, rct3::park::person::Clip { kCannonMotion, std::max(firing - 2 * kStepToSeat - hold - release, 0.0f) }, rct3::park::person::Clip { kCannonOutMotion, release } };
                    const rct3::park::person::Clip step = rct3::park::person::clip_at(clips, into - kStepToSeat);
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
                    pose.motion = rct3::park::person::motion::kWalk;
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
                const simulation::staff::WorkLook look = simulation::staff::work_look(staff->job, upkeep != nullptr ? upkeep->kind : simulation::Breakdown::None);
                // Played once, the start and the end as long as they last; the rest of the job repeats its loop.
                const auto once = [&](std::string_view name) -> std::optional<rct3::park::person::Clip> {
                    const auto number = name.empty() ? std::nullopt : rct3::park::person::motion_number(name);
                    if (!number)
                        return std::nullopt;
                    rct3::park::person::PlacedPerson probe = pose;
                    probe.motion = *number;
                    return rct3::park::person::Clip { *number, rct3::park::person::motion_seconds(*scene.person_models, probe) };
                };
                const auto start = once(look.start);
                const auto finish = once(look.finish);
                const float length = staff->job_length.value;
                std::vector<rct3::park::person::Clip> clips;
                if (start)
                    clips.push_back(*start);
                clips.push_back({ rct3::park::person::motion_number(look.loop).value_or(rct3::park::person::motion::kIdle), std::max(length - (start ? start->seconds : 0.0f) - (finish ? finish->seconds : 0.0f), 0.0f) });
                if (finish)
                    clips.push_back(*finish);
                const rct3::park::person::Clip step = rct3::park::person::clip_at(clips, std::min(length - staff->busy.value + scene.sim_time, length));
                pose.motion = step.motion;
                pose.time = step.seconds;
                const auto tool = scene.staff_tools.find(look.tool);
                pose.tool = tool != scene.staff_tools.end() ? &tool->second : nullptr;
            }
            // Guards have no plain standing motion; they tap a foot instead.
            else if (staff != nullptr && staff->role == simulation::StaffRole::Guard && pose.motion == rct3::park::person::motion::kIdle)
                pose.motion = rct3::park::person::motion_number("tapfoot").value_or(rct3::park::person::motion::kIdle);
            const bool idle = pose.motion == rct3::park::person::motion::kIdle;
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

} // namespace rct3::scene::park
