// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a ride placed whole (a flat ride) may go, and what placing it does to the ground. The ride's footprint
// is centred on the tile under the pointer and stands at a whole-unit height: the ground's height there,
// rounded. It needs land the park owns outright, no path and nothing solid standing above its base. Placing
// it levels the ground under its whole footprint to that height; the tiles round it keep their far corners
// and slope down or up to it.
//
// Its entrance and its exit each stand on a tile across the rim of the footprint, at the ride's height,
// facing the ride: their back is where the queue or path joins. A tracked ride's stand beside a station piece,
// across one of the sides its track does not run through, at the piece's height and facing it.
#pragma once

#include "park/path_clearance.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rct3::park
{

    /// Where a ride placed whole stands.
    struct RideSite
    {
        std::int32_t col = 0; ///< Lowest column and row of the footprint, as the park stores placed objects
        std::int32_t row = 0;
        std::int32_t turns = 0;                    ///< Quarter turns, 0..3
        std::int32_t height = 0;                   ///< Whole units
        std::array<std::int32_t, 2> span { 1, 1 }; ///< Columns and rows covered, after the turn
    };

    /// The site of a ride of `squares` (along its own X and Z) turned `turns` quarter turns, centred on the
    /// tile `col`, `row`.
    [[nodiscard]] RideSite ride_site(save::Tile tile, std::array<std::int32_t, 2> squares, std::int32_t turns, const save::TerrainGrid& grid);

    enum class SiteProblem : std::uint8_t
    {
        None,
        NotOwned, ///< Off the map, or land the park does not own outright
        Path,
        Blocked,    ///< Something solid stands there above the base
        OffWater,   ///< A ride that stands only on water, on a tile that holds none
        UnderWater, ///< Any other ride, below the water on a tile
    };

    /// What keeps a ride from `site` for the water of `bodies`, if anything: one `water_only` needs water under
    /// every tile, others none over their base. Any depth of water will do: the parks shipped with the game hold
    /// their water shows in deep lakes, and nothing in their files asks for more.
    [[nodiscard]] SiteProblem water_problem(const RideSite& site, bool water_only, std::span<const save::WaterBody> bodies);

    /// What keeps a ride from `site`, if anything.
    [[nodiscard]] SiteProblem site_problem(const RideSite& site, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids);

    /// Levels the ground under `site` to its height.
    void level_ground(save::TerrainGrid& grid, const RideSite& site);

    /// The whole-unit height one step of the shape ground tool takes a tile to: a sloped tile is flattened to
    /// its highest corner going `up` (its lowest going down), a flat one moves one unit.
    [[nodiscard]] std::int32_t shaped_height(const save::TerrainCell& cell, bool up);

    /// A ride's entrance or exit: its tile and the quarter turn it faces the ride by.
    struct DoorSpot
    {
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::uint8_t direction = 0; ///< 0 faces -x, 1 +z, 2 +x, 3 -z
        std::int32_t height = 0;    ///< Whole units; set for a tracked ride's, a flat ride's stand at the ride's
        auto operator<=>(const DoorSpot&) const = default;
    };

    /// The tile across the rim of `site` nearest to the tile `col`, `row`, facing the ride. Never a corner
    /// tile, which touches the footprint only diagonally.
    [[nodiscard]] DoorSpot door_spot(const RideSite& site, save::Tile tile);

    /// The tile beside one of `stations` (a tracked ride's station pieces) nearest to `tile`, across a side its
    /// track does not run through, facing the piece; nothing without stations.
    [[nodiscard]] std::optional<DoorSpot> station_door_spot(std::span<const save::TrackPiece> stations, save::Tile tile);

    /// The platform piece (its "sid" name) a station piece of the platform type `type` takes: the type, then
    /// "start" where the station begins (the piece before it is no station), "end" where it ends, "startEnd"
    /// for both and "middle" for neither; then "1stairLeft" or "1stairRight" for a door beside it on the left or
    /// right of the way the track runs, "2stair" for doors on both sides.
    [[nodiscard]] std::string platform_symbol(std::string_view type, const save::TrackPiece& piece, bool station_before, bool station_after, std::span<const DoorSpot> doors);

    /// What keeps an entrance or exit from `door`, if anything. `taken`: a door already placed for the ride.
    [[nodiscard]] SiteProblem door_problem(const RideSite& site, const DoorSpot& door, std::optional<DoorSpot> taken, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const SolidTiles& solids);

} // namespace rct3::park
