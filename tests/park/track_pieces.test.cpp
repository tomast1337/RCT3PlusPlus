// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_pieces.hpp"

#include "../ovl/record_file.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <format>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    constexpr float kTile = 4;

    /// Ten by ten flat tiles from (0, 0).
    save::TerrainGrid flat()
    {
        save::TerrainGrid g;
        g.width = 10;
        g.height = 10;
        g.tile_size_x = kTile;
        g.tile_size_z = kTile;
        g.cells.resize(100);
        return g;
    }

    /// A level, unbanked line one tile long through the middle of its tile, along the model's -X.
    PieceLine straight_line()
    {
        PieceLine l;
        l.start = { kTile / 2, 0, 0 };
        l.end = { -kTile / 2, 0, 0 };
        return l;
    }

    PieceRecord record(std::string name, std::uint32_t flags = 0)
    {
        PieceRecord r;
        r.name = name;
        r.item = std::format("{}Item", name);
        r.flags = flags;
        return r;
    }

    /// A piece with the given ends and curve, as its line would measure.
    PieceShape shape(PieceEnd entry, PieceEnd exit, int curve = 0, PieceUse use = PieceUse::Plain)
    {
        PieceShape s;
        s.entry = entry;
        s.exit = exit;
        s.curve = curve;
        s.use = use;
        return s;
    }

    save::PlacedObject placed(int col, int row, int turns, int height)
    {
        save::PlacedObject o;
        o.col = col;
        o.row = row;
        o.rotation = turns;
        o.base_height = height;
        o.force_absolute_height = true;
        return o;
    }

    bool has(const std::vector<LaidPiece>& fit, std::size_t index, bool reversed)
    {
        return std::ranges::contains(fit, LaidPiece { index, reversed });
    }
} // namespace

TEST_CASE("slopes are counted in steps from level, up positive")
{
    const glm::vec3 side { 0, 0, 1 };
    CHECK(measure_end({ -1, 0, 0 }, side).slope == 0);
    CHECK(measure_end({ -2, 1, 0 }, side).slope == 1);
    CHECK(measure_end({ -1, 2, 0 }, side).slope == 2);
    CHECK(measure_end({ 0, 1, 0 }, side).slope == 3);
    CHECK(measure_end({ -2, -1, 0 }, side).slope == -1);
    CHECK(measure_end({ 0, -1, 0 }, side).slope == -3);
}

TEST_CASE("banks lean left when the right rail is higher, upside down at four steps")
{
    const glm::vec3 forward { -1, 0, 0 };
    CHECK(measure_end(forward, { 0, 0, 1 }).bank == 0);
    CHECK(measure_end(forward, glm::normalize(glm::vec3 { 0, 1, 1 })).bank == 1);
    CHECK(measure_end(forward, { 0, 1, 0 }).bank == 2);
    CHECK(measure_end(forward, glm::normalize(glm::vec3 { 0, -1, 1 })).bank == -1);
    CHECK(measure_end(forward, { 0, 0, -1 }).bank == kUpsideDown);
    CHECK(bank_degrees(forward, glm::normalize(glm::vec3 { 0, 1, 1 })) == doctest::Approx(45));
    // A track that hangs below its rail counts from its own upside-down station.
    CHECK(measure_end(forward, { 0, 0, -1 }, 180).bank == 0);
}

TEST_CASE("headings count eighths of a turn from -X, turning right positive")
{
    const glm::vec3 side { 0, 0, 1 };
    CHECK(measure_end({ -1, 0, 0 }, side).heading == 0);
    CHECK(measure_end({ -1, 0, 1 }, side).heading == 1);
    CHECK(measure_end({ 0, 0, 1 }, side).heading == 2);
    CHECK(measure_end({ 1, 0, 0 }, side).heading == 4);
    CHECK(measure_end({ 0, 0, -1 }, side).heading == 6);
    // Straight up, the way the rails' level line faces stands in for the direction.
    CHECK(measure_end({ 0, 1, 0 }, { 0, 0, 1 }).heading == 0);
}

TEST_CASE("a piece laid backwards swaps its ends, turning them round and negating slope and bank")
{
    const PieceShape s = shape({ 0, 0, 0 }, { 1, 1, 2 }, 2);
    CHECK(laid_entry(s, false) == PieceEnd { 0, 0, 0 });
    CHECK(laid_exit(s, false) == PieceEnd { 1, 1, 2 });
    CHECK(laid_entry(s, true) == PieceEnd { -1, -1, 6 });
    CHECK(laid_exit(s, true) == PieceEnd { 0, 0, 4 });
    CHECK(laid_curve(s, true) == -2);
}

