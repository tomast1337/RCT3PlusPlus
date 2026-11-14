// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/track/trains.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/ride_train.hpp"
#include "ovl/tracked_ride.hpp"
#include "park/asset_index.hpp"
#include "park/object/models.hpp"
#include "park/track/rules.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/import_park.hpp"
#include "simulation/rides.hpp"
#include "strings/ascii.hpp"
#include "ui/screens/info/attraction.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string_view>

namespace rct3::scene::track
{

    std::optional<std::filesystem::path> train_file(const config::game::GameInstall& install, std::string_view train_name)
    {
        std::error_code error;
        const std::filesystem::path cars = install.find("Cars").value_or(std::filesystem::path { });
        for (auto it = std::filesystem::recursive_directory_iterator(cars, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            if (strings::equals_ignoring_case(it->path().filename().string(), std::format("{}.common.ovl", train_name)))
                return it->path();
        return std::nullopt;
    }

    namespace
    {

        /// The car counts trains of the kind named `train_name` allow, as the install's file of it sets them.
        ui::screens::info::TrainLimits train_limits_of(const config::game::GameInstall& install, std::string_view train_name)
        {
            ui::screens::info::TrainLimits limits;
            const auto file = train_file(install, train_name);
            if (!file)
                return limits;
            const auto car_file = ovl::Overlay::load(*file);
            for (const auto part : car_file ? std::array { core::maybe(&car_file->common()), car_file->unique() } : std::array<std::optional<const ovl::Part&>, 2> { })
                for (const ovl::RideTrainItem& t : part ? ovl::read_ride_trains(*car_file, *part) : std::vector<ovl::RideTrainItem> { })
                    if (t.train && strings::equals_ignoring_case(t.name, train_name) && t.train->fewest_cars > 0 && t.train->fewest_cars <= t.train->most_cars)
                        limits = { .fewest_cars = static_cast<int>(t.train->fewest_cars), .most_cars = static_cast<int>(t.train->most_cars), .usual_cars = static_cast<int>(std::clamp(t.train->usual_cars, t.train->fewest_cars, t.train->most_cars)) };
            return limits;
        }

    } // namespace

    bool fit_trains(park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index, int trains, int cars)
    {
        save::model::Ride& ride = scene.rides.rides[index];
        if (ride.trains.empty() || trains < 1 || cars < 1)
            return false;
        const std::string symbol = ride.trains.front().symbol;
        const std::string path = ride.trains.front().overlay_path;
        const std::string_view name = std::string_view(symbol).substr(0, symbol.find(':'));
        rct3::park::AssetIndex found = rct3::park::AssetIndex::scan(install.root(), { });
        if (const auto file = install.find(std::format("{}.common.ovl", path)))
            (void)found.add_file(*file);
        park::add_referenced(found, install); // Links between cars are often in files of their own
        const auto& course = index < scene.courses.size() ? scene.courses[index] : std::nullopt;
        const auto& lift = index < scene.track_rules.size() ? scene.track_rules[index].lift_car : std::nullopt;
        const auto lift_file = lift && course && !course->lifts.empty() ? train_file(install, lift->train) : std::nullopt;
        if (lift_file)
            (void)found.add_file(*lift_file);
        rct3::park::object::ModelLoader loader(found);
        // The train's designs by slot (rct3::park::ride::car_slots), each that it fills.
        std::array<std::optional<rct3::park::object::ObjectModel>, 6> designs;
        std::array<bool, 6> filled { };
        for (std::size_t slot = 0; slot < designs.size(); ++slot)
            filled[slot] = (designs[slot] = loader.load_car(name, path, slot)).has_value();
        // How each car of a train filling `slots` sits, by car; one without a model of its own takes no room.
        const auto fits_of = [&](const std::vector<std::size_t>& slots) {
            std::vector<rct3::park::object::CarFit> fits;
            for (const std::size_t slot : slots)
                fits.push_back(designs[slot] && designs[slot]->car_fit ? *designs[slot]->car_fit : rct3::park::object::CarFit { });
            return fits;
        };
        // On a course with a station, as many cars as its shortest station takes, as in the game; where even the
        // fewest its kind allows do not fit, that many all the same, so that any circuit runs.
        if (course && ride.mode != save::model::OperatingMode::MiniGolf && std::ranges::contains(course->kinds, rct3::park::track::CourseKind::Station))
        {
            const auto limits = scene.train_limits.find(index);
            const int fewest = std::min(limits != scene.train_limits.end() ? limits->second.fewest_cars : 1, cars);
            std::vector<float> lengths;
            for (int n = 1; n <= cars; ++n)
                lengths.push_back(rct3::park::track::train_length(fits_of(rct3::park::ride::car_slots(n, filled))));
            const int fit = rct3::park::track::cars_fitting(*course, lengths, fewest);
            cars = fit > 0 ? fit : fewest;
        }
        // No longer than the track a lift's car carries.
        if (lift_file)
        {
            std::string lift_path = std::filesystem::relative(*lift_file, install.root()).generic_string();
            lift_path.resize(lift_path.size() - std::string_view(".common.ovl").size());
            if (const auto carrier = loader.load_car(lift->train, lift_path, 0); carrier && rct3::park::track::carried_length(*carrier) > 0)
                while (cars > 1 && rct3::park::track::train_length(fits_of(rct3::park::ride::car_slots(cars, filled))) > rct3::park::track::carried_length(*carrier))
                    --cars;
        }
        // A car more where those there have no seat, as a front car alone may have none.
        while (cars < 2 && std::ranges::none_of(rct3::park::ride::car_slots(cars, filled), [&](std::size_t slot) { return designs[slot] && designs[slot]->seats > 0; }))
            ++cars;
        const std::vector<std::size_t> slots = rct3::park::ride::car_slots(cars, filled);
        std::vector<rct3::park::object::CarFit>& fits = scene.car_fits[index] = fits_of(slots);
        if (std::ranges::none_of(slots, [&](std::size_t slot) { return designs[slot] && designs[slot]->car_fit; }))
            fits.clear(); // Nothing to stand them by
        // As many trains as the course takes; a shuttle runs one.
        if (const auto coaster = course ? simulation::coaster_of(*course, ride) : std::nullopt; coaster && coaster->shuttle != simulation::Shuttle::None)
            trains = 1;
        else if (course)
            trains = std::min(trains, rct3::park::track::trains_fitting(*course, fits, ride.options.block_sectioned, index < scene.track_rules.size() ? scene.track_rules[index].most_trains : std::nullopt));
        ride.trains.assign(static_cast<std::size_t>(trains), save::model::Train { });
        ride.seats.clear();
        bool arm = false;
        for (int t = 0; t < trains; ++t)
        {
            save::model::Train& train = ride.trains[static_cast<std::size_t>(t)];
            train.symbol = symbol;
            train.overlay_path = path;
            for (std::size_t car = 0; car < slots.size(); ++car)
            {
                const auto& seated = designs[slots[car]];
                arm = arm || (seated && !seated->arm_joints.empty());
                train.cars.push_back({ .index = static_cast<std::int32_t>(car), .car_count = static_cast<std::int32_t>(slots.size()), .design_slot = static_cast<std::int32_t>(slots[car]) });
                for (std::uint32_t seat = 0; seated && seat < seated->seats; ++seat)
                    ride.seats.push_back({ .car = static_cast<std::int32_t>(car), .seat = static_cast<std::int32_t>(seat), .train = t });
            }
            train.length = rct3::park::track::train_length(fits);
        }
        ride.train_count = trains;
        ride.cars_per_train = cars;
        return arm;
    }

    std::optional<bool> use_train(park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index, std::string_view train_name)
    {
        save::model::Ride& ride = scene.rides.rides[index];
        if (const auto file = train_file(install, train_name))
        {
            std::string path = std::filesystem::relative(*file, install.root()).generic_string();
            path.resize(path.size() - std::string_view(".common.ovl").size());
            ride.trains.assign(1, save::model::Train { });
            save::model::Train& train = ride.trains.front();
            train.symbol = std::format("{}:rit", train_name);
            train.overlay_path = path;
            // Its stations take the platforms its train names, where the train names some; else its kind's.
            // shortcut: a train naming none keeps the platforms of the train before it; upgrade by keeping the
            // kind's own platform type apart if a ride offers trains of both sorts.
            const auto car_file = ovl::Overlay::load(*file);
            for (const auto part : car_file ? std::array { core::maybe(&car_file->common()), car_file->unique() } : std::array<std::optional<const ovl::Part&>, 2> { })
                for (const ovl::RideTrainItem& t : part ? ovl::read_ride_trains(*car_file, *part) : std::vector<ovl::RideTrainItem> { })
                    if (t.train && !t.train->platform.empty() && strings::equals_ignoring_case(t.name, train_name))
                        ride.platform_type = t.train->platform;
            // As many cars as its kind usually has, and the fewest and most it may have.
            const ui::screens::info::TrainLimits& limits = scene.train_limits[index] = train_limits_of(install, train_name);
            return fit_trains(scene, install, index, 1, limits.usual_cars);
        }
        return std::nullopt;
    }

    void ready_trains(park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index)
    {
        const save::model::Ride& ride = scene.rides.rides[index];
        if (ride.kind != save::model::RideKind::Tracked || scene.train_limits.contains(index))
            return;
        // The trains its kind's file offers, the first of them when it has none.
        std::vector<std::string>& choices = scene.train_choices[index];
        const std::string_view kind = std::string_view(ride.base.symbol).substr(0, ride.base.symbol.find(':'));
        if (const auto file = choices.empty() ? install.find(std::format("{}.common.ovl", ride.base.overlay_path)) : std::nullopt)
        {
            const auto overlay = ovl::Overlay::load(*file);
            for (const auto part : overlay ? std::array { core::maybe(&overlay->common()), overlay->unique() } : std::array<std::optional<const ovl::Part&>, 2> { })
                for (const ovl::TrackedRideItem& t : part ? ovl::read_tracked_rides(*overlay, *part) : std::vector<ovl::TrackedRideItem> { })
                    if (strings::equals_ignoring_case(t.name, kind) && t.ride && t.ride->operating)
                        choices.assign(t.ride->operating->trains.begin(), t.ride->operating->trains.end());
        }
        const std::string_view symbol = ride.trains.empty() ? std::string_view { } : std::string_view(ride.trains.front().symbol);
        const std::string_view train = ride.trains.empty() ? (choices.empty() ? std::string_view { } : std::string_view(choices.front())) : symbol.substr(0, symbol.find(':'));
        scene.train_limits[index] = train_limits_of(install, train);
    }

    void stand_trains(park::ParkScene& scene, std::size_t index)
    {
        save::model::Ride& ride = scene.rides.rides[index];
        const auto& course = index < scene.courses.size() ? scene.courses[index] : std::nullopt;
        const auto fits = scene.car_fits.find(index);
        const auto coaster = course ? simulation::coaster_of(*course, ride) : std::nullopt;
        // Boats wait along their dock back from where they set off, at its end.
        const bool boats = course && (ride.mode == save::model::OperatingMode::Boat || ride.mode == save::model::OperatingMode::WaterTransport);
        if ((!coaster && !boats) || fits == scene.car_fits.end() || ride.trains.empty())
            return;
        if (!coaster)
        {
            float front = course->length();
            for (save::model::Train& train : ride.trains)
            {
                rct3::park::track::stand_train(*course, train, fits->second, std::max(front, 0.0f));
                front -= train.length + rct3::park::track::kTrainGap;
            }
            return;
        }
        // A pool slide's track has an open end: its first car waits at the station, and the rest are out of sight
        // past that end, as if gone off it, and come back to the station one at a time.
        if (coaster->splash_out)
        {
            for (std::size_t t = 0; t < ride.trains.size(); ++t)
                rct3::park::track::stand_train(*course, ride.trains[t], fits->second, t == 0 ? coaster->start : course->length());
            return;
        }
        // Spread over its stations; those with no room are left out, as the game does.
        const std::vector<float> fronts = rct3::park::track::train_fronts(*course, coaster->start, ride.trains.front().length, static_cast<int>(ride.trains.size()), ride.options.block_sectioned);
        const auto kept = static_cast<std::int32_t>(fronts.size());
        ride.trains.resize(fronts.size());
        std::erase_if(ride.seats, [&](const auto& seat) { return seat.train >= kept; });
        ride.train_count = kept;
        for (std::size_t t = 0; t < fronts.size(); ++t)
            rct3::park::track::stand_train(*course, ride.trains[t], fits->second, fronts[t]);
    }

    rct3::park::path::SolidTiles boat_obstacles(const park::ParkScene& scene)
    {
        rct3::park::path::SolidTiles solids = rct3::park::path::solid_tiles(scene.objects, scene.models, scene.grid);
        rct3::park::path::add_path_decks(solids, scene.footpaths.paths, scene.grid);
        return solids;
    }

    void ready_boats(park::ParkScene& scene, std::size_t index)
    {
        const entt::entity e = index < scene.ride_entities.size() ? scene.ride_entities[index] : entt::null;
        if (e == entt::null || index >= scene.courses.size())
            return;
        save::model::RideModel one;
        one.rides.push_back(scene.rides.rides[index]);
        simulation::import_boats(scene.sim, one, std::span(&e, 1), std::span(&scene.courses[index], 1), scene.grid, scene.footpaths.water.bodies, boat_obstacles(scene));
        auto* boats = scene.sim.registry.try_get<simulation::Boats>(e);
        for (std::size_t k = 0; boats && k < scene.cars.size(); ++k)
            if (const auto model = scene.car_spots[k].ride == index ? scene.models.find(rct3::park::object::model_ref(scene.cars[k])) : scene.models.end(); model != scene.models.end() && model->second.boat_handling.turn_most > 0)
                boats->handling = model->second.boat_handling;
    }

    bool set_trains(render::Device& device, park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index, int trains, int cars)
    {
        save::model::Ride& ride = scene.rides.rides[index];
        const entt::entity e = index < scene.ride_entities.size() ? scene.ride_entities[index] : entt::null;
        auto& r = scene.sim.registry;
        const auto* fleet = e != entt::null ? r.try_get<simulation::Trains>(e) : nullptr;
        if (fleet && std::ranges::any_of(fleet->riders, [](const auto& riders) { return !riders.empty(); }))
            return false;
        if (const auto* cycle = e != entt::null && r.all_of<simulation::Boats>(e) ? r.try_get<simulation::CycleRide>(e) : nullptr; cycle && !cycle->riders.empty())
            return false;
        ready_trains(scene, install, index);
        // One with none, as the game leaves a closed ride, gets the first its kind offers; counts never set are
        // its kind's usual.
        if (ride.trains.empty())
        {
            const std::vector<std::string>& choices = scene.train_choices[index];
            if (choices.empty() || !use_train(scene, install, index, choices.front()))
                return false;
        }
        if (cars < 1)
            cars = scene.train_limits[index].usual_cars;
        scene.building.arm = fit_trains(scene, install, index, std::max(trains, 1), cars) || scene.building.arm;
        stand_trains(scene, index);
        // The park runs them from where they stand, its riders shared among them.
        const auto& course = index < scene.courses.size() ? scene.courses[index] : std::nullopt;
        if (auto coaster = fleet && !fleet->trains.empty() && course ? simulation::coaster_of(*course, ride) : std::nullopt)
        {
            coaster->start = fleet->trains.front().start;
            coaster->at = coaster->start;
            coaster->stops = fleet->trains.front().stops;
            r.replace<simulation::Trains>(e, simulation::trains_of(*coaster, ride));
            r.get<simulation::CycleRide>(e).capacity = static_cast<std::uint32_t>(ride.seats.size() / std::max<std::size_t>(ride.trains.size(), 1));
        }
        park::replace_ride_cars(device, scene, install, index);
        // Boats all go back to the dock, as many as the ride now has.
        if (e != entt::null && r.all_of<simulation::Boats>(e))
        {
            r.get<simulation::CycleRide>(e).capacity = static_cast<std::uint32_t>(ride.seats.size());
            ready_boats(scene, index);
        }
        return true;
    }

} // namespace rct3::scene::track
