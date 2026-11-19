// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/ride_camera.hpp"

#include "core/types.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/rides.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/info/attraction.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        namespace skins = ui::game_skins;

        /// The kinds of camera after `eye`, round from the rear to the nose, that the train car `car` is on has.
        std::optional<rct3::park::object::RideEye> next_eye(const ParkScene& scene, usize car, rct3::park::object::RideEye eye)
        {
            for (u8 step = 1; step < 3; ++step)
            {
                const auto other = static_cast<rct3::park::object::RideEye>((std::to_underlying(eye) + step) % 3);
                if (std::ranges::any_of(ride_seats(scene, scene.car_spots[car].ride, other), [&](usize c) { return scene.car_spots[c].train == scene.car_spots[car].train; }))
                    return other;
            }
            return std::nullopt;
        }

        /// Every train of the park a view can ride along in, ride by ride in the park's order: the first car of
        /// each with a camera of kind `eye`, or on a ride with none of that kind, of the first kind it has of the
        /// front seat, rear seat and nose.
        std::vector<std::pair<usize, rct3::park::object::RideEye>> train_stops(const ParkScene& scene, rct3::park::object::RideEye eye)
        {
            std::vector<std::pair<usize, rct3::park::object::RideEye>> stops;
            for (usize r = 0; r < scene.rides.rides.size(); ++r)
                for (const rct3::park::object::RideEye kind : { eye, rct3::park::object::RideEye::Front, rct3::park::object::RideEye::Rear, rct3::park::object::RideEye::Nose })
                    if (const std::vector<usize> seats = ride_seats(scene, save::model::RideIndex { r }, kind); !seats.empty())
                    {
                        std::vector<usize> trains;
                        for (const usize seat : seats)
                            if (!std::ranges::contains(trains, scene.car_spots[seat].train))
                            {
                                trains.push_back(scene.car_spots[seat].train);
                                stops.emplace_back(seat, kind);
                            }
                        break;
                    }
            return stops;
        }

        /// Unless already riding: everything else on screen is put away, the ride camera panel shows, and a park
        /// that was paused is set going so the ride moves.
        void start_riding(ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            if (scene.riding_along())
                return;
            ui::Widget& root = scene.desktop->root();
            scene.hidden_for_ride.clear();
            for (const auto& child : root.children())
                if (child->visible && child->name() != kGuideName)
                {
                    child->visible = false;
                    scene.hidden_for_ride.emplace_back(child->name());
                }
            root.add<ui::screens::park_view::RideCameraPanel>(std::string(kRideCameraName), skins::ride_camera_panel_style(assets.palette), *assets.strings);
            scene.paused_for_ride = scene.park.speed.paused;
            scene.park.speed.paused = false;
        }

        /// How many rider seats or fixed cameras (`eye`) flat ride `ride` has to look out from.
        usize flat_views(const ParkScene& scene, save::model::RideIndex ride, FlatEye eye)
        {
            usize count = 0;
            while (flat_view(scene, ride, eye, count))
                ++count;
            return count;
        }

        /// The flat rides with rider seats or fixed cameras to look out from, in the park's order.
        std::vector<save::model::RideIndex> flat_rides(const ParkScene& scene)
        {
            std::vector<save::model::RideIndex> rides;
            for (save::model::RideIndex ride { }; ride.value < scene.rides.rides.size(); ++ride.value)
                if (!scene.rides.rides[ride.value].base.demolished && (flat_view(scene, ride, FlatEye::Seat, 0) || flat_view(scene, ride, FlatEye::Fixed, 0)))
                    rides.push_back(ride);
            return rides;
        }

        /// Looks out from rider seat or fixed camera (`eye`) `at` of flat ride `ride`, as ride_along does from a
        /// car. The type button changes between seats and fixed cameras, the train buttons go from flat ride to
        /// flat ride.
        void ride_flat(ParkScene& scene, const ui::InterfaceAssets& assets, save::model::RideIndex ride, FlatEye eye, usize at)
        {
            start_riding(scene, assets);
            if (scene.riding_flat != std::pair { ride, at } || scene.flat_eye != eye)
            {
                scene.ride_turn = { };
                scene.ride_pull = 0;
            }
            scene.riding.reset();
            scene.riding_person.reset();
            scene.riding_flat = { ride, at };
            scene.flat_eye = eye;
            const ui::StringTable& strings = *assets.strings;
            constexpr std::array<std::string_view, 2> kNames { "RidecamGUI_TypePeep", "RidecamGUI_TypeStatic" };
            std::u16string info = ui::screens::info::attraction_name(scene.rides.rides[ride.value].base, strings);
            const FlatEye other_eye = eye == FlatEye::Fixed ? FlatEye::Seat : FlatEye::Fixed;
            info += u"  " + std::u16string(strings.get(kNames[std::to_underlying(eye)])) + u" " + strings::to_utf16(std::format("{}", at + 1));
            const bool other = flat_views(scene, ride, other_eye) > 0;
            const std::optional<usize> next = other ? std::optional<usize>(eye == FlatEye::Fixed ? ui::screens::park_view::kFlatSeatType : ui::screens::park_view::kFlatFixedType) : std::nullopt;
            if (auto panel = scene.desktop->root().find_as<ui::screens::park_view::RideCameraPanel>(kRideCameraName))
                panel->show(std::move(info), { .cameras = flat_views(scene, ride, eye) > 1, .trains = flat_rides(scene).size() > 1 }, strings.get("RidecamGUI_Ride"), next, other ? strings.get(kNames[std::to_underlying(other_eye)]) : std::u16string_view { });
        }

        /// Rides along in `car`, from its camera of kind `eye`.
        void ride_along(ParkScene& scene, const ui::InterfaceAssets& assets, usize car, rct3::park::object::RideEye eye)
        {
            ui::Widget& root = scene.desktop->root();
            start_riding(scene, assets);
            if (scene.riding != car || scene.riding_eye != eye)
            {
                scene.ride_view = { };
                scene.ride_turn = { };
                scene.ride_pull = 0;
            }
            scene.riding_flat.reset();
            scene.riding_person.reset();
            scene.riding = car;
            scene.riding_eye = eye;
            // Named as the game names them: the ride, then the train and the car counting from 1.
            const rct3::park::ride::CarSpot spot = scene.car_spots[car];
            usize place = 1;
            for (usize c = 0; c < car; ++c)
                place += scene.car_spots[c].ride == spot.ride && scene.car_spots[c].train == spot.train ? 1 : 0;
            const ui::StringTable& strings = *assets.strings;
            std::u16string info = ui::screens::info::attraction_name(scene.rides.rides[spot.ride.value].base, strings);
            info += u"  " + std::u16string(strings.get("RidecamGUI_Train")) + u" " + strings::to_utf16(std::format("{}", spot.train + 1));
            info += u"  " + std::u16string(strings.get("RidecamGUI_Car")) + u" " + strings::to_utf16(std::format("{}", place));
            constexpr std::array<std::string_view, 3> kEyeNames { "RidecamGUI_TypeNose", "RidecamGUI_TypeFront", "RidecamGUI_TypeRear" };
            info += u"  " + std::u16string(strings.get(kEyeNames[std::to_underlying(eye)]));
            const auto cameras = std::ranges::count_if(ride_seats(scene, spot.ride, eye), [&](usize seat) { return scene.car_spots[seat].train == spot.train; });
            const std::optional<rct3::park::object::RideEye> next = next_eye(scene, car, eye);
            if (auto panel = root.find_as<ui::screens::park_view::RideCameraPanel>(kRideCameraName))
                panel->show(std::move(info), { .cameras = cameras > 1, .trains = train_stops(scene, eye).size() > 1 }, strings.get("RidecamGUI_Train"), next.transform([](rct3::park::object::RideEye e) { return usize { std::to_underlying(e) }; }), next ? strings.get(kEyeNames[std::to_underlying(*next)]) : std::u16string_view { });
        }

    } // namespace

    std::vector<usize> ride_seats(const ParkScene& scene, save::model::RideIndex ride, rct3::park::object::RideEye eye)
    {
        std::vector<usize> seats;
        for (usize i = 0; i < scene.cars.size() && i < scene.car_spots.size() && !scene.rides.rides.at(ride.value).base.demolished; ++i)
            if (scene.car_spots[i].ride == ride && ride_eye(scene, i, eye))
                seats.push_back(i);
        return seats;
    }

    void ride_person(ParkScene& scene, const ui::InterfaceAssets& assets, usize person, bool behind)
    {
        if (person >= scene.shown_people.size())
            return;
        start_riding(scene, assets);
        if (scene.riding_person != person || scene.person_behind != behind)
        {
            scene.ride_turn = { };
            scene.ride_pull = 0;
        }
        scene.riding.reset();
        scene.riding_flat.reset();
        scene.riding_person = person;
        scene.person_behind = behind;
        const ShownPerson& p = scene.shown_people[person];
        const save::model::Person* saved = p.staff ? (p.saved < scene.people.staff.size() ? &scene.people.staff[p.saved].person : nullptr) : (p.saved < scene.people.guests.size() ? &scene.people.guests[p.saved].person : nullptr);
        const ui::StringTable& strings = *assets.strings;
        constexpr std::array<std::string_view, 2> kNames { "RidecamGUI_TypePeep", "RidecamGUI_TypeStatic" };
        if (auto panel = scene.desktop->root().find_as<ui::screens::park_view::RideCameraPanel>(kRideCameraName))
            panel->show(saved != nullptr ? strings::to_utf16(saved->name) : std::u16string { }, { .trains = scene.shown_people.size() > 1 }, { }, behind ? ui::screens::park_view::kFlatSeatType : ui::screens::park_view::kFlatFixedType, strings.get(kNames[!behind]));
    }

    void ride_cam(ParkScene& scene, const ui::InterfaceAssets& assets, save::model::RideIndex ride)
    {
        if (flat_view(scene, ride, FlatEye::Seat, 0) || flat_view(scene, ride, FlatEye::Fixed, 0))
        {
            ride_flat(scene, assets, ride, flat_view(scene, ride, FlatEye::Seat, 0) ? FlatEye::Seat : FlatEye::Fixed, 0);
            return;
        }
        const entt::entity e = ride.value < scene.ride_entities.size() ? scene.ride_entities[ride.value] : entt::null;
        const auto* fleet = e != entt::null ? scene.sim.registry.try_get<simulation::Trains>(e) : nullptr;
        for (const rct3::park::object::RideEye eye : { rct3::park::object::RideEye::Front, rct3::park::object::RideEye::Rear, rct3::park::object::RideEye::Nose })
            if (const std::vector<usize> seats = ride_seats(scene, ride, eye); !seats.empty())
            {
                const auto running = std::ranges::find_if(seats, [&](usize car) { return fleet != nullptr && scene.car_spots[car].train < fleet->trains.size() && fleet->trains[scene.car_spots[car].train].running; });
                ride_along(scene, assets, running != seats.end() ? *running : seats.front(), eye);
                return;
            }
    }

    void ride_action(ParkScene& scene, const ui::InterfaceAssets& assets, ui::screens::park_view::RideCameraAction action)
    {
        using enum ui::screens::park_view::RideCameraAction;
        if (action == Leave)
        {
            stop_riding(scene);
            return;
        }
        const bool next = action == NextCamera || action == NextTrain;
        const auto step = [&](usize at, usize count) { return (at + (next ? 1 : count - 1)) % count; };
        // With a person, the type changes between their eyes and behind them, and the trains are the other people.
        if (scene.riding_person)
        {
            if (action == ChangeType)
                ride_person(scene, assets, *scene.riding_person, !scene.person_behind);
            else if ((action == PreviousTrain || action == NextTrain) && !scene.shown_people.empty())
                ride_person(scene, assets, step(*scene.riding_person, scene.shown_people.size()), scene.person_behind);
            return;
        }
        // On a flat ride, the cameras are its seats or its fixed cameras, the type changes between them, and the
        // trains are the park's other flat rides, entered as ride_cam does.
        if (scene.riding_flat)
        {
            const auto [ride, view] = *scene.riding_flat;
            const FlatEye eye = scene.flat_eye;
            const FlatEye other_eye = eye == FlatEye::Fixed ? FlatEye::Seat : FlatEye::Fixed;
            const usize views = flat_views(scene, ride, eye);
            const std::vector<save::model::RideIndex> rides = flat_rides(scene);
            if ((action == PreviousCamera || action == NextCamera) && views > 0)
                ride_flat(scene, assets, ride, eye, step(view, views));
            else if (action == ChangeType && flat_views(scene, ride, other_eye) > 0)
                ride_flat(scene, assets, ride, other_eye, 0);
            else if (const auto at = std::ranges::find(rides, ride); (action == PreviousTrain || action == NextTrain) && at != rides.end())
            {
                const save::model::RideIndex to = rides[step(static_cast<usize>(at - rides.begin()), rides.size())];
                ride_flat(scene, assets, to, flat_view(scene, to, FlatEye::Seat, 0) ? FlatEye::Seat : FlatEye::Fixed, 0);
            }
            return;
        }
        if (!scene.riding)
            return;
        const rct3::park::ride::CarSpot spot = scene.car_spots[*scene.riding];
        if (action == ChangeType)
        {
            if (const auto next = next_eye(scene, *scene.riding, scene.riding_eye))
                for (const usize seat : ride_seats(scene, spot.ride, *next))
                    if (scene.car_spots[seat].train == spot.train)
                    {
                        ride_along(scene, assets, seat, *next);
                        break;
                    }
            return;
        }
        if (action == PreviousCamera || action == NextCamera)
        {
            std::vector<usize> stops = ride_seats(scene, spot.ride, scene.riding_eye);
            std::erase_if(stops, [&](usize seat) { return scene.car_spots[seat].train != spot.train; });
            if (const auto at = std::ranges::find(stops, *scene.riding); at != stops.end())
                ride_along(scene, assets, stops[step(static_cast<usize>(at - stops.begin()), stops.size())], scene.riding_eye);
            return;
        }
        const std::vector<std::pair<usize, rct3::park::object::RideEye>> stops = train_stops(scene, scene.riding_eye);
        const auto at = std::ranges::find_if(stops, [&](const auto& stop) { return scene.car_spots[stop.first].ride == spot.ride && scene.car_spots[stop.first].train == spot.train; });
        if (at != stops.end())
        {
            const auto [car, eye] = stops[step(static_cast<usize>(at - stops.begin()), stops.size())];
            ride_along(scene, assets, car, eye);
        }
    }

    void stop_riding(ParkScene& scene)
    {
        if (!scene.riding_along())
            return;
        ui::Widget& root = scene.desktop->root();
        for (const std::string& name : scene.hidden_for_ride)
            if (auto w = root.find(name))
                w->visible = true;
        if (auto panel = root.find(kRideCameraName))
            panel->close();
        if (scene.paused_for_ride)
            scene.park.speed.paused = true;
        scene.riding.reset();
        scene.riding_flat.reset();
        scene.riding_person.reset();
        scene.seat.reset();
    }

} // namespace rct3::scene::park
