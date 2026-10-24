// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces a tracked ride's track can be built from, told apart by their shape, and where the next piece
// goes at the open end of a track.
//
// A track overlay holds each piece as a section with a scenery item and the splines its cars follow. Pieces
// are drawn running along the model's -X. A piece is known here only by its splines' two ends: how steep each
// end is, how far it leans over and which way it points, and by the pair of small numbers the section stores
// for curves (which side turns and how tightly). The same piece may be laid either way along the track: a
// left curve laid backwards is a right curve, a climb laid backwards a drop, so many overlays hold only one
// hand of each piece.
//
// Measures, all in whole steps:
//  - Slope: 0 level, 1 gentle (about 1 in 2), 2 steep (about 2 in 1), 3 vertical; negative going down.
//  - Bank: an eighth of a turn per step, positive leaning left (the right rail higher), 4 upside down.
//    Upside-down tracks (coasters hanging below their rail) count from their own station.
//  - Heading: an eighth of a turn per step from the model's -X, positive turning right. In the park, from
//    the way an unturned model's -X points.
//  - Curve: 0 straight, 1 loose, 2 medium, 3 tight; negative to the left.
#pragma once

#include "core/maybe.hpp"
#include "park/object_placement.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ovl
{
    class Overlay;
} // namespace rct3::ovl

namespace rct3::park
{

    inline constexpr int kSlopeSteps = 3; ///< Up or down from level
    inline constexpr int kBankSteps = 4;  ///< Eighths of a turn to upside down
    inline constexpr int kHeadings = 8;   ///< Eighths of a turn round
    inline constexpr int kCurveSizes = 3; ///< Loose, medium and tight
    inline constexpr int kUpsideDown = kBankSteps;

    // Bits of a section's flags, as seen across the game's track overlays: lift chain pieces all have the
    // first, station pieces the second, and nearly every piece of later files the third.
    inline constexpr std::uint32_t kRoleBrakes = 1;     ///< PieceRecord::role of brakes
    inline constexpr std::uint32_t kRoleBlockBrake = 2; ///< PieceRecord::role of a block brake
    /// Roles of a rotating tower's pieces (base, shaft, caps): marked as chain, though nothing pulls a train up them
    inline constexpr std::uint32_t kRoleTowerFirst = 91;
    inline constexpr std::uint32_t kRoleTowerLast = 95;
    inline constexpr std::uint32_t kPieceChain = 0x2;
    inline constexpr std::uint32_t kPieceEnclosed = 0x8;      ///< A tube or tunnel round the track
    inline constexpr std::uint32_t kPieceWater = 0x1000;      ///< The track runs through water
    inline constexpr std::uint32_t kPieceBooster = 0x1000000; ///< Speeds the cars up
    inline constexpr std::uint32_t kPieceStation = 0x40;
    inline constexpr std::uint32_t kPiecePlatformAlone = 0x80;   ///< Its platform is capped at both ends, whatever stands beside it
    inline constexpr std::uint32_t kPieceCableLift = 0x20;       ///< A cable pulls the cars up it
    inline constexpr std::uint32_t kPieceLaunch = 0x4000;        ///< Pushes a launched train on, as launch sections do
    inline constexpr std::uint32_t kPieceHolds = 0x100;          ///< A train may be held on it: holding brakes, tilt and reverser pieces, tower caps
    inline constexpr std::uint32_t kPieceLiftTop = 0x800;        ///< The top of a lift, where a train waits for the block ahead
    inline constexpr std::uint32_t kPieceReverseLift = 0x800000; ///< The hill a reverse cable lift ride's train is pulled up backwards
    inline constexpr std::uint32_t kPieceCommon = std::uint32_t { 1 } << 22;
    /// A track may start with the piece laid as drawn, and with the second bit also laid backwards.
    inline constexpr std::uint32_t kPieceStarts = 0x1;
    inline constexpr std::uint32_t kPieceStartsBackward = 0x2000;
    /// A piece pattern never lays the piece backwards.
    inline constexpr std::uint32_t kPieceKeepsWay = 0x4;

    /// One end of a piece as a car passes it.
    struct PieceEnd
    {
        int slope = 0;
        int bank = 0;
        int heading = 0; ///< 0 to 7

        friend bool operator==(const PieceEnd&, const PieceEnd&) = default;
    };

