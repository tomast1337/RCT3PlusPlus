// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building a tracked ride's track in the park, piece by piece, with the construction panel. The panel offers
// the pieces of the ride's track overlay that fit the open end of its track; the picked one shows there as
// a see-through ghost, and building it adds it to the park's objects and to the ride's track in memory.
// Demolish takes the last piece away, which opens a closed circuit. A piece clicked between two others is
// built over instead: the panel offers the pieces that fit both, building one swaps it in (the same piece is
// left standing) and picks the next piece along, and demolish takes it out, leaving a gap the track is built
// on into from the piece before. After any of these, the ride's piece patterns swap the pieces they match (a
// station run's ends for its end pieces). A ride may not be edited while it is open. No supports are built
// under new pieces yet.
//
// A new ride starts with no pieces: its first piece, one its kind may start a track with, follows the
// pointer over the ground until it is built. A new ride picked from a build list whose kind has saved designs
// first opens their window (ui/screens/build/designs.hpp): building a design lays it whole with the train
// picked there, a custom track is built piece by piece, and cancelling closes the building as closing its
// panel does. A design in hand has a round panel of its own, as a flat ride being placed does: turning it with
// its price, its name and tune with why it may not stand where it is, its colours, and whether the ground it
// passes into is cut smoothly, sharply or not at all. Once laid its window opens and its doors are placed.
//
// The scene holds one TrackBuilding; it is idle until a ride is opened in it.
#pragma once

#include "config/game/install.hpp"
#include "park/build_catalogue.hpp"
#include "park/design_placement.hpp"
#include "park/moving_meshes.hpp"
#include "park/object/mesh.hpp"
#include "park/object/models.hpp"
#include "park/path/clearance.hpp"
#include "park/ride/cars.hpp"
#include "park/track/choice.hpp"
#include "park/track/completion.hpp"
#include "park/track/pieces.hpp"
#include "park/track/split.hpp"
#include "render/device.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/build_menus.hpp"
#include "scene/objects.hpp"
#include "simulation/coaster.hpp"
#include "ui/key_map.hpp"
#include "ui/screens/info/attraction.hpp"
#include "units/units.hpp"

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

namespace rct3::scene::park
{
    struct ParkScene;
} // namespace rct3::scene::park

namespace rct3::scene::track
{

