// SPDX-License-Identifier: GPL-3.0-or-later
//
// Placing a ride whole (a flat ride) picked in the build lists: its see-through ghost follows the pointer
// over the ground, Z turns it a quarter, and a click places it there if it fits (park/ride_placement.hpp),
// levelling the ground under it and paying its price. Then its entrance and its exit are placed in turn
// round its rim. A right click puts the tool away; a ride left without an entrance or exit stays built but
// takes no riders.
//
// A ride placed with both is run by the simulation from the first frame its entrance has a path behind it.
#pragma once

#include "park/build_catalogue.hpp"
#include "park/object_mesh.hpp"
#include "park/path_clearance.hpp"
#include "park/ride_placement.hpp"
#include "render/device.hpp"
#include "scene/track_building.hpp"
#include "ui/keyboard.hpp"
#include "ui/widget.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    struct RidePlacing
    {
        enum class Step : std::uint8_t
        {
            Ride,
            Entrance,
            Exit,
        };
        std::optional<park::BuildItem> item; ///< Being placed; none while idle
        Step step = Step::Ride;
        std::int32_t turns = 0;
        std::optional<park::RideSite> site; ///< Pointed at, then where the ride was placed
        std::optional<park::DoorSpot> door; ///< The entrance or exit pointed at
        std::optional<park::DoorSpot> entrance;
        park::SiteProblem problem = park::SiteProblem::None;
        bool affordable = true;
        std::size_t ride = 0; ///< Of the scene's rides, once placed
        park::ObjectMesh ghost_mesh;
        std::map<std::string, TrackModels, std::less<>> models;
        park::SolidTiles solids;
        std::size_t solids_of = static_cast<std::size_t>(-1); ///< The scene's object count when worked out
        std::vector<std::size_t> unjoined;                    ///< Rides placed here that the simulation does not run yet
    };

    /// The point of the ground under `at`, in a view of `size`, in tile units: column and row with fractions.
    [[nodiscard]] std::optional<glm::vec2> ground_spot(const ParkScene& scene, ui::Point at, ui::Point size);
    /// The tile of the ground under `at`, in a view of `size`.
    [[nodiscard]] std::optional<park::PathSpot> ground_tile(const ParkScene& scene, ui::Point at, ui::Point size);

    /// Starts placing `item`, in place of anything being placed before. Items other than rides placed
    /// whole are left alone; false for those.
    bool start_ride_placing(ParkScene& scene, const park::BuildItem& item);

    /// Puts the tool away.
    void stop_ride_placing(ParkScene& scene);

    [[nodiscard]] bool ride_placing(const ParkScene& scene);

    /// Each frame: follows the pointer at `at` (of a view `size`, not `over` the interface) and the keys, and
    /// has the simulation take on the rides placed here as their entrances join the paths.
    void update_ride_placing(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, const ui::KeyboardState& keys);

    /// A click on the park while placing: places what the ghost shows. False when not placing.
    bool ride_placing_click(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    void draw_ride_placing_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene
