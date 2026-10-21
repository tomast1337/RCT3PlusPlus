// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building a tracked ride's track in the park, piece by piece, with the construction panel. The panel offers
// the pieces of the ride's track overlay that fit the open end of its track; the picked one shows there as
// a see-through ghost, and building it adds it to the park's objects and to the ride's track in memory.
// Demolish takes the last piece away, which opens a closed circuit. After either, the ride's piece patterns
// swap the pieces they match (a station run's ends for its end pieces). A ride may not be edited while it is
// open. Nothing is paid and no supports are built under new pieces yet.
//
// The scene holds one TrackBuilding; it is idle until a ride is opened in it.
#pragma once

#include "park/object_mesh.hpp"
#include "park/object_models.hpp"
#include "park/track_choice.hpp"
#include "park/track_pieces.hpp"
#include "park/track_rules.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
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
        park::TrackRules rules;          ///< Of the ride's kind
        std::map<std::string, park::PieceCatalogue, std::less<>> catalogues;
        std::map<std::string, TrackModels, std::less<>> models;
        std::optional<park::TrackTip> tip; ///< None while the track is a closed circuit
        std::vector<park::LaidPiece> candidates;
        park::TrackChoice choice;
        std::optional<park::LaidPiece> picked;
        std::optional<park::PiecePlacement> ghost;
        park::ObjectMesh ghost_mesh;
    };

    /// Opens the construction panel on the tracked ride `ride` of the scene's rides, its pieces coming from
    /// the track overlay `overlay` (as a park names it). False when the ride has no track or the overlay
    /// cannot be read. The selection windows open new rides through this.
    bool open_track_building(ParkScene& scene, std::size_t ride, std::string overlay, const ui::InterfaceAssets& assets);

    /// Closes the construction panel and leaves the building idle.
    void close_track_building(ParkScene& scene);

    /// The track overlay of the ride's built pieces, if it has any.
    [[nodiscard]] std::optional<std::string> ride_track_overlay(const ParkScene& scene, std::size_t ride);

    /// Handles the panel's events. Call once per update after the interface.
    void update_track_building(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, int currency);

    /// Draws the ghost of the picked piece with `transform` and `light`.
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
