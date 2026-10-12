// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride model tests. Every save here is built in code with invented values and invented asset names.
#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "save/model/rides.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

using namespace rct3::save;

namespace {

struct Bytes {
  std::vector<std::byte> v;
  Bytes& put(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    v.insert(v.end(), b, b + n);
    return *this;
  }
  Bytes& u8(std::uint8_t x) { return put(&x, 1); }
  Bytes& u16(std::uint16_t x) { return put(&x, 2); }
  Bytes& u32(std::uint32_t x) { return put(&x, 4); }
  Bytes& u64(std::uint64_t x) { return put(&x, 8); }
  Bytes& f32(float x) { return put(&x, 4); }
  Bytes& cat(const Bytes& o) {
    v.insert(v.end(), o.v.begin(), o.v.end());
    return *this;
  }
  Bytes& name(const std::string& s) {
    u16(static_cast<std::uint16_t>(s.size()));
    for (const char c : s) u8(static_cast<std::uint8_t>(c));
    return *this;
  }
  Bytes& blob(const Bytes& payload) {
    u32(static_cast<std::uint32_t>(payload.v.size()));
    return cat(payload);
  }
};

struct Attr {
  std::string name, type;
  std::uint32_t size = 0;
  std::vector<Attr> children;
};

Attr i32(const char* n) { return {n, "int32", 4, {}}; }
Attr u32(const char* n) { return {n, "uint32", 4, {}}; }
Attr f32(const char* n) { return {n, "float32", 4, {}}; }
Attr flag(const char* n) { return {n, "bool", 1, {}}; }
Attr ref(const char* n) { return {n, "reference", 8, {}}; }
Attr str(const char* n) { return {n, "string", 0, {}}; }
Attr vec3(const char* n) { return {n, "vector3", 12, {}}; }
Attr ref_list(const char* n) { return {n, "list", 0, {{"r", "reference", 8, {}}}}; }
Attr int_list(const char* n) { return {n, "list", 0, {{"n", "int32", 4, {}}}}; }
Attr structure(const char* n, std::vector<Attr> kids) {
  std::uint32_t size = 0;
  for (const auto& k : kids) {
    if (k.size == 0) {
      size = 0;
      break;
    }
    size += k.size;
  }
  return {n, "struct", size, std::move(kids)};
}
Attr colours(const char* n) { return structure(n, {i32("COL0"), i32("COL1"), i32("COL2")}); }

void write_attr(Bytes& b, const Attr& a) {
  b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
  for (const auto& c : a.children) write_attr(b, c);
}

// Default bytes for an attribute nobody set: zeros, empty lists, empty strings.
void write_default(Bytes& b, const Attr& a) {
  if (a.size > 0) {
    for (std::uint32_t i = 0; i < a.size; ++i) b.u8(0);
  } else if (a.type == "string") {
    Bytes p;
    p.u32(0xEFEFEFEFu);
    b.blob(p);
  } else if (a.type == "list" || a.type == "array") {
    Bytes p;
    p.u32(0);
    b.blob(p);
  } else {
    b.u32(0);
  }
}

// An object under construction: attribute values by name (already encoded as the attribute's payload).
struct Obj {
  std::string cls;
  std::uint64_t id;
  std::map<std::string, Bytes> set;
  Obj& i(const std::string& n, std::int32_t x) {
    set[n] = Bytes().u32(static_cast<std::uint32_t>(x));
    return *this;
  }
  Obj& u(const std::string& n, std::uint32_t x) {
    set[n] = Bytes().u32(x);
    return *this;
  }
  Obj& f(const std::string& n, float x) {
    set[n] = Bytes().f32(x);
    return *this;
  }
  Obj& b(const std::string& n, bool x) {
    set[n] = Bytes().u8(x ? 1 : 0);
    return *this;
  }
  Obj& r(const std::string& n, std::uint64_t x) {
    set[n] = Bytes().u64(x);
    return *this;
  }
  Obj& s(const std::string& n, const std::u16string& x) {
    Bytes p;
    p.u32(0xEFEFEFEFu);
    for (const auto c : x) p.u16(c);
    set[n] = Bytes().blob(p);
    return *this;
  }
  Obj& refs(const std::string& n, const std::vector<std::uint64_t>& ids) {
    Bytes p;
    p.u32(static_cast<std::uint32_t>(ids.size()));
    for (const auto x : ids) p.u64(x);
    set[n] = Bytes().blob(p);
    return *this;
  }
  Obj& ints(const std::string& n, const std::vector<std::int32_t>& xs) {
    Bytes p;
    p.u32(static_cast<std::uint32_t>(xs.size()));
    for (const auto x : xs) p.u32(static_cast<std::uint32_t>(x));
    set[n] = Bytes().blob(p);
    return *this;
  }
  Obj& raw(const std::string& n, Bytes x) {
    set[n] = std::move(x);
    return *this;
  }
};

struct Schema {
  std::map<std::string, std::vector<Attr>> classes;
};

struct World {
  std::vector<std::byte> bytes;
  SaveFile file;
  std::unique_ptr<ObjectIndex> index;
  RideModel model;
};

std::unique_ptr<World> build(const Schema& schema, const std::vector<Obj>& objects) {
  auto w = std::make_unique<World>();
  Bytes b;
  std::vector<std::string> order;
  b.u32(static_cast<std::uint32_t>(schema.classes.size()));
  for (const auto& [name, attrs] : schema.classes) {
    order.push_back(name);
    b.name(name).u32(static_cast<std::uint32_t>(attrs.size()));
    for (const auto& a : attrs) write_attr(b, a);
  }
  b.u32(static_cast<std::uint32_t>(objects.size()));
  for (const auto& o : objects) {
    std::uint32_t ci = 0;
    for (std::uint32_t k = 0; k < order.size(); ++k) {
      if (order[k] == o.cls) ci = k;
    }
    b.u32(ci).u64(o.id);
    for (const auto& a : schema.classes.at(o.cls)) {
      const auto it = o.set.find(a.name);
      if (it != o.set.end()) b.cat(it->second);
      else write_default(b, a);
    }
  }
  w->bytes = std::move(b.v);
  auto file = read_save(w->bytes);
  REQUIRE(file);
  w->file = std::move(*file);
  w->index = std::make_unique<ObjectIndex>(w->file);
  auto model = load_rides(w->file, *w->index);
  REQUIRE(model);
  w->model = std::move(*model);
  return w;
}

Schema ride_schema() {
  Schema s;
  const Attr stations = {"RideStations",
                         "list",
                         0,
                         {ref("Entrance"), f32("EntranceTimeRemaining"), ref("Exit"), f32("ExitTimeRemaining"),
                          ref("QueueLine"), f32("m_queueTimeSecs")}};
  const auto results = [](const char* n) {
    return structure(n, {f32("MaxSpeed"), f32("RideLength"), i32("NDrops"),
                         structure("CornerCounts", {i32("n90"), i32("n180"), i32("n270"), i32("n360")}),
                         u32("Valid")});
  };
  s.classes["TrackedRideInstance"] = {
      ref("AIDDatabaseEntry"), str("Name"), i32("State"), u32("Flags"), i32("OpMode"), i32("TicketPrice"),
      i32("Direction"), ref("Track"), ref_list("Trains"), i32("NTrains"), i32("NCarsPerTrain"),
      str("TrackedRideSymbolName"), str("TrackedRideOverlayName"), stations, ref("RideStationsObj"),
      structure("RideAssessmentResults",
                {flag("AssessmentIsValid"), vec3("AssessmentRatings"), f32("AssessmentReliability"),
                 f32("AssessmentUnderCover"), f32("AssessmentValue")}),
      structure("TrackedRideStats", {structure("TrackedRideTesting", {results("Results"),
                                                                     results("SavedTrackedRideTestResults")})}),
      int_list("m_downTimeCounters"), f32("m_reliability"), f32("FixedSpeed"), i32("MinWaitTime"),
      i32("m_breakdownType"), {"GraphSpeed", "graphedValue", 0, {}},
      colours("CarFlexiColours"), ref_list("BlockSections"), int_list("SortedTrains"),
      {"RideSeats", "list", 0, {i32("TrainCarNo"), i32("TrainCarSeatNo"), i32("TrainNo")}},
      {"IntervalData", "list", 0, {i32("GuestsPerInterval"), i32("IncomePerInterval"), i32("LossesPerInterval")}},
  };
  s.classes["RideStations"] = {stations};
  s.classes["QueueLine"] = {ref("HeadTile"), i32("QueueSize"), ref("RideInstance"), i32("StationIndex")};
  s.classes["PathQueue"] = {ref("QueueLine")};
  s.classes["Track"] = {i32("Direction"), ref("FirstSegment"), flag("IsCircuit"), ref("LastSegment"),
                        flag("Prototype"), colours("TrackFlexiColours"), ref("TrackedRideInstance")};
  s.classes["TrackSegment"] = {ref("Track"), ref("FirstPiece"), ref("LastPiece"), ref("NextSegment"),
                               ref("PrevSegment")};
  s.classes["TrackPiece"] = {
      str("SYMBOLNAME"), ref("SIDDatabaseEntry"), ref("SceneryItem"),
      structure("SceneryItemDataField", {i32("POSX"), i32("POSZ"), i32("HEIGHT"), i32("DIRECTION"), i32("CORNER")}),
      flag("Reversed"), f32("StartDistance"), f32("StartDistanceBackwardsSpline"), colours("FlexiColourField"),
      i32("BrakeSpeed"), i32("HoldingTime"), ref("Next"), ref("Prev"), ref("Segment"), ref("PlatformPiece"),
  };
  s.classes["TrackPlatformPiece"] = {ref("TrackPiece"), u32("Flags"),
                                     structure("SceneryItemDataField", {i32("POSX"), i32("POSZ"), i32("HEIGHT"),
                                                                        i32("DIRECTION"), i32("CORNER")})};
  s.classes["RideTrainInstance"] = {
      ref("TrackedRideInstance"), ref_list("Cars"), i32("WhichTrain"), i32("State"), f32("StateTime"),
      f32("Distance"), f32("Speed"), i32("Station"), str("RideTrainSymbolName"), ref("FrontTrackPiece"),
      f32("Mass"),
  };
  s.classes["RideCarInstance"] = {
      ref("RideTrainInstance"), i32("WhichCar"), i32("NCars"), i32("WhichRideTrainCar"), i32("State"),
      i32("NumPeepsInCar"),
      structure("Stats", {f32("Distance"), f32("Speed"), vec3("Pos"), flag("PosValid"), ref("TrackPiece")}),
  };
  s.classes["StallInstance"] = {ref("AIDDatabaseEntry"), str("Name"), i32("State"), i32("TotalProfit"),
                                str("StallSymbolName"), ref("Scenery"), i32("Direction"),
                                ref_list("ShopItems"), ref_list("ShopExtras")};
  s.classes["ShopItemInstance"] = {ref("CIDEntry"), i32("Price"), i32("TotalSold"), str("Name"), ref("Stall"),
                                   ref("SaleItem"), i32("FlexiColour0")};
  s.classes["ShopExtraInstance"] = {ref("CIDEntry"), i32("Setting")};
  return s;
}

// Ids used by the fixtures.
constexpr std::uint64_t kRide = 10, kTrack = 11, kSeg = 12, kA = 20, kB = 21, kC = 22, kStale = 9999;

Obj piece(std::uint64_t id, const char16_t* sym, std::uint64_t next, std::uint64_t prev, int x, float dist) {
  Obj p{"TrackPiece", id, {}};
  p.s("SYMBOLNAME", sym).r("Next", next).r("Prev", prev).r("Segment", kSeg).f("StartDistance", dist);
  Bytes at;
  at.u32(static_cast<std::uint32_t>(x)).u32(7).u32(static_cast<std::uint32_t>(-3)).u32(2).u32(1);
  p.raw("SceneryItemDataField", at);
  return p;
}

std::vector<Obj> basic_track(std::uint64_t a_prev, std::uint64_t c_next) {
  Obj ride{"TrackedRideInstance", kRide, {}};
  ride.r("Track", kTrack).s("Name", u"Test Ride").i("State", 2).i("TicketPrice", 250).i("OpMode", 5);
  Obj track{"Track", kTrack, {}};
  track.r("FirstSegment", kSeg).r("LastSegment", kSeg).r("TrackedRideInstance", kRide).b("IsCircuit", false);
  Obj seg{"TrackSegment", kSeg, {}};
  seg.r("Track", kTrack).r("FirstPiece", kA).r("LastPiece", kC);
  return {ride, track, seg, piece(kC, u"Stub:tks", c_next, kB, 12, 8.0f), piece(kA, u"Straight:tks", kB, a_prev, 10, 0.0f),
          piece(kB, u"Curve:tks", kC, kA, 11, 4.0f)};
}

}  // namespace

