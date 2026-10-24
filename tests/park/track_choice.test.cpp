// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_choice.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::park;

namespace
{
    constexpr std::uint8_t kChain = toggle_bit(PieceToggle::Chain);
    constexpr std::uint8_t kEnclosed = toggle_bit(PieceToggle::Enclosed);

    PieceShape piece(PieceEnd exit, int curve = 0, PieceUse use = PieceUse::Plain, std::uint8_t toggles = 0)
    {
        PieceShape s;
        s.exit = exit;
        s.curve = curve;
        s.use = use;
        s.toggles = toggles;
        return s;
    }

    /// A small track: straight, a medium right curve (left when laid backwards), a climb and the same with a
    /// lift chain, a banked straight, a station, a loop, and a climb in a tube with a lift chain.
    std::vector<PieceShape> pieces()
    {
        return {
            piece({ 0, 0, 0 }),                                         // 0
            piece({ 0, 0, 2 }, 2),                                      // 1
            piece({ 1, 0, 0 }),                                         // 2
            piece({ 1, 0, 0 }, 0, PieceUse::Plain, kChain),             // 3
            piece({ 0, 1, 0 }),                                         // 4
            piece({ 0, 0, 0 }, 0, PieceUse::Special),                   // 5
            piece({ 0, 0, 0 }, 0, PieceUse::Station),                   // 6
            piece({ 1, 0, 0 }, 0, PieceUse::Plain, kChain | kEnclosed), // 7
        };
    }

    TrackChoice picks(int curve, int slope = 0, int bank = 0, std::uint8_t toggles = 0)
    {
        TrackChoice c;
        c.curve = curve;
        c.slope = slope;
        c.bank = bank;
        c.toggles = toggles;
        return c;
    }

    /// What fits a level, unbanked tip: everything above but the curve only fits backwards as well.
    std::vector<LaidPiece> fitting()
    {
        return { { 0, false }, { 1, false }, { 1, true }, { 2, false }, { 3, false }, { 4, false }, { 5, false }, { 6, false }, { 7, false } };
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
    CHECK(o.any_toggles == (kChain | kEnclosed));
    CHECK(o.toggles == 0);
    CHECK(choice_options(all, fitting(), picks(0, 1)).toggles == (kChain | kEnclosed));
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
    CHECK(chosen_piece(all, fitting(), picks(0, 1, 0, kChain)) == LaidPiece { 3, false });
    CHECK(chosen_piece(all, fitting(), picks(0, 1, 0, kChain | kEnclosed)) == LaidPiece { 7, false });
    // A toggle asked for where no piece has it falls back to the piece with the fewest others.
    CHECK(chosen_piece(all, fitting(), picks(0, 0, 0, kChain)) == LaidPiece { 0, false });
    CHECK(chosen_piece(all, fitting(), picks(0, 1, 0, kEnclosed)) == LaidPiece { 2, false });
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

TEST_CASE("a toggle turns over only when the picked shape comes the other way")
{
    const auto all = pieces();
    CHECK(pick_toggle(all, fitting(), picks(0, 1), PieceToggle::Chain).toggles == kChain);
    CHECK(pick_toggle(all, fitting(), picks(0, 1, 0, kChain), PieceToggle::Chain).toggles == 0);
    CHECK(pick_toggle(all, fitting(), { }, PieceToggle::Chain).toggles == 0);
    CHECK(pick_toggle(all, fitting(), picks(0, 1, 0, kChain), PieceToggle::Enclosed).toggles == (kChain | kEnclosed));
    CHECK(pick_toggle(all, fitting(), picks(0, 1), PieceToggle::Water).toggles == 0);
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

TEST_CASE("wide is of the end a piece is laid to")
{
    // A piece widening the channel, which also fits laid backwards, narrowing it.
    PieceShape widening = piece({ 0, 0, 0 }, 0, PieceUse::Plain, toggle_bit(PieceToggle::Wide));
    widening.record.joins[1].width = 1;
    const std::vector<PieceShape> all { widening };
    const std::vector<LaidPiece> both { { 0, false }, { 0, true } };
    CHECK(chosen_piece(all, both, picks(0, 0, 0, toggle_bit(PieceToggle::Wide))) == LaidPiece { 0, false });
    CHECK(chosen_piece(all, both, picks(0)) == LaidPiece { 0, true });
}
