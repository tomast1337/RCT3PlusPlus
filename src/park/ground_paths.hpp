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
// Rides and stalls pave the ground they use as if path lay there: each door's tile, a stall's or facility's
// footprint and a flat ride's floor. Paving takes the style of the path the door or stall opens onto, so it matches; an
// entrance carries a queue straight on through. It joins only itself and the tiles it opens onto, never a
// path that merely runs beside it, even where no fence stands between them. Where the ground is not level at
// its height it is a raised deck instead.
#pragma once

#include "park/edge_walls.hpp"
#include "park/terrain_mesh.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
        std::vector<std::string> styles;                     ///< System names, in the order first met
        std::vector<std::int16_t> tiles;                     ///< Row by row: index into styles, or kNoPath
        std::map<std::size_t, std::vector<float>> raised;    ///< By tile index: heights the raised paths there end at
        std::map<std::size_t, std::uint8_t> queues;          ///< By tile index: ground queue's open sides, bit per TileSide
        EdgeWalls walls;                                     ///< That paths do not join across; set by the caller
        std::vector<std::uint16_t> pavings;                  ///< Row by row: 1 + the paving a tile is laid by, 0 for a path of the park; empty when none
        std::set<std::pair<std::size_t, std::size_t>> links; ///< Tile index pairs, both ways, that paving joins across
    };

    inline constexpr std::int16_t kNoPath = -1;

    /// How much a raised path piece climbs over its tile, from its stored slope: none when flat or unknown.
    [[nodiscard]] float path_rise(std::uint8_t slope) noexcept;

    /// How close an end of a raised path must be to the ground to meet a ground path, in world units.
    inline constexpr float kPathMeets = 0.25f;

    /// The ground paths among `paths` on a grid of the terrain's size, and the ends of the raised ones. Paths
    /// off the grid and ground paths without a style are left out.
    [[nodiscard]] GroundPathMap ground_path_map(std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// Ground a ride or stall paves as if path lay there: a door's tile, a stall's footprint, a flat ride's floor.
    /// It is paved like the path at `source`, or in kPavingStyle while there is none, and joins only itself and
    /// the tiles in `links`, so it never runs into paths beside it.
    struct Paving
    {
        std::vector<save::Tile> tiles { };
        std::int32_t height = 0;              ///< Whole units, as placement spots store it
        std::optional<save::Tile> source { }; ///< The tile whose path sets the style
        bool entrance = false;                ///< A queue at `source` runs on through it, straight
        std::vector<save::Tile> links { };    ///< The tiles beside it that it joins
    };

    /// The path style paving takes while its source holds no path: the first of the game's path styles.
    inline constexpr std::string_view kPavingStyle = "Asphalt";

    /// How far from a paving's height a path at its source may lie and still set its style, in whole units.
    inline constexpr float kPavingReach = 1;

    /// The tiles a placed footprint stands on.
    using FootprintOf = std::function<std::vector<save::Tile>(const save::PlacementSpot& spot)>;

    /// What the attractions of `rides` pave: every door, every stall's and facility's footprint, every flat ride's
    /// floor and the floor of every tracked ride's base (a tower's or a house's).
    /// `footprint_of` gives the tiles a ride or stall stands on, only the squares it really fills; nothing when
    /// unknown. `floor_of` gives the tiles a track piece paves under it: a base's footprint, nothing for the rest.
    [[nodiscard]] std::vector<Paving> ride_pavings(const save::RideModel& rides, const FootprintOf& footprint_of, const FootprintOf& floor_of);

    /// One tile of paving laid as a path, and the paving it belongs to.
    struct PavedTile
    {
        save::PathTile path { };
        std::size_t paving = 0;
    };

    /// The paths `pavings` lay: one per tile that holds no path of `paths` yet. A tile whose ground lies level at
    /// the paving's height gets a ground path (or a ground queue running straight in); any other a flat raised one.
    [[nodiscard]] std::vector<PavedTile> lay_pavings(std::span<const save::PathTile> paths, std::span<const Paving> pavings, const save::TerrainGrid& grid);

    /// Marks the tiles laid in `map`, so each joins only its own paving and its links.
    void seal_pavings(GroundPathMap& map, std::span<const PavedTile> laid, std::span<const Paving> pavings);

    /// Whether the path on `tile` may join the one on `other` beside it: both are paths of the park, both are
    /// paving of the same ride or stall, or one is paving that links to the other.
    [[nodiscard]] bool may_join(const GroundPathMap& map, save::Tile tile, save::Tile other) noexcept;

    /// True when there is a ground path at (col, row); false off the map.
    [[nodiscard]] bool has_ground_path(const GroundPathMap& map, save::Tile tile) noexcept;

    /// True when the path on tile (col, row) carries on onto the tile (dc, dr) beside it (one step along x
    /// or z): a ground path is there, a ground queue there is open towards this tile, or a raised path there
    /// ends level with this tile's ground at their shared edge; the two may join (may_join); and no wall of `map`
    /// stands between them, unless both are paving of one ride or stall.
    [[nodiscard]] bool path_continues(const GroundPathMap& map, const save::TerrainGrid& grid, save::Tile tile, int dc, int dr);

    /// How far the path is lifted off the ground, in world units, so it never sinks into it.
    inline constexpr float kPathLift = 0.02f;

    /// The ground paths as triangles over the terrain. Batches use GroundPaint::PathA or PathB with the style
    /// index; they go over the ground with the same blending, after it.
    [[nodiscard]] TerrainMesh ground_path_mesh(const GroundPathMap& map, const save::TerrainGrid& grid);

} // namespace rct3::park