TEST_CASE("an open three piece track loads in travel order") {
  const auto w = build(ride_schema(), basic_track(kStale, kStale));
  REQUIRE(w->model.rides.size() == 1);
  const auto& r = w->model.rides[0];
  CHECK(r.kind == RideKind::Tracked);
  CHECK(r.base.name == "Test Ride");
  CHECK(r.base.state == AttractionState::Open);
  CHECK(r.mode == OperatingMode::Shuttle);
  CHECK(r.ticket_price == 250);  // hundredths
  REQUIRE(r.track.has_value());
  const auto& t = *r.track;
  REQUIRE(t.pieces.size() == 3);
  CHECK(t.pieces[0].id == kA);
  CHECK(t.pieces[1].id == kB);
  CHECK(t.pieces[2].id == kC);
  CHECK(t.pieces[0].symbol == "Straight:tks");
  CHECK(t.pieces[0].tile_x == 10);
  CHECK(t.pieces[0].tile_z == 7);
  CHECK(t.pieces[0].height == -3);
  CHECK(t.pieces[0].direction == 2);
  CHECK(t.pieces[0].corner == 1);
  CHECK(t.pieces[2].start_distance == 8.0f);
  CHECK_FALSE(t.circuit);
  CHECK_FALSE(t.truncated);
  REQUIRE(t.segments.size() == 1);
  CHECK(t.segments[0].piece_count == 3);
  CHECK(t.segments[0].owned_pieces == 3);
  const auto& d = w->model.diagnostics;
  CHECK(d.track_back_link_mismatch == 0);
  CHECK(d.truncated_tracks == 0);
  CHECK(d.open_ends_with_live_link == 0);
  CHECK(w->model.orphan_pieces == 0);
}

TEST_CASE("a closed circuit is derived from the chain, not the stored flag") {
  auto objs = basic_track(kC, kA);  // last links to first, first links back
  const auto w = build(ride_schema(), objs);
  const auto& t = *w->model.rides[0].track;
  CHECK(t.circuit);  // IsCircuit is stored false in the fixture
  CHECK(t.segments[0].closed);
  CHECK_FALSE(t.truncated);
}

TEST_CASE("only one end linking back is not a circuit") {
  const auto w = build(ride_schema(), basic_track(kStale, kA));
  const auto& t = *w->model.rides[0].track;
  CHECK_FALSE(t.circuit);
  CHECK(w->model.diagnostics.open_ends_with_live_link == 1);
}

TEST_CASE("a cycle in the next chain terminates and marks the track truncated") {
  auto objs = basic_track(kStale, kB);  // C -> B -> C ...
  // Make C no longer the segment's last piece: point LastPiece at an id that is never reached.
  for (auto& o : objs) {
    if (o.cls == "TrackSegment") o.r("LastPiece", 777);
  }
  const auto w = build(ride_schema(), objs);
  const auto& t = *w->model.rides[0].track;
  CHECK(t.pieces.size() == 3);
  CHECK(t.truncated);
  CHECK_FALSE(t.circuit);
  CHECK(w->model.diagnostics.truncated_tracks == 1);
}

TEST_CASE("a self linking piece does not loop forever") {
  auto objs = basic_track(kStale, kStale);
  for (auto& o : objs) {
    if (o.cls == "TrackPiece" && o.id == kA) o.r("Next", kA);
    if (o.cls == "TrackSegment") o.r("LastPiece", 777);
  }
  const auto w = build(ride_schema(), objs);
  const auto& t = *w->model.rides[0].track;
  CHECK(t.pieces.size() == 1);
  CHECK(t.truncated);
}

TEST_CASE("pieces no track reaches are counted as orphans") {
  auto objs = basic_track(kStale, kStale);
  objs.push_back(piece(40, u"Lost:tks", kStale, kStale, 1, 0));
  objs.back().r("Segment", kStale);
  const auto w = build(ride_schema(), objs);
  CHECK(w->model.orphan_pieces == 1);
  CHECK(w->model.rides[0].track->pieces.size() == 3);
}

TEST_CASE("missing attributes take defaults") {
  Schema s;
  s.classes["TrackedRideInstance"] = {str("Name")};
  Obj r{"TrackedRideInstance", 5, {}};
  r.s("Name", u"Bare");
  const auto w = build(s, {r});
  REQUIRE(w->model.rides.size() == 1);
  const auto& ride = w->model.rides[0];
  CHECK(ride.base.name == "Bare");
  CHECK(ride.base.state == AttractionState::Closed);
  CHECK(ride.options.fixed_speed == -1);
  CHECK(ride.options.min_wait == -1);
  CHECK(ride.breakdown.reliability == 100);
  CHECK(ride.breakdown.station == -1);
  CHECK(ride.base.time_since_crash == -1);
  CHECK(ride.train_count == -1);
  CHECK_FALSE(ride.track.has_value());
  CHECK(ride.stations.empty());
  CHECK_FALSE(ride.stats.last_lap.has_value());
}

