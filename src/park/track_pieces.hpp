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
#include <vector>

namespace rct3::park
{

    inline constexpr int kSlopeSteps = 3; ///< Up or down from level
    inline constexpr int kBankSteps = 4;  ///< Eighths of a turn to upside down
    inline constexpr int kHeadings = 8;   ///< Eighths of a turn round
    inline constexpr int kCurveSizes = 3; ///< Loose, medium and tight
    inline constexpr int kUpsideDown = kBankSteps;

    // Bits of a section's flags, as seen across the game's track overlays: lift chain pieces all have the
    // first, station pieces the second, and nearly every piece of later files the third.
    inline constexpr std::uint32_t kPieceChain = 0x2;
    inline constexpr std::uint32_t kPieceStation = 0x40;
    inline constexpr std::uint32_t kPieceCommon = std::uint32_t { 1 } << 22;

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

    /// What a section stores about a piece besides its splines.
    struct PieceRecord
    {
        std::string name; ///< Of the section
        std::string item; ///< Of its scenery item, which the park's object names
        std::uint32_t join_a = 0;
        std::uint32_t join_b = 0;
        std::uint32_t flags = 0;
        std::array<std::int32_t, 2> squares { 1, 1 };
        Positioning positioning = Positioning::Tile;
        std::int32_t cost = 0; ///< As the scenery item stores it
    };

    struct PieceShape
    {
        PieceRecord record;
        PieceLine line;
        PieceEnd entry; ///< Laid as drawn
        PieceEnd exit;
        int curve = 0;
        PieceUse use = PieceUse::Plain;
        bool chain = false;
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

    /// The piece of `catalogue` a track piece's symbol names, with or without its tag; nothing when not there.
    [[nodiscard]] const PieceShape* shape_named(const PieceCatalogue& catalogue, std::string_view symbol);

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

    /// The pieces that can follow `tip`, each with the way it is laid: a piece that fits both ways (a level
    /// curve, which turns the other way when laid backwards) comes twice.
    [[nodiscard]] std::vector<LaidPiece> pieces_after(std::span<const PieceShape> pieces, const PieceEnd& tip);

} // namespace rct3::park
