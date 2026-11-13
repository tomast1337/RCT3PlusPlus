// SPDX-License-Identifier: GPL-3.0-or-later
//
// A tracked ride's trains: the kinds its install offers, fitting them to the track's stations, standing them at
// the station and handing them to the park to run; boats for the rides that sail.
#pragma once

#include "config/game/install.hpp"
#include "park/path/clearance.hpp"
#include "render/device.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace rct3::scene::park
{
    struct ParkScene;
} // namespace rct3::scene::park

namespace rct3::scene::track
{

    /// Gives the tracked ride `index` of the scene's rides `trains` trains of `cars` cars, of the kind its first
    /// train already is, with their seats and (in car_fits) how each car of a train sits on the track. On a course
    /// with a station the cars are as many as its shortest station takes, and the trains as many as the course
    /// takes (rct3::park::track::trains_fitting); a shuttle runs one. Whether its riders sit on arms.
    bool fit_trains(park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index, int trains, int cars);

    /// Reads, the first time it is asked of the tracked ride `index`, the trains its kind offers and the car
    /// counts its train allows (the scene's train_choices and train_limits).
    void ready_trains(park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index);

    /// Stands the trains of the tracked ride `index` at its station: the first's front where trains set off, each
    /// other queued behind the one before; boats back along their dock from its end. Nothing for a ride whose cars
    /// are not fitted (car_fits) or whose course runs no train.
    void stand_trains(park::ParkScene& scene, std::size_t index);

    /// Gives the boat ride `index`, once the park runs it, a boat for each of its trains, all at its dock, sailing
    /// on the park's water as it is now and handling as its boats' kind does.
    void ready_boats(park::ParkScene& scene, std::size_t index);

    /// What boats cannot sail through or under: what stands in the park's water or hangs low over it, paths' decks
    /// among them.
    [[nodiscard]] rct3::park::path::SolidTiles boat_obstacles(const park::ParkScene& scene);

    /// Gives the tracked ride `index` new trains as fit_trains does (its kind's first when it has none), stood at
    /// its station, and has the park run them and draw their cars. False, and nothing changed, while riders are
    /// aboard or when no train can be found.
    bool set_trains(render::Device& device, park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index, int trains, int cars);

    /// Gives the tracked ride `index` one train of the kind named `train_name`, as many cars long as that kind
    /// usually is, and sets the ride's limits to the kind's. Whether its riders sit on arms; nothing when the
    /// install has no such train.
    std::optional<bool> use_train(park::ParkScene& scene, const config::game::GameInstall& install, std::size_t index, std::string_view train_name);

    /// The file of the train named `train_name` among the install's cars.
    std::optional<std::filesystem::path> train_file(const config::game::GameInstall& install, std::string_view train_name);

} // namespace rct3::scene::track