    /// What a piece is for, beyond its shape.
    enum class PieceUse : std::uint8_t
    {
        Plain,   ///< Picked by curve, slope and bank
        Station, ///< Where riders get on and off
        Special, ///< Loops, helices, brakes and the like, picked by name
    };

    /// What the player turns on or off for the next piece, beside its curve, slope and bank, in the order the
    /// game sets out their buttons.
    enum class PieceToggle : std::uint8_t
    {
        Chain,
        Enclosed,
        Water,
        Wide, ///< The piece ends as wide as the wide water rides' channel
        Booster,
    };
    inline constexpr std::size_t kPieceToggles = 5;
    /// The bit of `toggle` in PieceShape::toggles.
    [[nodiscard]] constexpr std::uint8_t toggle_bit(PieceToggle toggle)
    {
        return static_cast<std::uint8_t>(1u << std::to_underlying(toggle));
    }

    /// The two ends of a piece's middle line, between its rails, and which way the track runs there, in the
    /// model's space. The sides run from the left rail to the right rail.
    struct PieceLine
    {
        glm::vec3 start { 0.0f };
        glm::vec3 end { 0.0f };
        glm::vec3 start_direction { -1, 0, 0 };
        glm::vec3 end_direction { -1, 0, 0 };
        glm::vec3 start_side { 0, 0, 1 };
        glm::vec3 end_side { 0, 0, 1 };
        /// Points along the middle line from start to end, a few to each part of the rails' splines; just the
        /// two ends when the rails are split differently.
        std::vector<glm::vec3> path { };
        /// Which way is up for the track at each point of `path`: square to the rails, so it leans with
        /// the bank. A unit vector.
        std::vector<glm::vec3> ups { };
    };

    /// How one end of a piece meets the end of another, beyond their shape: by name, by the tags each offers
    /// the other, needs from it and forbids in it, and by width.
    struct PieceJoin
    {
        std::string name;
        std::vector<std::string> offers;
        std::vector<std::string> needs;
        std::vector<std::string> forbids;
        std::uint32_t width = 0;
    };

    /// What a section stores about a piece besides its splines.
    struct PieceRecord
    {
        std::string name; ///< Of the section
        std::string item; ///< Of its scenery item, which the park's object names
        std::uint32_t join_a = 0;
        std::uint32_t join_b = 0;
        std::uint32_t flags = 0;
        std::uint32_t role = 0; ///< As ovl::TrackSection::role
        std::array<std::int32_t, 2> squares { 1, 1 };
        Positioning positioning = Positioning::Tile;
        std::int32_t cost = 0;          ///< As the scenery item stores it
        std::array<PieceJoin, 2> joins; ///< Of its start and its end, laid as drawn
        std::uint32_t side = 0;         ///< Of side by side tracks; ends meet only ends of the same side
        std::string partner;            ///< Of side by side tracks: the other half of a joined piece, by name
        std::string group;              ///< For the ride's piece patterns
    };

    struct PieceShape
    {
        PieceRecord record;
        PieceLine line;
        PieceEnd entry; ///< Laid as drawn
        PieceEnd exit;
        int curve = 0;
        PieceUse use = PieceUse::Plain;
        std::uint8_t toggles = 0; ///< Bits of the PieceToggle values it has, laid as drawn
    };

    /// `line` measured in steps. Banks are counted from `base_bank`, in degrees.
    [[nodiscard]] PieceEnd measure_end(const glm::vec3& direction, const glm::vec3& side, float base_bank = 0);
    /// The lean of an end in degrees, positive to the left.
    [[nodiscard]] float bank_degrees(const glm::vec3& direction, const glm::vec3& side);

    /// A piece as its record and line make it. `base_bank` as in measure_end.
    [[nodiscard]] PieceShape shape_piece(PieceRecord record, const PieceLine& line, float base_bank = 0);

    /// The pieces of one track overlay, as shape_piece makes them, in the overlay's order.
    struct PieceCatalogue
    {
        std::string overlay; ///< As a park names it
        std::vector<PieceShape> pieces;
    };

    /// The catalogue of the track overlay whose common file is `file`, named `overlay` as a park names it.
    /// Sections whose splines or scenery item are missing are left out.
    [[nodiscard]] std::expected<PieceCatalogue, std::string> load_piece_catalogue(const std::filesystem::path& file, std::string overlay);

    /// The track overlays a tracked ride's file names among its strings, as paths relative to the ride's folder
    /// ("../Track7/Track7"), in AssetIndex's form from `path`, where the ride's overlay is. Its pieces come from
    /// the first that holds any; other paths, such as the folder of its icons, hold none.
    [[nodiscard]] std::vector<std::string> named_overlays(const ovl::Overlay& ride, std::string_view path);