TEST_CASE("uninitialised fill is masked or defaulted") {
  Obj r{"TrackedRideInstance", 5, {}};
  Bytes assess;
  assess.u8(0).f32(std::bit_cast<float>(0xCCCCCCCCu)).f32(std::bit_cast<float>(0xCCCCCCCCu))
      .f32(std::bit_cast<float>(0xCCCCCCCCu)).f32(std::bit_cast<float>(0xCCCCCCCCu))
      .f32(std::bit_cast<float>(0xCCCCCCCCu)).f32(std::bit_cast<float>(0xCCCCCCCCu));
  r.raw("RideAssessmentResults", assess);
  // Last lap: valid word has junk in the upper bytes and bit 0 set. Current lap: junk but bit 0 clear.
  const auto results = [](float speed, std::uint32_t valid) {
    Bytes b;
    b.f32(speed).f32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(valid);
    return b;
  };
  Bytes testing;
  testing.cat(results(std::bit_cast<float>(0xCCCCCCCCu), 0x6EF300u)).cat(results(12.5f, 0x6EF301u));
  r.raw("TrackedRideStats", testing);
  const auto w = build(ride_schema(), {r});
  const auto& ride = w->model.rides[0];
  CHECK_FALSE(ride.assessment.valid);
  CHECK(ride.assessment.excitement == 0);
  CHECK(ride.assessment.value == 0);
  REQUIRE(ride.stats.current_lap.has_value());
  CHECK(ride.stats.current_lap->max_speed == 0);  // fill pattern read as no data
  CHECK_FALSE(ride.stats.current_lap->valid);
  REQUIRE(ride.stats.last_lap.has_value());
  CHECK(ride.stats.last_lap->max_speed == 12.5f);
  CHECK(ride.stats.last_lap->valid);
}

