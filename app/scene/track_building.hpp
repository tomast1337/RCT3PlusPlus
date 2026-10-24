// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building a tracked ride's track in the park, piece by piece, with the construction panel. The panel offers
// the pieces of the ride's track overlay that fit the open end of its track; the picked one shows there as
// a see-through ghost, and building it adds it to the park's objects and to the ride's track in memory.
// Demolish takes the last piece away, which opens a closed circuit. After either, the ride's piece patterns
// swap the pieces they match (a station run's ends for its end pieces). A ride may not be edited while it is
// open. Nothing is paid and no supports are built under new pieces yet.
//
// A new ride starts with no pieces: its first piece, one its kind may start a track with, follows the
// pointer over the ground until it is built.
//
// The scene holds one TrackBuilding; it is idle until a ride is opened in it.
#pragma once

#include "config/game_install.hpp"
#include "park/build_catalogue.hpp"
#include "park/moving_meshes.hpp"
#include "park/object_mesh.hpp"
#include "park/object_models.hpp"
#include "park/ride_cars.hpp"
#include "park/track_choice.hpp"
#include "park/track_pieces.hpp"
#include "render/device.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/coaster.hpp"
#include "ui/key_map.hpp"
#include "ui/screens/attraction_info.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace rct3::park
{
    class AssetIndex;
} // namespace rct3::park

namespace rct3::ui
{
    struct InterfaceAssets;
} // namespace rct3::ui

namespace rct3::scene
{

    struct ParkScene;

    /// The models of one track overlay's pieces, loaded as they are first shown.
    struct TrackModels
    {
        std::unique_ptr<park::AssetIndex> index;
        std::unique_ptr<park::ModelLoader> loader;
        std::set<std::string, std::less<>> missing;      ///< Symbols looked for and not found, so not looked for again
        std::set<std::string, std::less<>> folders_read; ///< Folders every overlay below which was added

        TrackModels();
        ~TrackModels();
        TrackModels(TrackModels&&) noexcept;
        TrackModels& operator=(TrackModels&&) noexcept;
    };

    struct TrackBuilding
    {
        std::optional<std::size_t> ride; ///< Of the scene's rides; none while idle
        std::string overlay;             ///< The ride's track overlay, as its objects name it
        std::map<std::string, park::PieceCatalogue, std::less<>> catalogues;
        std::map<std::string, TrackModels, std::less<>> models;
        std::optional<park::TrackTip> tip; ///< None while the track is a closed circuit
        /// The track is built on before its first piece, as a giant slide is from its bottom up to its top.
        bool backward = false;
        /// A track with no pieces yet starts on the tile the pointer was last over, turned `turns` quarters.
        std::optional<save::Tile> aim;
        std::int32_t turns = 0;
        std::vector<park::LaidPiece> candidates;
        park::TrackChoice choice;
        std::optional<park::LaidPiece> picked;
        std::optional<park::PiecePlacement> ghost;
        park::ObjectMesh ghost_mesh;
        bool off_water = false; ///< The ghost's piece stands only on water, and some of it is on dry land
        std::string complaint;  ///< String key: why the track was last not finished; cleared by the next action
        bool arm = false;       ///< The ride's riders sit on arms, whose angles its pieces hold
        /// The piece whose arm angles the arm strip shows, by id; it shows them again when this changes.
        std::uint64_t arm_piece = 0;
        std::size_t arm_back = 0; ///< The piece the arm strip sets, counted back from the last
        /// Saved designs of the ride's kind, offered while its track has no pieces.
        std::vector<std::filesystem::path> designs;
        /// The pieces of the design picked, in track order, laid whole by the first click; empty for a track
        /// built piece by piece.
        std::vector<save::TrackPiece> design;
        /// A door the design places with its track: of which station, whether the exit, and where it stood.
        struct DesignDoor
        {
            std::size_t station = 0;
            bool exit = false;
            save::PlacedObject object;
        };
        std::vector<DesignDoor> design_doors;
        /// The design's ride, whose settings and colours the ride takes once the design is laid whole.
        std::optional<save::Ride> design_ride;
        /// What the round panel's tabs were last shown for: whether the first piece was still to lay, the
        /// ride's state, and whether a test could run.
        std::optional<std::tuple<bool, save::AttractionState, bool>> tabs_for;
        std::string preview_item; ///< The piece shown on the round panel's disc
        /// A see-through copy of the ride's first train, run round the circuit on the clock's time rather than
        /// the park's, so it runs while the park is paused.
        struct GhostTrain
        {
            simulation::Coaster train;
            std::vector<park::CarSpot> spots;
            std::vector<glm::mat4> stood;    ///< Where each car stood, by car
            float stood_at = 0;              ///< Where the train's front stood
            park::MovingMeshes meshes;       ///< The cars, then the joints of those on arms
            std::vector<std::size_t> joints; ///< The car each joint rides with, in the order of the meshes
        };
        std::optional<GhostTrain> test; ///< While a test runs
    };

    /// Gives the tracked ride `index` of the scene's rides `trains` trains of `cars` cars, of the kind its first
    /// train already is, with their seats and (in car_fits) how each car of a train sits on the track. Whether
    /// its riders sit on arms.
    bool fit_trains(ParkScene& scene, const config::GameInstall& install, std::size_t index, int trains, int cars);

    /// Gives the tracked ride `index` one train of the kind named `train_name`, as many cars long as that kind
    /// usually is, and sets the ride's limits to the kind's. Whether its riders sit on arms; nothing when the
    /// install has no such train.
    std::optional<bool> use_train(ParkScene& scene, const config::GameInstall& install, std::size_t index, std::string_view train_name);

    /// Adds a new tracked ride of `item` (a ride built piece by piece) to the park, closed and without pieces, and
    /// opens the construction panel on it. False when its pieces cannot be found.
    bool new_tracked_ride(ParkScene& scene, const park::BuildItem& item, const ui::InterfaceAssets& assets);

    /// Opens the construction panel on the tracked ride `ride` of the scene's rides, its pieces coming from
    /// the track overlay `overlay` (as a park names it). False when the ride has no track or the overlay
    /// cannot be read. The selection windows open new rides through this.
    bool open_track_building(ParkScene& scene, std::size_t ride, std::string overlay, const ui::InterfaceAssets& assets);

    /// Gives each station piece of the tracked ride `ride` the platform piece its type, its place in the
    /// station and the doors beside it call for (park::platform_symbol), adding those it lacks. Nothing for a
    /// ride without a platform type.
    void update_platforms(ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets);

    /// A click on the park while the ride being built has no pieces yet: lays its first piece where the ghost
    /// shows it. False, doing nothing, at other times.
    bool track_building_click(ParkScene& scene, const ui::InterfaceAssets& assets);

    /// Closes the construction panel and leaves the building idle.
    void close_track_building(ParkScene& scene);

    /// The track overlay of the ride's built pieces, if it has any.
    [[nodiscard]] std::optional<std::string> ride_track_overlay(const ParkScene& scene, std::size_t ride);

    /// The station pieces of the ride's track, which its entrance and exit stand beside.
    [[nodiscard]] std::vector<save::TrackPiece> station_pieces(ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets);

    /// Handles the panel's events, and for a track with no pieces the ground tile `pointed` at (none while the
    /// pointer is off the ground) and the turn commands. Call once per update after the interface.
    /// A test train runs `elapsed` seconds on.
    void update_track_building(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, int currency, ui::DistanceUnits units, std::optional<save::Tile> pointed, std::span<const ui::PlayCommand> commands, float elapsed);

    /// Draws the ghost of the picked piece, and any test train, with `transform` and `light`.
    void draw_track_ghost(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    // Shared with path building, which places objects the same way.

    /// Makes sure the model of `object` is loaded, reading it through its overlay's own index kept in `cache`.
    bool load_object_model(ParkScene& scene, std::map<std::string, TrackModels, std::less<>>& cache, const ui::InterfaceAssets& assets, const save::PlacedObject& object);

    /// Hands the scene's objects to the mesh builder again after a change.
    void objects_changed(ParkScene& scene);

    /// Waits for a mesh build that is reading the objects, before they change.
    void wait_for_build(ParkScene& scene);

    /// Paints and uploads the pictures of `ghost` the scene does not have yet.
    void ghost_textures(render::Device& device, ParkScene& scene, const park::ObjectMesh& ghost);

    /// Ghosts' tints: bluish where the piece can be built, red where it cannot.
    inline constexpr glm::vec4 kGhostTint { 0.45f, 0.75f, 1.0f, 0.7f };
    inline constexpr glm::vec4 kRefusedTint { 1.0f, 0.2f, 0.15f, 0.7f };

    /// `light` as ghosts are drawn in: lit evenly from every side, brighter than daylight, so they stand out at
    /// any hour and the Bloom option glows round them.
    [[nodiscard]] render::SceneLight ghost_light(render::SceneLight light);
    /// Draws `mesh` see-through with `transform` and `light`; `solid`, as a built object is drawn instead, for a
    /// picture of it alone.
    void draw_ghost_mesh(render::Device& device, const ParkScene& scene, const park::ObjectMesh& mesh, const glm::mat4& transform, const render::SceneLight& light, bool solid = false);

} // namespace rct3::scene
