// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "save/save_file.hpp"
#include "save/value.hpp"

using namespace rct3::save;

namespace {

// Synthetic schema and object builder. Class and field names are invented; only the type tags are the
// format's own.
struct Bytes {
  std::vector<std::byte> v;
  Bytes& u8(std::uint8_t x) {
    v.push_back(static_cast<std::byte>(x));
    return *this;
  }
  Bytes& u16(std::uint16_t x) { return put(&x, 2); }
  Bytes& u32(std::uint32_t x) { return put(&x, 4); }
  Bytes& u64(std::uint64_t x) { return put(&x, 8); }
  Bytes& f32(float x) { return put(&x, 4); }
  Bytes& put(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    v.insert(v.end(), b, b + n);
    return *this;
  }
  Bytes& cat(const Bytes& o) {
    v.insert(v.end(), o.v.begin(), o.v.end());
    return *this;
  }
  Bytes& name(const std::string& s) {
    u16(static_cast<std::uint16_t>(s.size()));
    for (const char c : s) u8(static_cast<std::uint8_t>(c));
    return *this;
  }
  // A length-prefixed variable attribute payload.
  Bytes& blob(const Bytes& payload) {
    u32(static_cast<std::uint32_t>(payload.v.size()));
    return cat(payload);
  }
  Bytes& marked_text(const std::u16string& s) {
    Bytes p;
    p.u32(0xEFEFEFEFu);
    for (const auto c : s) p.u16(c);
    return blob(p);
  }
};

struct Attr {
  std::string name, type;
  std::uint32_t size = 0;
  std::vector<Attr> children;
};

void write_attr(Bytes& b, const Attr& a) {
  b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
  for (const auto& c : a.children) write_attr(b, c);
}

// One class "Thing" holding `attrs`, and the given objects (ids 100, 101, ...) each with the given payload.
std::vector<std::byte> make_file(const std::vector<Attr>& attrs, const std::vector<Bytes>& payloads) {
  Bytes b;
  b.u32(1).name("Thing").u32(static_cast<std::uint32_t>(attrs.size()));
  for (const auto& a : attrs) write_attr(b, a);
  b.u32(static_cast<std::uint32_t>(payloads.size()));
  std::uint64_t id = 100;
  for (const auto& p : payloads) {
    b.u32(0).u64(id++).cat(p);
  }
  return b.v;
}

struct Decoded {
  std::vector<std::byte> bytes;
  SaveFile file;
  DecodedObject object;
};

// Builds a one-object file and decodes the object.
std::unique_ptr<Decoded> decode_one(const std::vector<Attr>& attrs, const Bytes& payload) {
  auto d = std::make_unique<Decoded>();
  d->bytes = make_file(attrs, {payload});
  auto file = read_save(d->bytes);
  REQUIRE(file);
  d->file = std::move(*file);
  auto obj = decode_object(d->file, d->file.objects[0]);
  REQUIRE(obj);
  d->object = std::move(*obj);
  return d;
}

}  // namespace

TEST_CASE("fixed scalars decode with the right sign and width") {
  const std::vector<Attr> attrs = {
      {"flag", "bool", 1, {}},       {"s8", "int8", 1, {}},       {"u8", "uint8", 1, {}},
      {"u16", "uint16", 2, {}},      {"s32", "int32", 4, {}},     {"u32", "uint32", 4, {}},
      {"real", "float32", 4, {}},
  };
  Bytes p;
  p.u8(7).u8(0xFE).u8(0xFE).u16(0xFFFE).u32(0xFFFFFFFEu).u32(0xFFFFFFFEu).f32(2.5f);
  const auto d = decode_one(attrs, p);
  const auto& o = d->object;
  CHECK(o.class_name == "Thing");
  CHECK(o.id == 100);
  CHECK(o.find("flag")->kind() == ValueKind::Bool);
  CHECK(o.find("flag")->as_bool());  // any non-zero byte
  CHECK(o.find("s8")->as_integer() == -2);
  CHECK(o.find("u8")->as_integer() == 254);
  CHECK(o.find("u16")->as_integer() == 65534);
  CHECK(o.find("s32")->as_integer() == -2);
  CHECK(o.find("u32")->as_integer() == 4294967294LL);
  CHECK(o.find("real")->as_float() == 2.5f);
  CHECK(o.find("missing") == nullptr);
  CHECK(o.stats.fallbacks == 0);
  CHECK(o.stats.opaque == 0);
  CHECK(o.stats.values == 7);
}

