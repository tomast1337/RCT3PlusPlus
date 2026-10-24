// SPDX-License-Identifier: GPL-3.0-or-later
//
// Placing a ride whole (a flat ride) picked in the build lists: its see-through ghost follows the pointer
// over the ground, the turn keys turn it a quarter, and a click places it there if it fits (park/ride_placement.hpp),
// levelling the ground under it and paying its price. Then its entrance and its exit are placed in turn
// round its rim. A right click puts the tool away; a ride left without an entrance or exit stays built but
// takes no riders. The ride is fenced round its rim as it is placed, and the fence opens for each door as it
// goes in, beside it; the doors take the fence's colours. The ground under the ride is paved (park/ground_paths.hpp).
//
// A ride placed with both is run by the simulation from the first frame its entrance has a path behind it.
// A tracked ride's entrance or exit, asked for from its window, is placed the same way beside its station;
// with both, it runs on the track built for it as the flat rides do.
//
// Scenery picked in the lists is placed the same way, one item a click until the tool is put away: centred
// on the tile pointed at, in the quarter of it pointed at for quarter-tile items, on the ground or on top of
// the thing pointed at, which is how roofs go onto walls. While Shift is held the item stays on its tile and
// rises and sinks with the pointer. An item may not take room another solid item or a path fills. A round
// panel in the bottom-left corner shows the item alone, slowly turning, with a tab that turns it and, for
// items that take colours, one that paints them; the colours start as the item's own each time it is picked.
// A ride placed whole gets the same panel, without the colours, and rises with Shift the same way; raised, it
// and its doors leave the ground under them as it is.
//
// A stall is placed as a ride placed whole is, but has no doors and no fence: it serves the tile it faces, and
// a click places one, puts the tool away and opens its window. It sells everything its kind offers, and the
// simulation takes it on once a path runs past the tile it faces.
#pragma once

#include "park/build_catalogue.hpp"
#include "park/camera.hpp"
#include "park/moving_meshes.hpp"
#include "park/object_mesh.hpp"
#include "park/path_clearance.hpp"
#include "park/ride_placement.hpp"
#include "render/device.hpp"
#include "scene/track_building.hpp"
#include "ui/key_map.hpp"
#include "ui/widget.hpp"

#include <cstddef>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::scene
{

    /// The placing panel's round view: a whole turn every ten seconds, looking down a little, near enough for
    /// small items to fill it.
    inline constexpr float kPreviewTurn = 2 * std::numbers::pi_v<float> / 10;
    inline constexpr float kPreviewPitch = 0.5f;
    inline constexpr float kPreviewClosest = 1.5f;

    struct ParkScene;

    struct RidePlacing
    {
        enum class Step : std::uint8_t
        {
            Ride,
            Entrance,
            Exit,
            Scenery,
        };
        std::optional<park::BuildItem> item; ///< Being placed; none while idle
        Step step = Step::Ride;
        std::int32_t turns = 0;
        std::optional<park::RideSite> site;     ///< Pointed at, then where the ride was placed
        std::optional<park::DoorSpot> door;     ///< The entrance or exit pointed at
        std::optional<park::DoorSpot> entrance; ///< Placed; of a tracked ride getting a door, its other door
        std::int32_t quadrant = 0;              ///< Of the tile, for scenery that takes a quarter of one
        std::int32_t ground = 0;                ///< Height of the ground under the site, of scenery or a ride
        bool absolute = false;                  ///< Scenery on a raised path, at the deck's height
        /// While Shift is held: where the pointer met the park when it went down, and the height then.
        struct Lift
        {
            glm::vec3 at { 0.0f };
            std::int32_t height = 0;
        };
        std::optional<Lift> lift;
        park::SiteProblem problem = park::SiteProblem::None;
        bool affordable = true;
        std::size_t ride = 0;             ///< Of the scene's rides, once placed
        std::vector<std::uint64_t> fence; ///< Of that ride, its fence pieces, built again as each door goes in
        park::ObjectMesh ghost_mesh;
        std::array<std::int32_t, 3> colours { }; ///< Of the scenery: its own when picked, then those the player paints
        std::string panel_item;                  ///< The scenery the placing panel is set up for
        park::ObjectMesh preview_mesh;           ///< The scenery alone, for the panel's round view
        /// Of scenery with bones, the same a piece per bone, moved as it moves; preview_bones gives each piece's bone.
        park::MovingMeshes preview_moving;
        std::vector<std::int32_t> preview_bones;
        park::ParkCamera preview_camera;
        render::TextureHandle preview_target;
        std::uint32_t preview_pixels = 0;
        std::map<std::string, TrackModels, std::less<>> models;
        park::SolidTiles solids;
        std::size_t solids_of = static_cast<std::size_t>(-1); ///< The scene's object count when worked out
        std::vector<std::size_t> unjoined;                    ///< Rides placed here that the simulation does not run yet
        std::vector<std::size_t> unjoined_stalls;             ///< The same of the stalls placed here
        /// Every show trick the game names, read from its main file when the first show ride is placed.
        std::optional<std::vector<std::string>> trick_names;
        /// Of a tracked ride getting a door: its station pieces, which the door stands beside
        std::vector<save::TrackPiece> stations;
    };

    /// The point of the ground under `at`, in a view of `size`, in tile units: column and row with fractions.
    [[nodiscard]] std::optional<glm::vec2> ground_spot(const ParkScene& scene, ui::Point at, ui::Point size);
    /// The highest id the park's objects, rides and stalls use; a new one takes the next.
    [[nodiscard]] std::uint64_t last_id(const ParkScene& scene);
    /// The tile of the ground under `at`, in a view of `size`.
    [[nodiscard]] std::optional<save::Tile> ground_tile(const ParkScene& scene, ui::Point at, ui::Point size);

    /// Starts placing `item`, in place of anything being placed before. Items other than rides placed
    /// whole, stalls and scenery are left alone; false for those.
    bool start_ride_placing(ParkScene& scene, const park::BuildItem& item);

    /// Starts placing the entrance, or the `exit`, of the tracked ride `ride` beside its `stations`, as `item`
    /// (the ride's) has it; one placed before moves. False without stations or a door in the item.
    bool start_door_placing(ParkScene& scene, std::size_t ride, const park::BuildItem& item, std::vector<save::TrackPiece> stations, bool exit);
    /// Adds `door`, whose model is loaded, as the entrance (or `exit`) of station `station` of the tracked ride
    /// `ride`, standing at `height`, in place of the one it had.
    void place_station_door(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t ride, std::size_t station, const save::PlacedObject& door, std::int32_t height, bool exit);

    /// Puts the tool away.
    void stop_ride_placing(ParkScene& scene);

    [[nodiscard]] bool ride_placing(const ParkScene& scene);

    /// Each frame: follows the pointer at `at` (of a view `size`, not `over` the interface), the game's
    /// `commands` and the placing panel, and has the simulation take on the rides placed here as their entrances
    /// join the paths. `shift`: whether Shift is held, which raises and lowers scenery.
    void update_ride_placing(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool over, bool shift, std::span<const ui::PlayCommand> commands);

    /// A click on the park while placing: places what the ghost shows. False when not placing.
    bool ride_placing_click(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets);

    void draw_ride_placing_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene
