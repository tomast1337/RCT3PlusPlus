// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/pieces.hpp"

#include "../../ovl/builder.hpp"
#include "../../ovl/record_file.hpp"
#include "ovl/spline.hpp"
#include "park/object/placement.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::track;

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
    CHECK(measure_end({ -1, 0, 0 }, side).heading == Heading8 { 0 });
    CHECK(measure_end({ -1, 0, 1 }, side).heading == Heading8 { 1 });
    CHECK(measure_end({ 0, 0, 1 }, side).heading == Heading8 { 2 });
    CHECK(measure_end({ 1, 0, 0 }, side).heading == Heading8 { 4 });
    CHECK(measure_end({ 0, 0, -1 }, side).heading == Heading8 { 6 });
    // Straight up, the way the rails' level line faces stands in for the direction.
    CHECK(measure_end({ 0, 1, 0 }, { 0, 0, 1 }).heading == Heading8 { 0 });
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
    CHECK(tip_of(placed(5, 5, 1, 3), s, false, g).end.heading == Heading8 { 2 });
    CHECK(tip_of(placed(5, 5, 0, 3), s, true, g).end.heading == Heading8 { 4 });
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
    CHECK(back->next.end.heading == Heading8 { 0 });
}

TEST_CASE("a saved layout moved and turned keeps each piece where the one before it ends")
{
    const save::TerrainGrid g = flat();
    const PieceShape s = shape_piece(record("Straight"), straight_line());
    PieceRecord r = record("Long");
    r.squares = { 2, 1 };
    PieceLine l;
    l.start = { kTile, 0, 0 };
    l.end = { -kTile, 0, 0 };
    const PieceShape longer = shape_piece(r, l);
    const save::PlacedObject first = placed(1, 1, 0, 3);
    const auto second = place_after(tip_of(first, s, false, g), longer, false, g);
    REQUIRE(second);
    const auto third = place_after(second->next, s, false, g);
    REQUIRE(third);
    for (int turns = 0; turns < 4; ++turns)
    {
        CAPTURE(turns);
        const save::PlacedObject to = placed(5, 4, turns, 7);
        const save::PlacedObject moved = object::moved_with(second->object, r.squares, first, { 1, 1 }, to);
        const auto laid = place_after(tip_of(to, s, false, g), longer, false, g);
        REQUIRE(laid);
        CHECK(moved.col == laid->object.col);
        CHECK(moved.row == laid->object.row);
        CHECK(moved.rotation == laid->object.rotation);
        CHECK(moved.base_height == laid->object.base_height);
        // The piece after it, measured from the moved one rather than the first.
        const save::PlacedObject next = object::moved_with(third->object, { 1, 1 }, second->object, r.squares, moved);
        const auto after = place_after(laid->next, s, false, g);
        REQUIRE(after);
        CHECK(next.col == after->object.col);
        CHECK(next.row == after->object.row);
    }
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

TEST_CASE("a track set's pieces are also looked for in the files beside it whose loaders include sections")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "rct3pp_piece_files_beside";
    fs::remove_all(dir);
    fs::create_directories(dir);
    // The header of a file whose one loader has the tag `tag`.
    const auto write = [&](const std::string& name, const std::string& tag) {
        test::Builder b;
        b.raw("FGRK", 4);
        b.u8(0x02), b.u8(0), b.u8(0), b.u8(0);
        b.u32(5), b.u32(0x10);
        b.u16(0), b.u16(0); // No stream block
        b.u32(0), b.u32(0); // No references
        b.u32(1);
        b.str("FGDK"), b.str("Loader"), b.u32(1), b.str(tag);
        b.u32(0), b.u32(1);
        std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(b.bytes.data()), static_cast<std::streamsize>(b.bytes.size()));
    };
    write("Set.common.ovl", "tks");
    write("Set_More.common.ovl", "tks");
    write("Bend_data.common.ovl", "shs");
    write("Set_More.unique.ovl", "tks");
    std::ofstream(dir / "object::Broken.common.ovl") << "not an overlay";
    CHECK(piece_files_beside(dir / "Set.common.ovl") == std::vector { dir / "Set_More.common.ovl" });
    fs::remove_all(dir);
}

TEST_CASE("slide_climbs: each slide is reached up the loop and along the path ending nearest its top")
{
    // A walk up from the water, a stub off each side at the top, and two slides down from the stubs' ends.
    const std::vector<glm::vec3> loop { { 0, 0, 10 }, { 0, 2, 6 }, { 0, 4, 2 }, { 0, 5, 0 } };
    const std::vector<std::vector<glm::vec3>> paths { { { 0, 4, 2 }, { -2, 4, 2 } }, { { 0, 5, 0 }, { 2, 5, 0 } } };
    const std::vector<std::vector<glm::vec3>> slides { { { 2.1f, 5, 0 }, { 3, 0, 10 } }, { { -2.1f, 4, 2 }, { -3, 0, 10 } } };
    const auto ways = slide_climbs(loop, paths, slides);
    REQUIRE(ways.size() == 2);
    CHECK(ways[0] == std::vector<glm::vec3> { { 0, 0, 10 }, { 0, 2, 6 }, { 0, 4, 2 }, { 0, 5, 0 }, { 0, 5, 0 }, { 2, 5, 0 }, { 2.1f, 5, 0 }, { 3, 0, 10 } });
    CHECK(ways[1] == std::vector<glm::vec3> { { 0, 0, 10 }, { 0, 2, 6 }, { 0, 4, 2 }, { 0, 4, 2 }, { -2, 4, 2 }, { -2.1f, 4, 2 }, { -3, 0, 10 } });
    // With no walk up, no ways.
    CHECK(slide_climbs({ }, paths, slides).empty());
}

TEST_CASE("a piece's middle line follows its rails even when they are split into a different number of parts")
{
    // Straight rails 8 long along -X, a metre apart: the left in two parts, the right in one.
    const auto rail = [](float z, std::vector<float> xs) {
        ovl::Spline s;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            ovl::SplineNode n;
            n.position = { xs[i], 0, z };
            if (i > 0)
                n.tangent_in = { (xs[i - 1] - xs[i]) / 3, 0, 0 };
            if (i + 1 < xs.size())
                n.tangent_out = { (xs[i + 1] - xs[i]) / 3, 0, 0 };
            s.nodes.push_back(n);
        }
        return s;
    };
    const auto line = middle_line(rail(-0.5f, { 4, 0, -4 }), rail(0.5f, { 4, -4 }));
    REQUIRE(line);
    REQUIRE(line->path.size() > 2);
    float length = 0;
    for (std::size_t k = 0; k < line->path.size(); ++k)
    {
        CHECK(line->path[k].z == doctest::Approx(0));
        CHECK(line->ups[k].y == doctest::Approx(1));
        if (k > 0)
            length += glm::distance(line->path[k - 1], line->path[k]);
    }
    CHECK(length == doctest::Approx(8));
    CHECK(line->path[line->path.size() / 2].x == doctest::Approx(0));
}