TEST_CASE("type names match case-insensitively") {
  const auto d = decode_one({{"x", "INT32", 4, {}}}, Bytes().u32(9));
  CHECK(d->object.find("x")->kind() == ValueKind::Integer);
  CHECK(d->object.find("x")->as_integer() == 9);
}

TEST_CASE("vectors, orientation, matrix and references") {
  const std::vector<Attr> attrs = {
      {"pos", "vector3", 12, {}},
      {"rot", "orientation", 12, {}},
      {"m", "matrix44", 64, {}},
      {"link", "reference", 8, {}},
      {"owner", "managedobjectptr", 8, {}},
  };
  Bytes p;
  p.f32(1).f32(2).f32(3);
  p.f32(0.25f).f32(0.5f).f32(0.75f);
  for (int i = 0; i < 16; ++i) p.f32(static_cast<float>(i));
  p.u64(101).u64(0);
  const auto d = decode_one(attrs, p);
  const auto& o = d->object;
  REQUIRE(o.find("pos")->floats().size() == 3);
  CHECK(o.find("pos")->kind() == ValueKind::Vector3);
  CHECK(o.find("pos")->floats()[2] == 3.0f);
  CHECK(o.find("rot")->kind() == ValueKind::Orientation);
  CHECK(o.find("rot")->floats()[1] == 0.5f);
  REQUIRE(o.find("m")->floats().size() == 16);
  CHECK(o.find("m")->kind() == ValueKind::Matrix44);
  CHECK(o.find("m")->floats()[15] == 15.0f);
  CHECK(o.find("link")->kind() == ValueKind::Reference);
  CHECK(o.find("link")->as_reference() == 101);
  CHECK(o.find("owner")->as_reference() == 0);
}

TEST_CASE("a scalar with the wrong declared size stays raw and counts as opaque") {
  const auto d = decode_one({{"v", "vector3", 8, {}}}, Bytes().u32(1).u32(2));
  CHECK(d->object.find("v")->kind() == ValueKind::Raw);
  CHECK(d->object.find("v")->raw().size() == 8);
  CHECK(d->object.stats.opaque == 1);
  CHECK(d->object.stats.fallbacks == 0);
}

TEST_CASE("strings") {
  const std::vector<Attr> attrs = {{"s", "string", 0, {}}};
  SUBCASE("marked UTF-16") {
    const auto d = decode_one(attrs, Bytes().marked_text(u"Café €"));
    CHECK(d->object.find("s")->kind() == ValueKind::String);
    CHECK(d->object.find("s")->text() == "Caf\xC3\xA9 \xE2\x82\xAC");
  }
  SUBCASE("surrogate pair and a lone surrogate") {
    const auto d = decode_one(attrs, Bytes().marked_text(std::u16string{0xD83D, 0xDE00, 0xD800, u'x'}));
    CHECK(d->object.find("s")->text() == "\xF0\x9F\x98\x80\xEF\xBF\xBD" "x");
  }
  SUBCASE("marker only is the empty string") {
    const auto d = decode_one(attrs, Bytes().marked_text(u""));
    CHECK(d->object.find("s")->kind() == ValueKind::String);
    CHECK(d->object.find("s")->text().empty());
  }
  SUBCASE("zero length is the empty string") {
    const auto d = decode_one(attrs, Bytes().u32(0));
    CHECK(d->object.find("s")->kind() == ValueKind::String);
    CHECK(d->object.find("s")->text().empty());
  }
  SUBCASE("unmarked payload is one byte per character") {
    Bytes inner;
    inner.u8('o').u8('l').u8('d').u8(0xE9);
    const auto d = decode_one(attrs, Bytes().blob(inner));
    CHECK(d->object.find("s")->text() == "old\xC3\xA9");
  }
  SUBCASE("odd number of text bytes after the marker degrades to raw") {
    Bytes inner;
    inner.u32(0xEFEFEFEFu).u8('a').u8(0).u8('b');
    const auto d = decode_one(attrs, Bytes().blob(inner));
    CHECK(d->object.find("s")->kind() == ValueKind::Raw);
    CHECK(d->object.stats.fallbacks == 1);
  }
}

TEST_CASE("symbol text") {
  const std::vector<Attr> attrs = {{"sym", "resourcesymbol", 0, {}}};
  SUBCASE("printable ASCII") {
    Bytes inner;
    for (const char c : std::string("alpha:beta")) inner.u8(static_cast<std::uint8_t>(c));
    const auto d = decode_one(attrs, Bytes().blob(inner));
    CHECK(d->object.find("sym")->text() == "alpha:beta");
  }
  SUBCASE("control bytes degrade to raw") {
    Bytes inner;
    inner.u8('a').u8(1);
    const auto d = decode_one(attrs, Bytes().blob(inner));
    CHECK(d->object.find("sym")->kind() == ValueKind::Raw);
    CHECK(d->object.stats.fallbacks == 1);
  }
}

TEST_CASE("list of scalars and list of structs") {
  const std::vector<Attr> attrs = {
      {"nums", "list", 0, {{"n", "int32", 4, {}}}},
      {"pairs", "array", 0, {{"a", "uint8", 1, {}}, {"b", "float32", 4, {}}}},
      {"empty", "list", 0, {{"n", "int32", 4, {}}}},
  };
  Bytes nums;
  nums.u32(3).u32(10).u32(20).u32(0xFFFFFFFFu);
  Bytes pairs;
  pairs.u32(2).u8(1).f32(0.5f).u8(2).f32(1.5f);
  Bytes empty;
  empty.u32(0);
  const auto d = decode_one(attrs, Bytes().blob(nums).blob(pairs).blob(empty));
  const auto& o = d->object;
  REQUIRE(o.find("nums")->items().size() == 3);
  CHECK(o.find("nums")->items()[0].as_integer() == 10);
  CHECK(o.find("nums")->items()[2].as_integer() == -1);
  REQUIRE(o.find("pairs")->items().size() == 2);
  const auto& second = o.find("pairs")->items()[1];
  CHECK(second.kind() == ValueKind::Struct);
  CHECK(second.field("a")->as_integer() == 2);
  CHECK(second.field("b")->as_float() == 1.5f);
  CHECK(o.find("empty")->kind() == ValueKind::List);
  CHECK(o.find("empty")->items().empty());
  CHECK(o.stats.fallbacks == 0);
}

TEST_CASE("nested variable fields carry their own length prefix") {
  // An array whose element is an array of int32, plus a struct with a string and a fixed field.
  const std::vector<Attr> attrs = {
      {"grid", "array", 0, {{"row", "array", 0, {{"n", "int32", 4, {}}}}}},
      {"rec", "struct", 0, {{"label", "string", 0, {}}, {"level", "uint8", 1, {}}}},
  };
  Bytes row0, row1;
  row0.u32(2).u32(1).u32(2);
  row1.u32(0);
  Bytes grid;
  grid.u32(2).blob(row0).blob(row1);
  Bytes rec;
  rec.marked_text(u"hi").u8(9);
  const auto d = decode_one(attrs, Bytes().blob(grid).blob(rec));
  const auto& o = d->object;
  REQUIRE(o.find("grid")->items().size() == 2);
  CHECK(o.find("grid")->items()[0].items().size() == 2);
  CHECK(o.find("grid")->items()[0].items()[1].as_integer() == 2);
  CHECK(o.find("grid")->items()[1].items().empty());
  CHECK(o.find("rec")->kind() == ValueKind::Struct);
  CHECK(o.find("rec")->field("label")->text() == "hi");
  CHECK(o.find("rec")->field("level")->as_integer() == 9);
  CHECK(o.stats.fallbacks == 0);
}

TEST_CASE("fixed-size struct is decoded from its children") {
  const std::vector<Attr> attrs = {{"pt", "struct", 5, {{"x", "float32", 4, {}}, {"on", "bool", 1, {}}}}};
  const auto d = decode_one(attrs, Bytes().f32(1.0f).u8(1));
  CHECK(d->object.find("pt")->field("x")->as_float() == 1.0f);
  CHECK(d->object.find("pt")->field("on")->as_bool());
  CHECK(d->object.stats.fallbacks == 0);
}

TEST_CASE("fixed-size struct whose children do not add up stays raw") {
  const std::vector<Attr> attrs = {{"pt", "struct", 6, {{"x", "float32", 4, {}}}}};
  const auto d = decode_one(attrs, Bytes().f32(1.0f).u8(1).u8(2));
  CHECK(d->object.find("pt")->kind() == ValueKind::Raw);
  CHECK(d->object.stats.fallbacks == 1);
}

