// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_rules.hpp"

#include "ovl/tracked_ride.hpp"

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

TEST_CASE("an unfinished track with a station tests and opens, as one stretch; without a station, or in two, it does not")
{
    using K = CourseKind;
    TrackCourse course;
    course.points.resize(3);
    course.distance = { 0, 6, 20 };
    course.kinds = { K::Station, K::Plain, K::Plain };
    course.closed = false;
    const save::Ride r = ride(4, 1, false);
    CHECK_FALSE(open_refusal(r, TrackRules { }, kTest, false, course));
    CHECK_FALSE(open_refusal(r, TrackRules { }, kOpen, false, course));
    // Tested while it is built; opened only once it is not.
    CHECK_FALSE(open_refusal(r, TrackRules { }, kTest, true, course));
    CHECK(open_refusal(r, TrackRules { }, kOpen, true, course) == "CannotOpenTrackedRideWhileEditing");
    CHECK(open_refusal(r, rules_with([](TrackRules& t) { t.circuit_only = true; }), kOpen, false, course) == "CannotOpenOrTestNotCircuit");
    save::Ride split = r;
    split.track->segments.resize(2);
    CHECK(open_refusal(split, TrackRules { }, kTest, false, course) == "TrackedRideIsDisjoint");
    course.kinds[0] = K::Plain;
    CHECK(open_refusal(r, TrackRules { }, kTest, false, course) == "RideHasNoStation");
}

TEST_CASE("entrances and exits are counted as the ride's door rules say")
{
    save::Ride two = ride(6, 2);
    two.stations[1] = { };
    // At least one: the first station's do.
    CHECK_FALSE(open_refusal(two, TrackRules { }, kOpen, false));
    // One per station.
    CHECK(open_refusal(two, rules_with([](TrackRules& r) { r.entrances = 0; }), kOpen, false) == "CantOpenRideMissingEntrance");
    CHECK(open_refusal(two, rules_with([](TrackRules& r) { r.exits = 0; }), kOpen, false) == "CantOpenRideMissingExit");
    // None anywhere.
    save::Ride bare = ride(6, 1);
    bare.stations[0] = { .entrance = 1 };
    CHECK(open_refusal(bare, TrackRules { }, kOpen, false) == "CantOpenRideNoExit");
    CHECK_FALSE(open_refusal(bare, rules_with([](TrackRules& r) { r.exits = 1; }), kOpen, false));
    CHECK(open_refusal(bare, rules_with([](TrackRules& r) { r.exits = kOneDoor; }), kOpen, false) == "CantOpenRideNoExit");
    // A test run needs no doors.
    CHECK_FALSE(open_refusal(bare, TrackRules { }, kTest, false));
}

TEST_CASE("a flat ride always tests, and opens once it has its doors")
{
    save::Ride flat;
    flat.kind = save::RideKind::Flat;
    flat.stations.resize(1);
    CHECK_FALSE(open_refusal(flat, TrackRules { }, kTest, false));
    CHECK(open_refusal(flat, TrackRules { }, kOpen, false) == "CantOpenRideNoEntrance");
    flat.stations[0] = { .entrance = 1, .exit = 2 };
    CHECK_FALSE(open_refusal(flat, TrackRules { }, kOpen, false));
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

TEST_CASE("a ride whose cable lift is missing does not open; one whose station is shorter than its train, or whose closed course was laid unclosed, does")
{
    using K = CourseKind;
    TrackCourse course;
    course.points.resize(4);
    course.distance = { 0, 6, 10, 30 };
    course.kinds = { K::Station, K::Plain, K::Plain, K::Plain };
    course.closed = true;
    save::Ride r = ride(6);
    r.trains.resize(1);
    r.trains[0].length = 5;
    CHECK_FALSE(open_refusal(r, TrackRules { }, kOpen, false, course));
    r.trains[0].length = 7;
    CHECK_FALSE(open_refusal(r, TrackRules { }, kTest, false, course));
    // A course that comes round to its start is a circuit, however its pieces were laid.
    r.track->circuit = false;
    CHECK_FALSE(open_refusal(r, TrackRules { .circuit_only = true }, kTest, false, course));

    // Its station pieces count before the park keeps the station.
    save::Ride bare = r;
    bare.stations.clear();
    CHECK(open_refusal(bare, TrackRules { }, kOpen, false, course) == "CantOpenRideNoEntrance");
    course.kinds[0] = K::Plain;
    CHECK(open_refusal(bare, TrackRules { }, kOpen, false, course) == "RideHasNoStation");
    course.kinds[0] = K::Station;

    r.mode = save::OperatingMode::ReverseCableLiftHill;
    r.trains[0].length = 5;
    CHECK(open_refusal(r, TrackRules { }, kOpen, false, course) == "CannotOpenOrTestNoReverseCableLiftHill");
    course.reverse_lift = true;
    CHECK_FALSE(open_refusal(r, TrackRules { }, kOpen, false, course));
}

TEST_CASE("a ride whose special pieces fill only the first two slots is built back from its station")
{
    ovl::TrackedRide stack;
    stack.track_sections = { "Top", "Middle", "", "" };
    CHECK(track_rules(stack).builds_back);
    ovl::TrackedRide coaster;
    coaster.track_sections = { "Lift", "Brake", "Station", "" };
    CHECK_FALSE(track_rules(coaster).builds_back);
    coaster.track_sections = { "", "", "", "" };
    CHECK_FALSE(track_rules(coaster).builds_back);
}

TEST_CASE("a ride its file marks as one building is set down in one piece")
{
    ovl::TrackedRide house;
    house.rules.flags = 0x410;
    CHECK(track_rules(house).one_piece);
    CHECK(track_rules(house).never_tested);
    house.rules.flags = 0x2;
    CHECK_FALSE(track_rules(house).one_piece);
}

TEST_CASE("a walk-through opens only when its exit comes after its entrance")
{
    const TrackRules walk = rules_with([](TrackRules& r) { r.entrances = r.exits = kOneDoor; });
    save::Ride r = ride(6, 2, false);
    r.stations[0] = { .entrance = 1 };
    r.stations[1] = { .exit = 2 };
    CHECK_FALSE(open_refusal(r, walk, kOpen, false));
    std::swap(r.stations[0], r.stations[1]);
    CHECK(open_refusal(r, walk, kOpen, false) == "CannotOpenExitBeforeEntrance");
    // Tested either way, and on a circuit there is no before.
    CHECK_FALSE(open_refusal(r, walk, kTest, false));
    r.track->circuit = true;
    CHECK_FALSE(open_refusal(r, walk, kOpen, false));
}