TEST_CASE("stale references load as none") {
  auto objs = basic_track(kStale, kStale);
  objs[0].r("Track", kStale);
  objs[0].refs("Trains", {kStale, 0});
  objs[0].refs("BlockSections", {kStale, kStale});
  const auto w = build(ride_schema(), objs);
  const auto& r = w->model.rides[0];
  CHECK(r.track_id == 0);
  CHECK_FALSE(r.track.has_value());
  CHECK(r.trains.empty());
  CHECK(w->model.diagnostics.unresolved_tracks == 1);
  CHECK(w->model.diagnostics.unresolved_trains == 1);  // the 0 entry is silently none
  REQUIRE(r.block_sections.size() == 2);
  CHECK(r.block_sections[0] == 0);
  CHECK(w->model.orphan_pieces == 3);
}

TEST_CASE("trains and cars keep their stored indices and unknown states") {
  auto objs = basic_track(kStale, kStale);
  objs[0].refs("Trains", {50, 51});
  Obj t0{"RideTrainInstance", 50, {}};
  t0.r("TrackedRideInstance", kRide).refs("Cars", {60, 61, 62}).i("WhichTrain", 0).i("State", 69).f("Distance", 3.5f);
  t0.s("RideTrainSymbolName", u"Invented:rit").r("FrontTrackPiece", kB);
  Obj t1{"RideTrainInstance", 51, {}};
  t1.r("TrackedRideInstance", kRide).refs("Cars", {63}).i("WhichTrain", 1).i("State", 22);
  const auto car = [](std::uint64_t id, std::uint64_t train, int which, int count, int slot) {
    Obj c{"RideCarInstance", id, {}};
    c.r("RideTrainInstance", train).i("WhichCar", which).i("NCars", count).i("WhichRideTrainCar", slot).i("State", 1);
    return c;
  };
  objs.push_back(t0);
  objs.push_back(t1);
  objs.push_back(car(60, 50, 0, 3, 0));
  objs.push_back(car(61, 50, 1, 3, 5));
  objs.push_back(car(62, 50, 2, 3, 2));
  objs.push_back(car(63, 51, 0, 1, 0));
  const auto w = build(ride_schema(), objs);
  const auto& r = w->model.rides[0];
  REQUIRE(r.trains.size() == 2);
  CHECK(static_cast<int>(r.trains[0].state) == 69);  // unknown value survives
  CHECK(r.trains[1].state == TrainState::StoppingAtStation);
  CHECK(r.trains[0].symbol == "Invented:rit");
  CHECK(r.trains[0].front_piece == kB);
  CHECK(r.trains[0].distance == 3.5f);
  REQUIRE(r.trains[0].cars.size() == 3);
  CHECK(r.trains[0].cars[1].design_slot == 5);
  CHECK(r.trains[0].cars[2].index == 2);
  CHECK(r.trains[1].cars.size() == 1);
  const auto& d = w->model.diagnostics;
  CHECK(d.train_index_mismatch == 0);
  CHECK(d.car_index_mismatch == 0);
  CHECK(d.car_count_mismatch == 0);
  CHECK(d.train_back_link_mismatch == 0);
  CHECK(d.car_back_link_mismatch == 0);
}

TEST_CASE("index and count mismatches are reported, not fatal") {
  auto objs = basic_track(kStale, kStale);
  objs[0].refs("Trains", {50});
  Obj t0{"RideTrainInstance", 50, {}};
  t0.r("TrackedRideInstance", 77).refs("Cars", {60, 61}).i("WhichTrain", 3);
  Obj c0{"RideCarInstance", 60, {}};
  c0.r("RideTrainInstance", 50).i("WhichCar", 0).i("NCars", 2);
  Obj c1{"RideCarInstance", 61, {}};
  c1.r("RideTrainInstance", 50).i("WhichCar", 5).i("NCars", 9);
  objs.push_back(t0);
  objs.push_back(c0);
  objs.push_back(c1);
  const auto w = build(ride_schema(), objs);
  const auto& d = w->model.diagnostics;
  CHECK(d.train_index_mismatch == 1);
  CHECK(d.train_back_link_mismatch == 1);
  CHECK(d.car_index_mismatch == 1);
  CHECK(d.car_count_mismatch == 1);
  CHECK(w->model.rides[0].trains[0].cars.size() == 2);
}