    /// The piece of `catalogue` a track piece's symbol names, with or without its tag; nothing when not there.
    [[nodiscard]] std::optional<const PieceShape&> shape_named(const PieceCatalogue& catalogue, std::string_view symbol);

    /// The bank the catalogue's pieces count from: that of its stations, in degrees (0 or 180).
    [[nodiscard]] float station_bank(std::span<const PieceRecord> records, std::span<const PieceLine> lines);

    /// Pieces made from records and lines side by side, counting banks from their stations'. Plain pieces
    /// that share a shape with a plainer one become special, so each shape has one plain piece.
    [[nodiscard]] std::vector<PieceShape> shape_pieces(std::span<const PieceRecord> records, std::span<const PieceLine> lines);

    /// A piece laid one way or the other.
    struct LaidPiece
    {
        std::size_t index = 0; ///< In the catalogue
        bool reversed = false;

        friend bool operator==(const LaidPiece&, const LaidPiece&) = default;
    };

    /// The ends and curve of `shape` laid as `reversed` says.
    [[nodiscard]] PieceEnd laid_entry(const PieceShape& shape, bool reversed);
    [[nodiscard]] PieceEnd laid_exit(const PieceShape& shape, bool reversed);
    [[nodiscard]] int laid_curve(const PieceShape& shape, bool reversed);

    /// Whether `end` points along a diagonal.
    [[nodiscard]] constexpr bool diagonal(const PieceEnd& end)
    {
        return (end.heading & 1) != 0;
    }

    /// Whether a piece entering at `entry` may follow a track whose last piece leaves at `tip`: the same
    /// slope and bank, and a heading on the same kind of line (along the grid or across it), as pieces only
    /// turn by quarter turns.
    [[nodiscard]] bool fits_after(const PieceEnd& tip, const PieceEnd& entry);

    /// Whether the end `before` of one piece and the end `next` of the piece after it may meet: the same name
    /// (ignoring case) and width, and on each side, when it needs tags, one of them offered by the other, and
    /// none of those it forbids.
    [[nodiscard]] bool ends_meet(const PieceJoin& before, const PieceJoin& next);

    /// How `shape` laid as `reversed` joins at its start and its end.
    [[nodiscard]] const PieceJoin& laid_start_join(const PieceShape& shape, bool reversed);
    [[nodiscard]] const PieceJoin& laid_end_join(const PieceShape& shape, bool reversed);

    /// The open end of a track in the park.
    struct TrackTip
    {
        glm::vec3 position { 0.0f };
        PieceEnd end; ///< Heading in the park's eighths
    };

    /// Where a piece goes in the park: an object to place and the open end it leaves.
    struct PiecePlacement
    {
        save::PlacedObject object; ///< Tile, height and turn set; the rest left for the caller
        TrackTip next;
    };

    /// Where `shape` laid as `reversed` goes so that it carries on from `tip` on `grid`. Nothing when its
    /// start cannot meet the tip at a whole tile and height.
    [[nodiscard]] std::optional<PiecePlacement> place_after(const TrackTip& tip, const PieceShape& shape, bool reversed, const save::TerrainGrid& grid);

    /// The open end a placed piece leaves when laid as `reversed`.
    [[nodiscard]] TrackTip tip_of(const save::PlacedObject& object, const PieceShape& shape, bool reversed, const save::TerrainGrid& grid);
    /// Where a placed piece laid as `reversed` starts, as the open end a piece before it would leave.
    [[nodiscard]] TrackTip start_of(const save::PlacedObject& object, const PieceShape& shape, bool reversed, const save::TerrainGrid& grid);

    /// The pieces that can follow `tip`, each with the way it is laid: a piece that fits both ways (a level
    /// curve, which turns the other way when laid backwards) comes twice. With `last`, the piece of `pieces`
    /// the tip is the end of, its end must meet theirs too.
    [[nodiscard]] std::vector<LaidPiece> pieces_after(std::span<const PieceShape> pieces, const PieceEnd& tip, std::optional<LaidPiece> last = std::nullopt);

    /// The pieces a track may start with, each with the way it is laid.
    [[nodiscard]] std::vector<LaidPiece> starting_pieces(std::span<const PieceShape> pieces);

} // namespace rct3::park