TEST_CASE("curve and path blobs") {
  const std::vector<Attr> attrs = {{"curve", "graphedValue", 0, {}}, {"route", "waypointlist", 0, {}}};
  Bytes curve;
  curve.u32(0xFFFFFFFFu).u32(8).u32(2).f32(1.0f).f32(2.0f);
  Bytes route;
  route.u16(2).u16(5).u16(6).u16(0xFFFF);
  route.f32(1).f32(2).f32(3).f32(4).f32(5).f32(6);
  const auto d = decode_one(attrs, Bytes().blob(curve).blob(route));
  const auto& o = d->object;
  CHECK(o.find("curve")->field("first")->as_integer() == -1);
  CHECK(o.find("curve")->field("capacity")->as_integer() == 8);
  REQUIRE(o.find("curve")->field("values")->floats().size() == 2);
  CHECK(o.find("curve")->field("values")->floats()[1] == 2.0f);
  CHECK(o.find("route")->field("count")->as_integer() == 2);
  CHECK(o.find("route")->field("word2")->as_integer() == 6);
  REQUIRE(o.find("route")->field("points")->items().size() == 2);
  CHECK(o.find("route")->field("points")->items()[1].floats()[0] == 4.0f);
  CHECK(o.stats.fallbacks == 0);
}

TEST_CASE("curve and path with inconsistent lengths degrade to raw") {
  const std::vector<Attr> attrs = {{"curve", "graphedValue", 0, {}}, {"route", "waypointlist", 0, {}}};
  Bytes curve;
  curve.u32(0).u32(8).u32(5).f32(1.0f);  // claims five values, holds one
  Bytes route;
  route.u16(9).u16(0).u16(0).u16(0xFFFF);  // claims nine points, holds none
  const auto d = decode_one(attrs, Bytes().blob(curve).blob(route));
  CHECK(d->object.find("curve")->kind() == ValueKind::Raw);
  CHECK(d->object.find("route")->kind() == ValueKind::Raw);
  CHECK(d->object.stats.fallbacks == 2);
}

TEST_CASE("unknown variable types are kept as raw bytes") {
  Bytes inner;
  inner.u8(1).u8(2).u8(3);
  const auto d = decode_one({{"blob", "mystery", 0, {}}, {"after", "int32", 4, {}}}, Bytes().blob(inner).u32(77));
  REQUIRE(d->object.find("blob")->kind() == ValueKind::Raw);
  CHECK(d->object.find("blob")->raw().size() == 3);
  CHECK(d->object.find("after")->as_integer() == 77);
  CHECK(d->object.stats.opaque == 1);
  CHECK(d->object.stats.fallbacks == 0);
}

TEST_CASE("hostile counts and leftovers degrade the container, not the object") {
  const std::vector<Attr> attrs = {{"items", "list", 0, {{"n", "int32", 4, {}}}}, {"tail", "uint8", 1, {}}};
  SUBCASE("count far larger than the payload") {
    Bytes inner;
    inner.u32(0xFFFFFFFFu).u32(1);
    const auto d = decode_one(attrs, Bytes().blob(inner).u8(5));
    CHECK(d->object.find("items")->kind() == ValueKind::Raw);
    CHECK(d->object.find("tail")->as_integer() == 5);
    CHECK(d->object.stats.fallbacks == 1);
  }
  SUBCASE("bytes left after the last element") {
    Bytes inner;
    inner.u32(1).u32(1).u8(0);
    const auto d = decode_one(attrs, Bytes().blob(inner).u8(5));
    CHECK(d->object.find("items")->kind() == ValueKind::Raw);
    CHECK(d->object.stats.fallbacks == 1);
  }
  SUBCASE("payload too short for the count") {
    Bytes inner;
    inner.u8(1).u8(0);
    const auto d = decode_one(attrs, Bytes().blob(inner).u8(5));
    CHECK(d->object.find("items")->kind() == ValueKind::Raw);
  }
  SUBCASE("elements with no layout cannot carry a count") {
    const std::vector<Attr> none = {{"items", "list", 0, {}}};
    Bytes inner;
    inner.u32(0x7FFFFFFFu);
    const auto d = decode_one(none, Bytes().blob(inner));
    CHECK(d->object.find("items")->kind() == ValueKind::Raw);
    CHECK(d->object.stats.fallbacks == 1);
  }
  SUBCASE("elements with no layout and a zero count are an empty list") {
    const std::vector<Attr> none = {{"items", "list", 0, {}}};
    const auto d = decode_one(none, Bytes().blob(Bytes().u32(0)));
    CHECK(d->object.find("items")->kind() == ValueKind::List);
    CHECK(d->object.stats.fallbacks == 0);
  }
  SUBCASE("variable element with an inner length past the payload") {
    const std::vector<Attr> var = {{"names", "list", 0, {{"s", "string", 0, {}}}}};
    Bytes inner;
    inner.u32(1).u32(0x1000);
    const auto d = decode_one(var, Bytes().blob(inner));
    CHECK(d->object.find("names")->kind() == ValueKind::Raw);
    CHECK(d->object.stats.fallbacks == 1);
  }
}