TEST_CASE("curves take their size from the curve pair and their side from the turn")
{
    PieceLine right = straight_line();
    right.end_direction = { 0, 0, 1 };
    PieceRecord tight = record("Tight");
    tight.join_a = 3;
    CHECK(shape_piece(tight, right).curve == 3);
    PieceLine left = straight_line();
    left.end_direction = { 0, 0, -1 };
    PieceRecord loose = record("Loose");
    loose.join_a = 4;
    CHECK(shape_piece(loose, left).curve == -1);
    // A turning piece without a curve pair is picked by name.
    CHECK(shape_piece(record("Helix"), right).use == PieceUse::Special);
    CHECK(shape_piece(record("Station", kPieceStation), straight_line()).use == PieceUse::Station);
    CHECK(shape_piece(record("Lift", kPieceChain), straight_line()).toggles == toggle_bit(PieceToggle::Chain));
    CHECK(shape_piece(record("Tube", kPieceEnclosed | kPieceBooster), straight_line()).toggles == (toggle_bit(PieceToggle::Enclosed) | toggle_bit(PieceToggle::Booster)));
    CHECK(shape_piece(record("Splash", kPieceWater), straight_line()).toggles == toggle_bit(PieceToggle::Water));
    PieceRecord widening = record("Widening");
    widening.joins[1].width = 1;
    CHECK(shape_piece(widening, straight_line()).toggles == toggle_bit(PieceToggle::Wide));
}

TEST_CASE("each shape keeps one plain piece, the one with the fewest flags")
{
    const std::vector<PieceRecord> records { record("StraightFancy", 0x10 | kPieceCommon), record("Straight", kPieceCommon), record("StraightLift", kPieceChain) };
    const std::vector<PieceLine> lines(records.size(), straight_line());
    const std::vector<PieceShape> pieces = shape_pieces(records, lines);
    REQUIRE(pieces.size() == 3);
    CHECK(pieces[0].use == PieceUse::Special);
    CHECK(pieces[1].use == PieceUse::Plain);
    // A lift chain makes a shape of its own.
    CHECK(pieces[2].use == PieceUse::Plain);
}

TEST_CASE("a piece fits after a tip with the same slope and bank on the same kind of line")
{
    CHECK(fits_after({ 1, 0, 0 }, { 1, 0, 2 }));
    CHECK_FALSE(fits_after({ 1, 0, 0 }, { 0, 0, 0 }));
    CHECK_FALSE(fits_after({ 0, 1, 0 }, { 0, 0, 0 }));
    CHECK_FALSE(fits_after({ 0, 0, 0 }, { 0, 0, 1 }));
    CHECK(fits_after({ 0, 0, 1 }, { 0, 0, 3 }));
}

TEST_CASE("pieces that fit either way come twice, the others once or not at all")
{
    const std::vector<PieceShape> pieces {
        shape({ 0, 0, 0 }, { 0, 0, 2 }, 2), // Level curve
        shape({ 0, 0, 0 }, { 1, 0, 0 }),    // Climb
        shape({ 1, 0, 0 }, { 1, 0, 0 }),    // Steady climb
    };
    const std::vector<LaidPiece> level = pieces_after(pieces, { 0, 0, 0 });
    CHECK(has(level, 0, false));
    CHECK(has(level, 0, true));
    CHECK(has(level, 1, false));
    CHECK_FALSE(has(level, 1, true));
    CHECK_FALSE(has(level, 2, false));
    const std::vector<LaidPiece> climbing = pieces_after(pieces, { 1, 0, 0 });
    CHECK(has(climbing, 2, false));
    CHECK_FALSE(has(climbing, 0, false));
}

TEST_CASE("the tip of a placed piece is its line's end in the park, turned with it")
{
    const save::TerrainGrid g = flat();
    const PieceShape s = shape_piece(record("Straight"), straight_line());
    const TrackTip tip = tip_of(placed(5, 5, 0, 3), s, false, g);
    // Unturned, the model's -X points along the park's +X.
    CHECK(tip.position.x == doctest::Approx(6 * kTile));
    CHECK(tip.position.y == doctest::Approx(3));
    CHECK(tip.position.z == doctest::Approx(5.5f * kTile));
    CHECK(tip.end == PieceEnd { 0, 0, 0 });
    CHECK(tip_of(placed(5, 5, 1, 3), s, false, g).end.heading == 2);
    CHECK(tip_of(placed(5, 5, 0, 3), s, true, g).end.heading == 4);
    // Its start, as the end of a piece before it.
    const TrackTip start = start_of(placed(5, 5, 0, 3), s, false, g);
    CHECK(start.position.x == doctest::Approx(5 * kTile));
    CHECK(start.end == PieceEnd { 0, 0, 0 });
    CHECK(place_after(start, s, false, g)->object.col == 5);
}

TEST_CASE("the next piece goes on the tile its start meets, turned to the tip")
{
    const save::TerrainGrid g = flat();
    const PieceShape s = shape_piece(record("Straight"), straight_line());
    const TrackTip tip = tip_of(placed(5, 5, 0, 3), s, false, g);

    const auto ahead = place_after(tip, s, false, g);
    REQUIRE(ahead);
    CHECK(ahead->object.col == 6);
    CHECK(ahead->object.row == 5);
    CHECK(ahead->object.rotation == 0);
    CHECK(ahead->object.base_height == 3);
    CHECK(ahead->object.symbol == "StraightItem");
    CHECK(ahead->next.position.x == doctest::Approx(7 * kTile));

    // Laid backwards it is turned half round on the same tile.
    const auto back = place_after(tip, s, true, g);
    REQUIRE(back);
    CHECK(back->object.col == 6);
    CHECK(back->object.rotation == 2);
    CHECK(back->next.end.heading == 0);
}

