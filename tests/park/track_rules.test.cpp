// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_rules.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// A tracked ride of `pieces` pieces in one part, with `stations` stations that have their doors.
    save::Ride ride(std::size_t pieces, std::size_t stations = 1, bool circuit = true)
    {
        save::Ride r;
        r.kind = save::RideKind::Tracked;
        r.track.emplace();
        r.track->pieces.resize(pieces);
        r.track->segments.resize(1);
        r.track->circuit = circuit;
        r.stations.resize(stations, save::Station { .entrance = 1, .exit = 2 });
        return r;
    }

    PieceShape piece(std::string name, std::string group = { }, std::uint32_t flags = 0)
    {
        PieceShape s;
        s.record.name = std::move(name);
        s.record.group = std::move(group);
        s.record.flags = flags;
        return s;
    }

    PieceTest named(std::string name)
    {
        return { .kind = PieceTest::Kind::Piece, .name = std::move(name) };
    }

    PieceTest in_group(std::string name, bool negate = false)
    {
        return { .kind = PieceTest::Kind::Group, .name = std::move(name), .negate = negate };
    }

    /// Default rules changed by `change`.
    TrackRules rules_with(auto change)
    {
        TrackRules r;
        change(r);
        return r;
    }

    constexpr auto kOpen = save::AttractionState::Open;
    constexpr auto kTest = save::AttractionState::Testing;
} // namespace

TEST_CASE("a ride opens when its track and doors keep to its rules, and says why not first")
{
    const TrackRules rules;
    CHECK_FALSE(open_refusal(ride(6), rules, kOpen, false));
    CHECK_FALSE(open_refusal(ride(0, 0), rules, save::AttractionState::Closed, false));

    save::Ride split = ride(6, 0);
    split.track->segments.resize(2);
    CHECK(open_refusal(split, rules, kOpen, false) == "TrackedRideIsDisjoint");
    CHECK(open_refusal(split, rules_with([](TrackRules& r) { r.may_be_split = true; }), kOpen, false) == "RideHasNoStation");
    CHECK(open_refusal(ride(2), rules_with([](TrackRules& r) { r.min_pieces = 3; }), kTest, false) == "CannotOpenOrTestInsufficientPieces");
    CHECK(open_refusal(ride(6, 1, false), rules_with([](TrackRules& r) { r.circuit_only = true; }), kOpen, false) == "CannotOpenOrTestNotCircuit");
    CHECK(open_refusal(ride(6), rules_with([](TrackRules& r) { r.never_tested = true; }), kTest, false) == "CannotTestTrackedRide");
    CHECK_FALSE(open_refusal(ride(6), rules_with([](TrackRules& r) { r.never_tested = true; }), kOpen, false));
    // Built while the panel has it, it may be tested but not opened.
    CHECK(open_refusal(ride(6), rules, kOpen, true) == "CannotOpenTrackedRideWhileEditing");
    CHECK_FALSE(open_refusal(ride(6), rules, kTest, true));
}

TEST_CASE("entrances and exits are counted as the ride's door rules say")
{
    save::Ride two = ride(6, 2);
    two.stations[1] = { };
    // At least one: the first station's do.
    CHECK_FALSE(open_refusal(two, TrackRules { }, kOpen, false));
    // One per station.
    CHECK(open_refusal(two, rules_with([](TrackRules& r) { r.entrances = 0; }), kOpen, false) == "CantOpenRideMissingEntrance");
    CHECK(open_refusal(two, rules_with([](TrackRules& r) { r.exits = 0; }), kTest, false) == "CantTestRideMissingExit");
    // None anywhere.
    save::Ride bare = ride(6, 1);
    bare.stations[0] = { .entrance = 1 };
    CHECK(open_refusal(bare, TrackRules { }, kOpen, false) == "CantOpenRideNoExit");
    CHECK_FALSE(open_refusal(bare, rules_with([](TrackRules& r) { r.exits = 1; }), kOpen, false));
    CHECK(open_refusal(bare, rules_with([](TrackRules& r) { r.exits = kOneDoor; }), kTest, false) == "CantTestRideNoExit");
}

TEST_CASE("patterns swap a station run's ends for its own end pieces")
{
    // Middles are what the player lays; a middle with no station piece after it becomes the end, and the
    // end becomes a middle again once more station follows.
    const std::vector<PieceShape> pieces { piece("Middle", "station"), piece("End", "station"), piece("Straight") };
    const std::vector<PiecePattern> patterns {
        { { named("Middle"), named("End") }, { in_group("station", true), { } } },
        { { [] {
               auto t = named("Middle");
               t.last = true;
               return t;
           }(),
            named("End") } },
        { { named("End"), named("Middle") }, { in_group("station"), { } } },
    };
    using L = LaidPiece;
    CHECK(apply_patterns(pieces, { L { 0 }, L { 0 }, L { 2 } }, patterns, false) == std::vector<L> { { 0 }, { 1 }, { 2 } });
    CHECK(apply_patterns(pieces, { L { 2 }, L { 0 } }, patterns, false) == std::vector<L> { { 2 }, { 1 } });
    CHECK(apply_patterns(pieces, { L { 0 }, L { 1 }, L { 0 } }, patterns, false) == std::vector<L> { { 0 }, { 0 }, { 1 } });
    // On a circuit nothing is last.
    CHECK(apply_patterns(pieces, { L { 2 }, L { 0 } }, patterns, true) == std::vector<L> { { 2 }, { 0 } });
}

TEST_CASE("a pattern lays its piece backwards when asked, unless the piece keeps its way")
{
    PieceTest back = named("Start");
    back.backward = true;
    const std::vector<PiecePattern> patterns { { { named("Middle"), back } } };
    std::vector<PieceShape> pieces { piece("Middle"), piece("Start") };
    CHECK(apply_patterns(pieces, { LaidPiece { 0 } }, patterns, false) == std::vector<LaidPiece> { { 1, true } });
    pieces[1].record.flags = kPieceKeepsWay;
    CHECK(apply_patterns(pieces, { LaidPiece { 0 } }, patterns, false) == std::vector<LaidPiece> { { 0 } });
}