    struct TrackBuilding
    {
        std::optional<std::size_t> ride; ///< Of the scene's rides; none while idle
        std::string overlay;             ///< The ride's track overlay, as its objects name it
        std::map<std::string, rct3::park::track::PieceCatalogue, std::less<>> catalogues;
        std::map<std::string, TrackModels, std::less<>> models;
        /// The platform pieces of each platform type's file, by the file as a park names it, read once.
        std::map<std::string, std::vector<std::string>, std::less<>> platform_names;
        std::optional<rct3::park::track::TrackTip> tip; ///< None while the track is a closed circuit
        /// The track is built on before its first piece, as a giant slide is from its bottom up to its top.
        bool backward = false;
        bool heights = false; ///< The track's heights are shown while it is built
        /// Settings the next piece is built with, where it has them.
        int holding_time = 5; ///< Seconds
        int car_turn = 0;     ///< Degrees
        int brake_speed = 0;  ///< Whole m/s for brakes and block brakes; 0 until set, so they take the ride's
        std::uint8_t facing = 0;
        std::optional<save::model::RideColours> colours; ///< Painted in the ring's colour tab; else as the piece before
        /// A track with no pieces yet starts on the tile the pointer was last over, turned `turn`.
        std::optional<save::model::Tile> aim;
        rct3::park::ModelTurn turn;
        /// The height Shift raised the first piece to, which it keeps once Shift is let go; never under the ground.
        std::optional<std::int32_t> raised;
        /// While Shift is held over the first piece: where the pointer met the ground when it went down, and the
        /// piece's height then. The piece stays on its tile meanwhile.
        struct Lift
        {
            glm::vec3 at { 0.0f };
            std::int32_t height = 0;
        };
        std::optional<Lift> lift;
        std::vector<rct3::park::track::LaidPiece> candidates;
        rct3::park::track::TrackChoice choice;
        bool station_first = false; ///< The choice is still to be set to the station of a track being begun
        std::optional<rct3::park::track::LaidPiece> picked;
        std::optional<rct3::park::track::PiecePlacement> ghost;
        /// A piece between two others clicked in the park, by place in the ride's track: the panel offers what
        /// may stand in its place, and building steps on along the track to the piece after it, or before it
        /// when `selected_back`. None while building at the track's open end.
        std::optional<std::size_t> selected;
        bool selected_back = false;
        /// On a ride of two tracks side by side, which open end is built on and with which pieces.
        rct3::park::track::SplitState split = rct3::park::track::SplitState::None;
        rct3::park::object::ObjectMesh ghost_mesh;
        bool off_water = false;      ///< The ghost's piece stands only on water, and some of it is on dry land
        bool clashes = false;        ///< The ghost's piece (or any of a design) takes room a solid item or a path takes, or is off owned land
        bool unowned = false;        ///< Of those, it stands off land the park owns
        bool in_ground = false;      ///< Of those, it cuts into the ground
        std::size_t ghost_doors = 0; ///< The design's doors the ghost shows with its track
        std::string ghost_platform;  ///< The platform piece the ghost shows beside its station piece, if any
        std::string complaint;       ///< String key: why the track was last not finished; cleared by the next action
        bool arm = false;            ///< The ride's riders sit on arms, whose angles its pieces hold
        /// The piece whose arm angles the arm strip shows, by id; it shows them again when this changes.
        std::uint64_t arm_piece = 0;
        std::size_t arm_back = 0; ///< The piece the arm strip sets, counted back from the last
        /// Saved designs of the ride's kind, offered in their window before its track has pieces.
        std::vector<DesignFile> designs;
        bool offer_designs = false; ///< Their window is to open
        bool browsing = false;      ///< Their window is open; closing it closes the building
        /// The picture of the design their window shows, the train of its kind it shows (by place in the ride's
        /// train choices), and the design (by place in designs).
        std::optional<render::TextureHandle> design_picture;
        std::size_t design_train = 0;
        std::optional<std::size_t> design_shown;
        std::vector<bool> designs_locked; ///< By place in designs: waiting for a career scenario
        /// The pieces of the design picked, in track order, laid whole by the first click; empty for a track
        /// built piece by piece.
        std::vector<save::model::TrackPiece> design;
        /// A door the design places with its track: of which station, whether the exit, and where it stood.
        struct DesignDoor
        {
            std::size_t station = 0;
            bool exit = false;
            save::model::PlacedObject object;
        };
        std::vector<DesignDoor> design_doors;
        /// The design's ride, whose settings and colours the ride takes once the design is laid whole.
        std::optional<save::model::Ride> design_ride;
        /// While a design is in hand: why its ghost may not stand, if it may not; the name and tune (by place in
        /// the scene's tunes, from 1; 0 for none) the ride takes once it is laid; and whether the ground it passes
        /// into is cut (as smooth_cuts says) rather than refusing it. Its own round panel shows them.
        rct3::park::DesignRefusal refusal = rct3::park::DesignRefusal::None;
        std::vector<rct3::park::pools::PoolOpening> pool_openings; ///< Where the design's ghost meets a pool beside it
        std::u16string design_title;
        std::size_t design_tune = 0;
        bool cut_ground = false;
        /// What the design's panel was last shown for: the ground choice and the colours.
        std::optional<std::tuple<bool, bool, save::model::RideColours>> design_tabs_for;
        /// What the round panel's tabs were last shown for: whether the first piece was still to lay, the
        /// ride's state, whether the construction strip was hidden, and whether cuts were smooth.
        std::optional<std::tuple<bool, save::model::AttractionState, bool, bool>> tabs_for;
        bool smooth_cuts = false; ///< Whether ground the track passes into is cut smoothly, not sharply
        std::string preview_item; ///< The piece shown on the round panel's disc
        /// Auto-complete: while it looks for a way, the way nearest the station so far shows as the ghost; the way
        /// it found shows until a second click on the button lays it, or any other action turns it down.
        std::optional<rct3::park::track::CompletionSearch> completing;
        std::vector<rct3::park::track::LaidPlacement> proposed;
        units::Seconds clock; ///< The panel has been open, which the button flashes by
        /// While the ride is testing and its track is not yet a circuit, a see-through copy of its first train
        /// runs from the station to the open end and crashes off it, its see-through wrecks flying, blowing up and
        /// burning as a crashed train's do, though they hurt no one; once they are gone it sets off again. It runs
        /// on the clock's time rather than the park's, so it runs while the park is paused.
        struct GhostTrain
        {
            simulation::Coaster train;
            std::vector<rct3::park::ride::CarSpot> spots;
            std::vector<glm::mat4> stood;                   ///< Where each car stood, by car
            float stood_at = 0;                             ///< Where the train's front stood
            units::Seconds waited;                          ///< It has stood still short of the open end
            std::vector<simulation::Wreck> wrecks;          ///< Its cars, once crashed, front to back
            random::RandomState dice = random::seeded({ }); ///< How its wrecks tumble
            rct3::park::MovingMeshes meshes;                ///< The cars, then the joints of those on arms
            std::vector<std::size_t> joints;                ///< The car each joint rides with, in the order of the meshes
        };
        std::optional<GhostTrain> test;
    };

    /// Adds a new tracked ride of `item` (a ride built piece by piece) to the park, closed and without pieces, and
    /// opens the construction panel on it. False when its pieces cannot be found.
    bool new_tracked_ride(park::ParkScene& scene, const rct3::park::BuildItem& item, const ui::InterfaceAssets& assets);

    /// Opens the construction panel on the tracked ride `ride` of the scene's rides, its pieces coming from
    /// the track overlay `overlay` (as a park names it). False when the ride has no track or the overlay
    /// cannot be read. The selection windows open new rides through this.
    bool open_track_building(park::ParkScene& scene, std::size_t ride, std::string overlay, const ui::InterfaceAssets& assets);

    /// Gives each station piece of the tracked ride `ride` the platform piece its type, its place in the
    /// station and the doors beside it call for (rct3::park::ride::platform_symbol), adding those it lacks. Nothing for a
    /// ride without a platform type.
    void update_platforms(park::ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets);

    /// A click on the park at `at`, in interface units on a screen of `size`, while a ride is built. With no
    /// pieces yet, lays its first piece where the ghost shows it. Otherwise a click on one of its pieces picks
    /// it to build over, stepping on toward the end of it nearer the click; one at the track's open end goes
    /// back to building there. False, doing nothing, at other times.
    bool track_building_click(park::ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size);

    /// Closes the construction panel and leaves the building idle.
    void close_track_building(park::ParkScene& scene);

    /// The track overlay of the ride's built pieces, if it has any.
    [[nodiscard]] std::optional<std::string> ride_track_overlay(const park::ParkScene& scene, std::size_t ride);

    /// What ride `ride`'s track pieces cost to lay, hundredths; 0 when its pieces cannot be read.
    [[nodiscard]] std::int64_t track_cost(park::ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets);

    /// The station pieces of the ride's track, which its entrance and exit stand beside.
    [[nodiscard]] std::vector<save::model::TrackPiece> station_pieces(park::ParkScene& scene, std::size_t ride, const ui::InterfaceAssets& assets);

    /// Handles the panel's events, and for a track with no pieces the pointer at `at` on a view of `size` (`over`
    /// the interface or not), the turn commands and `shift`, held to raise or sink the first piece. Call once per
    /// update after the interface, `elapsed` after the last.
    void update_track_building(render::Device& device, park::ParkScene& scene, const ui::InterfaceAssets& assets, int currency, ui::screens::info::DistanceUnits units, ui::Point at, ui::Point size, bool over, bool shift, std::span<const ui::PlayCommand> commands, units::Seconds elapsed);

    /// The arrows where the picked piece starts and ends (see park/build_arrows.hpp); empty while no piece is.
    [[nodiscard]] rct3::park::TerrainMesh track_arrows(const park::ParkScene& scene);

    /// Draws the ghost of the picked piece and its arrows, and any test train, with `transform` and `light`.
    void draw_track_ghost(render::Device& device, const park::ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

} // namespace rct3::scene::track