TEST_CASE("an object listed many times is attached once") {
  auto objs = basic_track(kStale, kStale);
  objs[0].refs("Trains", std::vector<std::uint64_t>(200, 50));
  Obj t0{"RideTrainInstance", 50, {}};
  t0.r("TrackedRideInstance", kRide).refs("Cars", std::vector<std::uint64_t>(100, 60));
  Obj c0{"RideCarInstance", 60, {}};
  c0.r("RideTrainInstance", 50);
  objs.push_back(t0);
  objs.push_back(c0);
  const auto w = build(ride_schema(), objs);
  CHECK(w->model.rides[0].trains.size() == 1);
  CHECK(w->model.rides[0].trains[0].cars.size() == 1);
  CHECK(w->model.diagnostics.duplicate_refs == 199 + 99);
}

TEST_CASE("hostile list counts leave the lists empty") {
  Obj r{"TrackedRideInstance", 5, {}};
  Bytes huge;
  huge.u32(0x7FFFFFFFu).u64(1);  // claims two billion entries in eight bytes of data
  r.raw("Trains", Bytes().blob(huge));
  r.raw("RideSeats", Bytes().blob(huge));
  r.raw("m_downTimeCounters", Bytes().blob(huge));
  const auto w = build(ride_schema(), {r});
  REQUIRE(w->model.rides.size() == 1);
  CHECK(w->model.rides[0].trains.empty());
  CHECK(w->model.rides[0].seats.empty());
}

TEST_CASE("stations resolve through the holder object and count queue tiles") {
  auto objs = basic_track(kStale, kStale);
  objs[0].r("RideStationsObj", 70);
  const auto station = [](std::uint64_t entrance, std::uint64_t line) {
    Bytes b;
    b.u64(entrance).f32(1.5f).u64(kStale).f32(0).u64(line).f32(30.0f);
    return b;
  };
  Bytes list;
  list.u32(2).cat(station(80, 71)).cat(station(kStale, 72));
  Obj holder{"RideStations", 70, {}};
  holder.raw("RideStations", Bytes().blob(list));
  Obj entrance{"TrackedRideInstance", 80, {}};  // any live object works as the placement target
  Obj q1{"QueueLine", 71, {}};
  q1.i("QueueSize", 2).r("RideInstance", kRide).i("StationIndex", 0);
  Obj q2{"QueueLine", 72, {}};
  q2.i("QueueSize", 3).i("StationIndex", 1);
  for (const std::uint64_t tid : {200u, 201u}) {
    Obj t{"PathQueue", tid, {}};
    t.r("QueueLine", 71);
    objs.push_back(t);
  }
  Obj t3{"PathQueue", 202, {}};
  t3.r("QueueLine", 72);
  objs.push_back(holder);
  objs.push_back(entrance);
  objs.push_back(q1);
  objs.push_back(q2);
  objs.push_back(t3);
  const auto w = build(ride_schema(), objs);
  const auto& r = w->model.rides[0];
  REQUIRE(r.stations.size() == 2);
  CHECK(r.stations[0].entrance == 80);
  CHECK(r.stations[0].exit == 0);  // stale
  CHECK(r.stations[0].entrance_timer == 1.5f);
  CHECK(r.stations[0].queue_line == 71);
  CHECK(r.stations[0].queue_size == 2);
  CHECK(r.stations[0].queue_tiles == 2);
  CHECK(r.stations[0].queue_time == 30.0f);
  CHECK(r.stations[1].entrance == 0);
  CHECK(r.stations[1].queue_size == 3);
  CHECK(r.stations[1].queue_tiles == 1);
  CHECK(w->model.diagnostics.queue_size_mismatch == 1);
}

