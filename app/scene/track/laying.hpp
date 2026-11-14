// SPDX-License-Identifier: GPL-3.0-or-later
//
// Laying a tracked ride's track (scene/track/building.hpp): the pieces of its overlay, what fits the open end and
// the ghost of the piece picked, and building, completing and demolishing pieces, with the platforms beside its
// stations and the running cost each piece adds. The construction panel and the clicks on the park act through
// these.
#pragma once

#include "park/path/clearance.hpp"
#include "park/ride/placement.hpp"
#include "park/track/pieces.hpp"
#include "park/track/split.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/track/building.hpp"
#include "simulation/park/finance.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace rct3::scene::track
{

    /// The role track files give holding brakes (ovl::TrackSection::role).
    inline constexpr std::uint32_t kHoldingRole = 24;

    const rct3::park::track::PieceCatalogue* catalogue(const TrackBuilding& b);

    /// The pieces of the track overlay `overlay`, read once.
    std::optional<const rct3::park::track::PieceCatalogue&> load_catalogue(TrackBuilding& b, const std::string& overlay, const ui::InterfaceAssets& assets);

    /// Of the scene's objects by id.
    std::unordered_map<std::uint64_t, std::size_t> object_indices(const park::ParkScene& scene);

    /// Whether the ride's track waits for the first piece of a stretch: it has none yet, or a new one was begun.
    bool opening(const save::model::Ride& ride);

    /// The open end of the ride's track: past its last piece, unless the track closes on itself.
    std::optional<rct3::park::track::TrackTip> track_tip(const park::ParkScene& scene, const TrackBuilding& b);

    /// The colours the next piece is built in: as painted, else as the last piece, else as a new track starts.
    save::model::RideColours next_colours(const park::ParkScene& scene, const TrackBuilding& b);

    /// Whether `shape` stands beside a platform.
    bool has_platform(const rct3::park::track::PieceShape& shape);

    /// What the ride's station pieces take their platforms from: its platform type's file as a park names it,
    /// the platform pieces in it, and its doors. Nothing when it has no platform type, or its file is missing.
    struct Platforms
    {
        std::string overlay;
        std::span<const std::string> names;
        std::vector<rct3::park::ride::DoorSpot> doors;
    };

    std::optional<Platforms> ride_platforms(TrackBuilding& b, const save::model::Ride& ride, const ui::InterfaceAssets& assets);

    /// Where a station piece of `shape` stands, from which of its neighbours are stations and whether it has any.
    rct3::park::ride::PlatformPlace platform_place(const rct3::park::track::PieceShape& shape, bool station_before, bool station_after, bool track_before, bool track_after);

    /// The catalogue's piece a track piece is, laid as it is.
    std::optional<rct3::park::track::LaidPiece> laid_piece(const rct3::park::track::PieceCatalogue& c, const save::model::TrackPiece& piece);

    /// Hides the platform built beside a station piece, which goes with it.
    void hide_platform(park::ParkScene& scene, const save::model::TrackPiece& piece);

    /// Builds on in `state`, at the open end of its track: the last stretch of that track still open, made the
    /// track's last.
    void use_split(park::ParkScene& scene, TrackBuilding& b, rct3::park::track::SplitState state);

    /// Of the left and the right track of a ride of two, whether it is closed: none of its stretches is open.
    std::array<bool, 2> split_closed(const park::ParkScene& scene, const TrackBuilding& b);

    /// A design's door `o`, moved and turned as the design's first piece `from` is to stand as `to`.
    save::model::PlacedObject moved_door(save::model::PlacedObject o, const save::model::TrackPiece& from, const save::model::TrackPiece& to);

    /// Where piece `i` of `design` goes when the design is moved and turned as its first piece is to stand as
    /// `to`, as it was saved rather than on after the piece before: some pieces' ends do not quite meet their
    /// neighbours' as their shapes are read. Nothing when the catalogue has no such piece.
    std::optional<rct3::park::track::PiecePlacement> design_piece(const rct3::park::track::PieceCatalogue& c, std::span<const save::model::TrackPiece> design, std::size_t i, const save::model::PlacedObject& to, const save::model::TerrainGrid& grid);

    /// Whether `o`, its model loaded, may not stand: it would take room `taken` or a path takes, stand off
    /// land the park owns, or cut ground that may not be cut.
    bool clashes(const park::ParkScene& scene, const rct3::park::path::SolidTiles& taken, const save::model::PlacedObject& o);

    /// Recomputes what fits the open end, the piece picked and its ghost.
    void refresh(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets);

    /// After a piece is built or taken away: swaps the pieces the ride's patterns match for the pieces they
    /// become, each laid where the one it replaces started, and measures the track's course again.
    void track_changed(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets);

    /// Adds the ghost to the park and to the ride's track, paid for as `charge` says. Over a piece picked
    /// mid-track, the ghost takes its place, unless it is the same piece, which is left as it stands; either way
    /// the next piece along is picked.
    void build(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, simulation::park::Charge charge = simulation::park::Charge::IfAffordable);

    /// Shows `way` as the ghost, the pieces it lays.
    void show_way(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets, std::span<const rct3::park::track::LaidPlacement> way);

    /// A click on auto-complete: starts looking for a way round to the station, stops looking while it looks,
    /// and lays the way found, paid for if the park has the money.
    void complete(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets);

    /// Takes the last piece laid out of the park and off the ride's track, which opens a closed circuit. A piece
    /// picked mid-track is taken out instead, leaving a gap that building goes on into from the piece before
    /// it, the same piece picked.
    void demolish(park::ParkScene& scene, TrackBuilding& b, const ui::InterfaceAssets& assets);

} // namespace rct3::scene::track
