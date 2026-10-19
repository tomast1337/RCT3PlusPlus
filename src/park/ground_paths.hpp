// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paths laid on the ground. A path on the ground has no shape of its own: it is a picture laid over the
// terrain of its tile, following the tile's corner heights, so it bends with the land. Where the picture is
// see-through the grass below shows, which gives the path its edges.
//
// Each path style comes with two pictures, A and B, each split into a 2 by 2 grid of cells. A cell is a whole
// tile's worth of path drawn one way: open on every side, with an edge along one axis, a closed end, and so
// on. Every cell is symmetric, so any quarter of it shows that look at that quarter of a tile. A tile is
// split into quarters, and each quarter looks at the three tiles round its corner (the one beside it along
// x, the one beside it along z and the one across the corner) to choose the look it needs; it then shows
// the matching quarter of that cell. A tile whose four quarters all want the same cell is drawn whole.
//
// A raised or sloping path beside a tile counts as path there when one of its ends sits on the ground at
// the edge they share, so a ground path runs on into the foot of a slope. So does a queue on the ground
// that is open towards the tile.
//
// Pictures and cells (u across, v down):
//
//     A: [ edge on x, open corner | closed, open corner ]   B: [ edge on x     | closed  ]
//        [ open                   | edge on z, open corner]     [ open, notched | edge on z ]
//
// "Edge on x" is closed on the sides facing the tiles beside it along x and open along z. An "open
// corner" bends the edge out at the tile's corner to meet path across the corner; a "notched" cell is
// open on both sides with the corner cut away, for the inside of a turn.
//
// A flat ride standing on the ground is paved under its footprint and its doors as if path lay there: in the
// style of the ground path its exit lets out onto, or a plain grey style while there is none, so the paving
// joins the path.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// How one quarter of a ground path tile looks.
    enum class PathLook : std::uint8_t
    {
        Open,        ///< Path on both sides and across the corner
        Notched,     ///< Path on both sides, grass in the corner
        EdgeX,       ///< Grass along x; straight edge
        EdgeXJoined, ///< Grass along x; the edge bends out to path across the corner
        EdgeZ,       ///< Grass along z; straight edge
        EdgeZJoined, ///< Grass along z; the edge bends out to path across the corner
        Closed,      ///< Grass on both sides: a rounded end or outer corner
        CornerOnly,  ///< Grass on both sides but a scrap of path in the corner
    };

    /// Which picture (0 for A, 1 for B) and which cell of it (0 to 3, row by row) shows a look.
    struct PathCell
    {
        std::uint8_t sheet = 0;
        std::uint8_t cell = 0;
    };

    [[nodiscard]] PathCell path_cell(PathLook look) noexcept;

    /// The look of a tile quarter, given whether there is path beside it along x, along z and across the
    /// corner.
    [[nodiscard]] PathLook quarter_look(bool along_x, bool along_z, bool across) noexcept;

    /// The ground path styles of a park, and which one lies on each tile.
    struct GroundPathMap
    {
        int width = 0;
        int height = 0;
        std::vector<std::string> styles;                  ///< System names, in the order first met
        std::vector<std::int16_t> tiles;                  ///< Row by row: index into styles, or kNoPath
        std::map<std::size_t, std::vector<float>> raised; ///< By tile index: heights the raised paths there end at
        std::map<std::size_t, std::uint8_t> queues;       ///< By tile index: ground queue's open sides, bit per TileSide
    };

    inline constexpr std::int16_t kNoPath = -1;

    /// How much a raised path piece climbs over its tile, from its stored slope: none when flat or unknown.
    [[nodiscard]] float path_rise(std::uint8_t slope) noexcept;

    /// How close an end of a raised path must be to the ground to meet a ground path, in world units.
    inline constexpr float kPathMeets = 0.25f;

    /// The ground paths among `paths` on a grid of the terrain's size, and the ends of the raised ones. Paths
    /// off the grid and ground paths without a style are left out.
    [[nodiscard]] GroundPathMap ground_path_map(std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// The floor of a flat ride standing on the ground: its footprint, paved like the ground path that leads
    /// away from its exit, or in kRideFloorStyle while none does.
    struct RideFloor
    {
        int col0 = 0; ///< Footprint, both ends in
        int row0 = 0;
        int col1 = -1;
        int row1 = -1;
        std::optional<std::array<int, 2>> beyond_exit; ///< The tile the exit lets out onto
        std::vector<std::array<int, 2>> doors;         ///< The stations' entrance and exit tiles, paved alike
    };

    /// The plain grey path style floors take while their exit leads nowhere.
    inline constexpr std::string_view kRideFloorStyle = "Asphalt";

    /// The floors of the flat rides among `rides` whose footprint lies level at their height. `squares_of`
    /// gives the squares (along its own X and Z) of the object a ride is placed as, or nothing when unknown.
    [[nodiscard]] std::vector<RideFloor> ride_floors(std::span<const save::Ride> rides, const std::function<std::optional<std::array<std::int32_t, 2>>(std::uint64_t object)>& squares_of, const save::TerrainGrid& grid);

    /// Lays `floors` into `map` as ground paths, so they draw and join as paths do. Tiles that already hold a
    /// path keep it.
    void pave_ride_floors(GroundPathMap& map, std::span<const RideFloor> floors);

    /// True when there is a ground path at (col, row); false off the map.
    [[nodiscard]] bool has_ground_path(const GroundPathMap& map, int col, int row) noexcept;

    /// True when the path on tile (col, row) carries on onto the tile (dc, dr) beside it (one step along x
    /// or z): a ground path is there, a ground queue there is open towards this tile, or a raised path there
    /// ends level with this tile's ground at their shared edge.
    [[nodiscard]] bool path_continues(const GroundPathMap& map, const save::TerrainGrid& grid, int col, int row, int dc, int dr);

    /// How far the path is lifted off the ground, in world units, so it never sinks into it.
    inline constexpr float kPathLift = 0.02f;

    /// The ground paths as triangles over the terrain. Batches use GroundPaint::PathA or PathB with the style
    /// index; they go over the ground with the same blending, after it.
    [[nodiscard]] TerrainMesh ground_path_mesh(const GroundPathMap& map, const save::TerrainGrid& grid);

} // namespace rct3::park