TEST_CASE("a piece whose start cannot meet the tip at a whole tile and height is not placed")
{
    const save::TerrainGrid g = flat();
    const PieceShape s = shape_piece(record("Straight"), straight_line());
    TrackTip tip = tip_of(placed(5, 5, 0, 3), s, false, g);
    tip.position.y += 0.5f;
    CHECK_FALSE(place_after(tip, s, false, g));
    tip.position.y -= 0.5f;
    tip.position.x += 1;
    CHECK_FALSE(place_after(tip, s, false, g));
    // Nor one that does not fit the tip at all.
    tip.position.x -= 1;
    CHECK(place_after(tip, s, false, g));
    CHECK_FALSE(place_after({ tip.position, { 1, 0, 0 } }, s, false, g));
}

TEST_CASE("ends meet by name, width and the tags each side needs and forbids")
{
    const auto end = [](std::string name, std::vector<std::string> offers = { }, std::vector<std::string> needs = { }, std::vector<std::string> forbids = { }, std::uint32_t width = 0) { return PieceJoin { std::move(name), std::move(offers), std::move(needs), std::move(forbids), width }; };
    CHECK(ends_meet(end(""), end("")));
    CHECK(ends_meet(end("Rope"), end("rope")));
    CHECK_FALSE(ends_meet(end("Rope"), end("")));
    CHECK_FALSE(ends_meet(end(""), end("cap")));
    CHECK_FALSE(ends_meet(end("", { }, { }, { }, 1), end("")));
    // Needs one of the other's offers, from either side.
    CHECK(ends_meet(end("", { "Shelf" }), end("", { }, { "Ledge", "Shelf" })));
    CHECK_FALSE(ends_meet(end(""), end("", { }, { "Shelf" })));
    CHECK_FALSE(ends_meet(end("", { }, { "Shelf" }), end("", { "Ledge" })));
    // Forbids none of them.
    CHECK_FALSE(ends_meet(end("", { "Shelf" }), end("", { }, { }, { "shelf" })));
    CHECK(ends_meet(end("", { "Ledge" }), end("", { }, { }, { "Shelf" })));
}

TEST_CASE("after a piece, only pieces whose start meets its end follow")
{
    // A climb whose top ends in a cap, a rope piece that runs from its foot, and a plain one.
    std::vector<PieceShape> pieces { shape({ 0, 0, 0 }, { 0, 0, 0 }), shape({ 0, 0, 0 }, { 0, 0, 0 }), shape({ 0, 0, 0 }, { 0, 0, 0 }) };
    pieces[0].record.joins[1].name = "cap";
    pieces[1].record.joins[0].name = "cap";
    const std::vector<LaidPiece> after_cap = pieces_after(pieces, { 0, 0, 0 }, LaidPiece { 0, false });
    CHECK(has(after_cap, 1, false));
    CHECK_FALSE(has(after_cap, 2, false));
    CHECK_FALSE(has(after_cap, 0, false));
    // Laid backwards, the cap is at the piece's start, so the plain piece follows.
    const std::vector<LaidPiece> after_back = pieces_after(pieces, { 0, 0, 0 }, LaidPiece { 0, true });
    CHECK(has(after_back, 2, false));
    CHECK(has(after_back, 1, true));
    CHECK_FALSE(has(after_back, 1, false));
    // Pieces of side by side tracks only follow their own side's.
    pieces[2].record.side = 1;
    CHECK_FALSE(has(pieces_after(pieces, { 0, 0, 0 }, LaidPiece { 1, false }), 2, false));
}

TEST_CASE("a track starts only with pieces marked to, backwards only when marked so")
{
    std::vector<PieceShape> pieces(3);
    pieces[0].record.flags = kPieceStarts;
    pieces[1].record.flags = kPieceStarts | kPieceStartsBackward;
    pieces[2].record.flags = kPieceStartsBackward;
    CHECK(starting_pieces(pieces) == std::vector<LaidPiece> { { 0, false }, { 1, false }, { 1, true } });
}

TEST_CASE("a ride's file names the track overlay its pieces come from")
{
    test::Pool pool;
    pool.add("../Pieces/Pieces");
    pool.add("../../Icons/Icons");
    pool.add("plain words");
    pool.add("Ride:trr");
    const ovl::Overlay ride = test::make_record_overlay(5, pool, "Ride:trr", std::vector<std::byte>(16));
    CHECK(named_overlays(ride, "rides/Ride/Ride") == std::vector<std::string> { "rides/Pieces/Pieces", "Icons/Icons" });
}
