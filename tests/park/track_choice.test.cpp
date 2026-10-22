// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_choice.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::park;

namespace
{
    PieceShape piece(PieceEnd exit, int curve = 0, PieceUse use = PieceUse::Plain, bool chain = false)
    {
        PieceShape s;
        s.exit = exit;
        s.curve = curve;
        s.use = use;
        s.chain = chain;
        return s;
    }

    /// A small track: straight, a medium right curve (left when laid backwards), a climb and the same with a
    /// lift chain, a banked straight, a station and a loop.
    std::vector<PieceShape> pieces()
    {
        return {
            piece({ 0, 0, 0 }),                           // 0
            piece({ 0, 0, 2 }, 2),                        // 1
            piece({ 1, 0, 0 }),                           // 2
            piece({ 1, 0, 0 }, 0, PieceUse::Plain, true), // 3
            piece({ 0, 1, 0 }),                           // 4
            piece({ 0, 0, 0 }, 0, PieceUse::Special),     // 5
            piece({ 0, 0, 0 }, 0, PieceUse::Station),     // 6
        };
    }

    TrackChoice picks(int curve, int slope = 0, int bank = 0, bool chain = false)
    {
        TrackChoice c;
        c.curve = curve;
        c.slope = slope;
        c.bank = bank;
        c.chain = chain;
        return c;
    }

    /// What fits a level, unbanked tip: everything above but the curve only fits backwards as well.
    std::vector<LaidPiece> fitting()
    {
        return { { 0, false }, { 1, false }, { 1, true }, { 2, false }, { 3, false }, { 4, false }, { 5, false }, { 6, false } };
    }
} // namespace

TEST_CASE("a value is offered when some fitting piece has it")
{
    const auto all = pieces();
    const ChoiceOptions o = choice_options(all, fitting(), { });
    CHECK(o.curves.at(curve_index(0)));
    CHECK(o.curves.at(curve_index(2)));
    CHECK(o.curves.at(curve_index(-2)));
    CHECK_FALSE(o.curves.at(curve_index(1)));
    CHECK(o.slopes.at(slope_index(1)));
    CHECK_FALSE(o.slopes.at(slope_index(-1)));
    CHECK(o.banks.at(bank_index(1)));
    CHECK_FALSE(o.banks.at(bank_index(kUpsideDown)));
    CHECK(o.any_chain);
    CHECK_FALSE(o.chain);
}

TEST_CASE("pieces picked by name are listed stations first")
{
    const auto all = pieces();
    const ChoiceOptions o = choice_options(all, fitting(), { });
    REQUIRE(o.named.size() == 2);
    CHECK(o.named[0] == LaidPiece { 6, false });
    CHECK(o.named[1] == LaidPiece { 5, false });
}

TEST_CASE("the picks name the piece laid as drawn first")
{
    const auto all = pieces();
    CHECK(chosen_piece(all, fitting(), { }) == LaidPiece { 0, false });
    CHECK(chosen_piece(all, fitting(), picks(2)) == LaidPiece { 1, false });
    CHECK(chosen_piece(all, fitting(), picks(-2)) == LaidPiece { 1, true });
    CHECK_FALSE(chosen_piece(all, fitting(), picks(1)));
    TrackChoice named;
    named.named = LaidPiece { 5, false };
    CHECK(chosen_piece(all, fitting(), named) == LaidPiece { 5, false });
}

TEST_CASE("without the lift chain asked for, the piece without one is picked")
{
    const auto all = pieces();
    CHECK(chosen_piece(all, fitting(), picks(0, 1)) == LaidPiece { 2, false });
    CHECK(chosen_piece(all, fitting(), picks(0, 1, 0, true)) == LaidPiece { 3, false });
    // A chain asked for where there is none falls back to the plain piece.
    CHECK(chosen_piece(all, fitting(), picks(0, 0, 0, true)) == LaidPiece { 0, false });
}

TEST_CASE("picking a value keeps it and moves the other picks to the nearest that have a piece")
{
    const auto all = pieces();
    const TrackChoice curved = picks(2);
    const TrackChoice climbing = pick_value(all, fitting(), curved, ChoiceAxis::Slope, 1);
    CHECK(climbing.slope == 1);
    CHECK(climbing.curve == 0);
    const TrackChoice banked = pick_value(all, fitting(), climbing, ChoiceAxis::Bank, 1);
    CHECK(banked.bank == 1);
    CHECK(banked.slope == 0);
    // Picks that already have a piece stay as they are.
    CHECK(pick_value(all, fitting(), { }, ChoiceAxis::Curve, -2) == picks(-2));
}

TEST_CASE("picking a curve, slope or bank drops a piece picked by name")
{
    const auto all = pieces();
    TrackChoice named;
    named.named = LaidPiece { 5, false };
    CHECK_FALSE(pick_value(all, fitting(), named, ChoiceAxis::Curve, 0).named);
}

TEST_CASE("the lift chain turns over only when the picked shape comes the other way")
{
    const auto all = pieces();
    CHECK(pick_chain(all, fitting(), picks(0, 1)).chain);
    CHECK_FALSE(pick_chain(all, fitting(), picks(0, 1, 0, true)).chain);
    CHECK_FALSE(pick_chain(all, fitting(), { }).chain);
}

TEST_CASE("after a new open end the picks settle on the nearest that have a piece")
{
    const auto all = pieces();
    const std::vector<LaidPiece> straight_only { { 0, false } };
    TrackChoice c = picks(2, 1);
    c.named = LaidPiece { 5, false };
    const TrackChoice settled = settle_choice(all, straight_only, c);
    CHECK(settled.curve == 0);
    CHECK(settled.slope == 0);
    CHECK_FALSE(settled.named);
    // Nothing fitting leaves the picks alone.
    CHECK(settle_choice(all, { }, picks(2)).curve == 2);
}

TEST_CASE("with no plain piece offered, the station is picked before other special pieces")
{
    const std::vector<PieceShape> all = pieces();
    const std::vector<LaidPiece> tower_base { { 5, false }, { 6, false } };
    const TrackChoice settled = settle_choice(all, tower_base, picks(0));
    REQUIRE(settled.named);
    CHECK(settled.named->index == 6);
    CHECK(chosen_piece(all, tower_base, settled)->index == 6);
}