TEST_CASE("nesting beyond the decoder depth limit degrades instead of recursing") {
  // Built by hand: the container reader would refuse a schema this deep, the decoder must still be safe.
  constexpr std::size_t kLevels = 80;
  SaveFile file;
  Attribute chain{"leaf", "int32", 4, {}};
  for (std::size_t i = 0; i < kLevels; ++i) {
    Attribute outer{"level", "struct", 0, {}};
    outer.children.push_back(std::move(chain));
    chain = std::move(outer);
  }
  file.classes.push_back(Class{"Deep", {}});
  file.classes[0].attributes.push_back(std::move(chain));

  Bytes payload;
  payload.u32(0).u64(1);
  Bytes inner;
  inner.u32(42);
  for (std::size_t i = 0; i < kLevels; ++i) {
    Bytes outer;
    outer.blob(inner);
    inner = std::move(outer);
  }
  payload.cat(inner);
  Object o;
  o.class_index = 0;
  o.id = 1;
  o.bytes = payload.v;
  file.objects.push_back(o);

  const auto r = decode_object(file, file.objects[0]);
  REQUIRE(r);
  CHECK(r->stats.fallbacks >= 1);
}

TEST_CASE("decode_object validates its input") {
  SaveFile file;
  Object o;
  o.class_index = 3;
  CHECK(decode_object(file, o).error() == Error::BadClassIndex);

  file.classes.push_back(Class{"Empty", {}});
  o.class_index = 0;
  CHECK(decode_object(file, o).error() == Error::BadObjectTable);  // shorter than the class index and id
}

TEST_CASE("object index resolves ids, first one wins on duplicates") {
  Bytes a, b;
  a.u32(1);
  b.u32(2);
  const auto bytes = make_file({{"n", "int32", 4, {}}}, {a, b});
  const auto file = read_save(bytes);
  REQUIRE(file);
  const ObjectIndex index(*file);
  CHECK(index.size() == 2);
  REQUIRE(index.find(100));
  CHECK(index.find(100)->id == 100);
  CHECK(index.find(101) == &file->objects[1]);
  CHECK(index.find(0) == nullptr);
  CHECK(index.find(9999) == nullptr);
}

TEST_CASE("every truncation and every single byte flip decodes without crashing") {
  const std::vector<Attr> attrs = {
      {"label", "string", 0, {}},
      {"items", "list", 0, {{"a", "uint16", 2, {}}, {"s", "string", 0, {}}}},
      {"curve", "graphedValue", 0, {}},
      {"pos", "vector3", 12, {}},
  };
  Bytes label;
  label.marked_text(u"ab");
  Bytes items;
  items.u32(2);
  items.u16(1).marked_text(u"x");
  items.u16(2).marked_text(u"yz");
  Bytes curve;
  curve.u32(0).u32(1).u32(1).f32(1.0f);
  Bytes p;
  p.cat(label).blob(items).blob(curve).f32(1).f32(2).f32(3);
  const auto original = make_file(attrs, {p});

  const auto exercise = [](const std::vector<std::byte>& data) {
    const auto file = read_save(data);
    if (!file) return;
    for (const auto& o : file->objects) {
      const auto r = decode_object(*file, o);
      REQUIRE(r);
      CHECK(r->stats.values <= 2 * o.bytes.size() + file->classes[o.class_index].attributes.size());
    }
  };
  for (std::size_t n = 0; n <= original.size(); ++n) {
    exercise(std::vector<std::byte>(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(n)));
  }
  for (std::size_t i = 0; i < original.size(); ++i) {
    for (const std::uint8_t mask : {0x01, 0x80, 0xFF}) {
      auto copy = original;
      copy[i] ^= static_cast<std::byte>(mask);
      exercise(copy);
    }
  }
}