TEST_CASE("station platforms attach to their pieces") {
  auto objs = basic_track(kStale, kStale);
  for (auto& o : objs) {
    if (o.cls == "TrackPiece" && o.id == kB) o.r("PlatformPiece", 90);
  }
  Obj pl{"TrackPlatformPiece", 90, {}};
  pl.r("TrackPiece", kB).u("Flags", 5);
  Bytes at;
  at.u32(11).u32(7).u32(0).u32(1).u32(0);
  pl.raw("SceneryItemDataField", at);
  objs.push_back(pl);
  const auto w = build(ride_schema(), objs);
  const auto& t = *w->model.rides[0].track;
  REQUIRE(t.pieces[1].platform.has_value());
  CHECK(t.pieces[1].platform->flags == 5);
  CHECK(t.pieces[1].platform->tile_x == 11);
  CHECK_FALSE(t.pieces[0].platform.has_value());
  CHECK(w->model.diagnostics.platform_back_link_mismatch == 0);
}

TEST_CASE("downtime windows give a percentage") {
  Obj r{"TrackedRideInstance", 5, {}};
  r.ints("m_downTimeCounters", {0, 20, 20, 20, 7, 0, 0, 99});
  r.f("m_reliability", 62.5f);
  r.i("m_breakdownType", 3).i("FixedSpeed", 0);  // FixedSpeed is a float field here; integer bytes read as tiny
  const auto w = build(ride_schema(), {r});
  const auto& b = w->model.rides[0].breakdown;
  CHECK(b.downtime_windows[7] == 20);  // clamped
  CHECK(b.downtime_percent() == doctest::Approx((20 + 20 + 20 + 7 + 20) * 100.0 / 160.0));
  CHECK(b.reliability == 62.5f);
  CHECK(b.current == BreakdownKind::RestraintsFailToClose);
}

TEST_CASE("stalls load items and extras with prices in hundredths") {
  Obj stall{"StallInstance", 300, {}};
  stall.s("Name", u"Shop").i("State", 2).i("TotalProfit", -80).s("StallSymbolName", u"Invented:sta");
  stall.r("Scenery", 310).i("Direction", 3).refs("ShopItems", {301, 302, kStale}).refs("ShopExtras", {303});
  Obj i1{"ShopItemInstance", 301, {}};
  i1.r("CIDEntry", 310).i("Price", 120).i("TotalSold", 4).s("Name", u"%ME%").r("Stall", 300);
  Obj i2{"ShopItemInstance", 302, {}};
  i2.r("Stall", 12345).i("Price", 50).i("FlexiColour0", 9);
  Obj e1{"ShopExtraInstance", 303, {}};
  e1.i("Setting", 2).r("CIDEntry", 310);
  Obj placement{"TrackedRideInstance", 310, {}};  // any live object
  const auto w = build(ride_schema(), {stall, i1, i2, e1, placement});
  REQUIRE(w->model.stalls.size() == 1);
  const auto& s = w->model.stalls[0];
  CHECK(s.base.name == "Shop");
  CHECK(s.base.symbol == "Invented:sta");
  CHECK(s.base.total_profit == -80);
  CHECK(s.base.facing == 3);
  CHECK(s.base.placement == 310);
  REQUIRE(s.items.size() == 2);
  CHECK(s.items[0].price == 120);
  CHECK(s.items[0].total_sold == 4);
  CHECK(s.items[0].item_asset == 310);
  CHECK(s.items[1].colours[0] == 9);
  REQUIRE(s.extras.size() == 1);
  CHECK(s.extras[0].level == 2);
  CHECK(w->model.diagnostics.item_back_link_mismatch == 1);
  CHECK(w->model.diagnostics.unresolved_items == 1);
}

TEST_CASE("a save without any rides loads an empty model") {
  Schema s;
  s.classes["Other"] = {i32("x")};
  const auto w = build(s, {Obj{"Other", 1, {}}});
  CHECK(w->model.rides.empty());
  CHECK(w->model.stalls.empty());
}
