// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/value.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <utility>

namespace rct3::save {

namespace detail {
struct ValueExtra {
  std::string text;
  std::vector<float> floats;
  std::vector<Value> items;
  std::vector<std::string_view> names;
};
}  // namespace detail

const char* to_string(ValueKind k) noexcept {
  switch (k) {
    case ValueKind::Raw: return "raw";
    case ValueKind::Bool: return "bool";
    case ValueKind::Integer: return "integer";
    case ValueKind::Float: return "float";
    case ValueKind::String: return "string";
    case ValueKind::Vector3: return "vector3";
    case ValueKind::Orientation: return "orientation";
    case ValueKind::Matrix44: return "matrix44";
    case ValueKind::FloatArray: return "float-array";
    case ValueKind::Reference: return "reference";
    case ValueKind::List: return "list";
    case ValueKind::Struct: return "struct";
  }
  return "unknown";
}

// ---- Value ------------------------------------------------------------------------------------------------

Value::Value() noexcept = default;
Value::~Value() = default;
Value::Value(Value&&) noexcept = default;
Value& Value::operator=(Value&&) noexcept = default;

float Value::as_float() const noexcept {
  const auto bits = static_cast<std::uint32_t>(bits_);
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

std::string_view Value::text() const noexcept { return extra_ ? std::string_view(extra_->text) : std::string_view(); }

std::span<const float> Value::floats() const noexcept {
  return extra_ ? std::span<const float>(extra_->floats) : std::span<const float>();
}

std::span<const Value> Value::items() const noexcept {
  return extra_ ? std::span<const Value>(extra_->items) : std::span<const Value>();
}

std::span<const std::string_view> Value::names() const noexcept {
  return extra_ ? std::span<const std::string_view>(extra_->names) : std::span<const std::string_view>();
}

const Value* Value::field(std::string_view name) const noexcept {
  if (kind_ != ValueKind::Struct || !extra_) return nullptr;
  for (std::size_t i = 0; i < extra_->names.size(); ++i) {
    if (extra_->names[i] == name) return &extra_->items[i];
  }
  return nullptr;
}

Value Value::make_bool(bool v) {
  Value r;
  r.kind_ = ValueKind::Bool;
  r.bits_ = v ? 1 : 0;
  return r;
}

Value Value::make_integer(std::int64_t v) {
  Value r;
  r.kind_ = ValueKind::Integer;
  r.bits_ = static_cast<std::uint64_t>(v);
  return r;
}

Value Value::make_float(float v) {
  Value r;
  r.kind_ = ValueKind::Float;
  std::uint32_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  r.bits_ = bits;
  return r;
}

Value Value::make_reference(std::uint64_t id) {
  Value r;
  r.kind_ = ValueKind::Reference;
  r.bits_ = id;
  return r;
}

Value Value::make_raw(std::span<const std::byte> bytes) {
  Value r;
  r.kind_ = ValueKind::Raw;
  r.raw_ = bytes;
  return r;
}

Value Value::make_string(std::string text) {
  Value r;
  r.kind_ = ValueKind::String;
  r.extra_ = std::make_unique<detail::ValueExtra>();
  r.extra_->text = std::move(text);
  return r;
}

Value Value::make_floats(ValueKind kind, std::vector<float> values) {
  Value r;
  r.kind_ = kind;
  r.extra_ = std::make_unique<detail::ValueExtra>();
  r.extra_->floats = std::move(values);
  return r;
}

Value Value::make_list(std::vector<Value> items) {
  Value r;
  r.kind_ = ValueKind::List;
  r.extra_ = std::make_unique<detail::ValueExtra>();
  r.extra_->items = std::move(items);
  return r;
}

Value Value::make_struct(std::vector<std::string_view> names, std::vector<Value> items) {
  Value r;
  r.kind_ = ValueKind::Struct;
  r.extra_ = std::make_unique<detail::ValueExtra>();
  r.extra_->names = std::move(names);
  r.extra_->items = std::move(items);
  return r;
}

// ---- Decoder ----------------------------------------------------------------------------------------------

namespace {

// Nesting the decoder is willing to follow. The class table reader already caps schema depth; this guards
// callers that build a SaveFile by hand.
constexpr std::size_t kMaxDepth = 32;
// Smallest encoding of a variable child inside an element: its u32 length.
constexpr std::size_t kVariableChildBytes = 4;

// The fixed-size types the decoder understands. A fixed attribute is only decoded as one of these when its
// declared size also matches, mirroring a reader that skips by size when the layout is not what it expects.
// Matching is case-insensitive.
enum class Scalar : std::uint8_t { Bool, I8, U8, U16, I32, U32, F32, Vec3, Orient, Mat44, Ref };

struct ScalarType {
  std::string_view name;
  std::uint32_t size;
  Scalar scalar;
};

constexpr std::array kScalarTypes = {
    ScalarType{"bool", 1, Scalar::Bool},          ScalarType{"int8", 1, Scalar::I8},
    ScalarType{"uint8", 1, Scalar::U8},           ScalarType{"uint16", 2, Scalar::U16},
    ScalarType{"int32", 4, Scalar::I32},          ScalarType{"uint32", 4, Scalar::U32},
    ScalarType{"float32", 4, Scalar::F32},        ScalarType{"vector3", 12, Scalar::Vec3},
    ScalarType{"orientation", 12, Scalar::Orient}, ScalarType{"matrix44", 64, Scalar::Mat44},
    ScalarType{"reference", 8, Scalar::Ref},      ScalarType{"managedobjectptr", 8, Scalar::Ref},
};

// Compound type names with variable size. Everything else with size 0 is opaque.
constexpr std::string_view kStringType = "string";
constexpr std::string_view kSymbolType = "resourcesymbol";
constexpr std::string_view kListType = "list";
constexpr std::string_view kArrayType = "array";
constexpr std::string_view kStructType = "struct";
constexpr std::string_view kCurveType = "graphedValue";
constexpr std::string_view kPathType = "waypointlist";

bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

class Cursor {
 public:
  explicit Cursor(std::span<const std::byte> data) : data_(data) {}
  [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
  [[nodiscard]] bool done() const noexcept { return pos_ == data_.size(); }

  std::optional<std::span<const std::byte>> take(std::size_t n) {
    if (n > remaining()) return std::nullopt;
    auto s = data_.subspan(pos_, n);
    pos_ += n;
    return s;
  }
  std::optional<std::uint32_t> u32() {
    auto s = take(4);
    if (!s) return std::nullopt;
    return load<std::uint32_t>(*s, 0);
  }

  template <class T>
  static T load(std::span<const std::byte> s, std::size_t at) noexcept {
    T v;
    std::memcpy(&v, s.data() + at, sizeof v);
    return v;
  }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
};

void append_utf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// UTF-16LE to UTF-8; unpaired surrogates become U+FFFD.
std::string utf16le_to_utf8(std::span<const std::byte> bytes) {
  std::string out;
  out.reserve(bytes.size() / 2);
  const std::size_t n = bytes.size() / 2;
  for (std::size_t i = 0; i < n; ++i) {
    char32_t u = Cursor::load<std::uint16_t>(bytes, i * 2);
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n) {
      const char32_t lo = Cursor::load<std::uint16_t>(bytes, (i + 1) * 2);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        ++i;
      } else {
        u = 0xFFFD;
      }
    } else if (u >= 0xD800 && u <= 0xDFFF) {
      u = 0xFFFD;
    }
    append_utf8(out, u);
  }
  return out;
}

class Decoder {
 public:
  DecodeStats stats;

  // Reads one attribute, fixed or length-prefixed, from the cursor. Fails only when the framing runs past the
  // end of the cursor's data.
  std::optional<Value> attribute(const Attribute& a, Cursor& c, std::size_t depth) {
    if (a.size > 0) {
      auto bytes = c.take(a.size);
      if (!bytes) return std::nullopt;
      return fixed(a, *bytes, depth);
    }
    auto length = c.u32();
    if (!length) return std::nullopt;
    auto bytes = c.take(*length);
    if (!bytes) return std::nullopt;
    return variable(a, *bytes, depth);
  }

 private:
  Value produce(Value v) {
    ++stats.values;
    return v;
  }

  Value fallback(std::span<const std::byte> bytes, const DecodeStats& before) {
    stats = before;
    ++stats.fallbacks;
    return produce(Value::make_raw(bytes));
  }

  Value opaque(std::span<const std::byte> bytes) {
    ++stats.opaque;
    return produce(Value::make_raw(bytes));
  }

  // Children of a compound type, decoded one after another from `c`. nullopt when the data is too short.
  std::optional<std::vector<Value>> sequence(const std::vector<Attribute>& children, Cursor& c, std::size_t depth) {
    std::vector<Value> out;
    out.reserve(children.size());
    for (const auto& child : children) {
      auto v = attribute(child, c, depth + 1);
      if (!v) return std::nullopt;
      out.push_back(std::move(*v));
    }
    return out;
  }

  // One element of a list or array: the lone child, or a struct of several children.
  std::optional<Value> element(const std::vector<Attribute>& children, Cursor& c, std::size_t depth) {
    if (children.size() == 1) return attribute(children[0], c, depth + 1);
    auto fields = sequence(children, c, depth);
    if (!fields) return std::nullopt;
    std::vector<std::string_view> names;
    names.reserve(children.size());
    for (const auto& child : children) names.push_back(child.name);
    ++stats.values;
    return Value::make_struct(std::move(names), std::move(*fields));
  }

  Value structure(const Attribute& a, std::span<const std::byte> bytes, std::size_t depth) {
    const DecodeStats before = stats;
    Cursor c(bytes);
    auto fields = sequence(a.children, c, depth);
    if (!fields || !c.done()) return fallback(bytes, before);
    std::vector<std::string_view> names;
    names.reserve(a.children.size());
    for (const auto& child : a.children) names.push_back(child.name);
    return produce(Value::make_struct(std::move(names), std::move(*fields)));
  }

  Value fixed(const Attribute& a, std::span<const std::byte> bytes, std::size_t depth) {
    if (depth > kMaxDepth) return fallback(bytes, stats);
    for (const auto& t : kScalarTypes) {
      if (t.size != a.size || !iequals(a.type, t.name)) continue;
      return produce(scalar(t.scalar, bytes));
    }
    if (iequals(a.type, kStructType) && !a.children.empty()) return structure(a, bytes, depth);
    return opaque(bytes);
  }

  static Value scalar(Scalar s, std::span<const std::byte> b) {
    switch (s) {
      case Scalar::Bool: return Value::make_bool(b[0] != std::byte{0});
      case Scalar::I8: return Value::make_integer(static_cast<std::int8_t>(b[0]));
      case Scalar::U8: return Value::make_integer(static_cast<std::uint8_t>(b[0]));
      case Scalar::U16: return Value::make_integer(Cursor::load<std::uint16_t>(b, 0));
      case Scalar::I32: return Value::make_integer(Cursor::load<std::int32_t>(b, 0));
      case Scalar::U32: return Value::make_integer(Cursor::load<std::uint32_t>(b, 0));
      case Scalar::F32: return Value::make_float(Cursor::load<float>(b, 0));
      case Scalar::Ref: return Value::make_reference(Cursor::load<std::uint64_t>(b, 0));
      case Scalar::Vec3:
      case Scalar::Orient:
      case Scalar::Mat44: {
        std::vector<float> f(b.size() / 4);
        std::memcpy(f.data(), b.data(), f.size() * 4);
        const auto kind = s == Scalar::Vec3 ? ValueKind::Vector3
                          : s == Scalar::Orient ? ValueKind::Orientation
                                                : ValueKind::Matrix44;
        return Value::make_floats(kind, std::move(f));
      }
    }
    return Value::make_raw(b);
  }

  Value variable(const Attribute& a, std::span<const std::byte> bytes, std::size_t depth) {
    if (depth > kMaxDepth) return fallback(bytes, stats);
    if (iequals(a.type, kStringType)) return string(bytes);
    if (iequals(a.type, kSymbolType)) return symbol(bytes);
    if (iequals(a.type, kListType) || iequals(a.type, kArrayType)) return container(a, bytes, depth);
    if (iequals(a.type, kStructType) && !a.children.empty()) return structure(a, bytes, depth);
    if (iequals(a.type, kCurveType)) return curve(bytes);
    if (iequals(a.type, kPathType)) return path(bytes);
    return opaque(bytes);
  }

  Value string(std::span<const std::byte> bytes) {
    // Marked payloads hold UTF-16LE after a four byte marker. Without the marker the bytes are one 8-bit
    // character each.
    constexpr std::uint32_t kMarker = 0xEFEFEFEFu;
    if (bytes.size() >= 4 && Cursor::load<std::uint32_t>(bytes, 0) == kMarker) {
      const auto body = bytes.subspan(4);
      if (body.size() % 2 != 0) return fallback(bytes, stats);
      return produce(Value::make_string(utf16le_to_utf8(body)));
    }
    std::string s;
    for (const std::byte b : bytes) append_utf8(s, static_cast<std::uint8_t>(b));
    return produce(Value::make_string(std::move(s)));
  }

  Value symbol(std::span<const std::byte> bytes) {
    std::string s;
    s.reserve(bytes.size());
    for (const std::byte b : bytes) {
      const auto ch = static_cast<unsigned char>(b);
      if (ch < 0x20 || ch > 0x7E) return fallback(bytes, stats);
      s += static_cast<char>(ch);
    }
    return produce(Value::make_string(std::move(s)));
  }

  Value container(const Attribute& a, std::span<const std::byte> bytes, std::size_t depth) {
    const DecodeStats before = stats;
    Cursor c(bytes);
    const auto count = c.u32();
    if (!count) return fallback(bytes, before);

    std::size_t min_element = 0;
    for (const auto& child : a.children) min_element += child.size > 0 ? child.size : kVariableChildBytes;
    // Every element consumes at least min_element bytes, so the count is bounded by the data. Elements that
    // could be empty give no such bound and are only accepted for a count of zero.
    if (min_element == 0 ? *count != 0 : *count > c.remaining() / min_element) return fallback(bytes, before);

    std::vector<Value> items;
    items.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
      auto v = element(a.children, c, depth);
      if (!v) return fallback(bytes, before);
      items.push_back(std::move(*v));
    }
    if (!c.done()) return fallback(bytes, before);
    return produce(Value::make_list(std::move(items)));
  }

  // Curve: i32 first, u32 capacity, u32 count, then `count` float32.
  Value curve(std::span<const std::byte> bytes) {
    if (bytes.size() >= 12) {
      const auto count = Cursor::load<std::uint32_t>(bytes, 8);
      if (count <= (bytes.size() - 12) / 4 && 12 + std::size_t{count} * 4 == bytes.size()) {
        std::vector<float> f(count);
        std::memcpy(f.data(), bytes.data() + 12, std::size_t{count} * 4);
        std::vector<Value> v;
        v.push_back(Value::make_integer(Cursor::load<std::int32_t>(bytes, 0)));
        v.push_back(Value::make_integer(Cursor::load<std::uint32_t>(bytes, 4)));
        v.push_back(Value::make_floats(ValueKind::FloatArray, std::move(f)));
        stats.values += 3;
        return produce(Value::make_struct({"first", "capacity", "values"}, std::move(v)));
      }
    }
    return fallback(bytes, stats);
  }

  // Path: four u16 (count and three more), then `count` vector3.
  Value path(std::span<const std::byte> bytes) {
    if (bytes.size() >= 8) {
      const auto count = Cursor::load<std::uint16_t>(bytes, 0);
      if (8 + std::size_t{count} * 12 == bytes.size()) {
        std::vector<Value> head;
        for (std::size_t i = 0; i < 4; ++i) head.push_back(Value::make_integer(Cursor::load<std::uint16_t>(bytes, i * 2)));
        std::vector<Value> points;
        points.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
          std::vector<float> f(3);
          std::memcpy(f.data(), bytes.data() + 8 + i * 12, 12);
          points.push_back(Value::make_floats(ValueKind::Vector3, std::move(f)));
        }
        stats.values += 4 + count + 1;
        head.push_back(Value::make_list(std::move(points)));
        return produce(Value::make_struct({"count", "word1", "word2", "word3", "points"}, std::move(head)));
      }
    }
    return fallback(bytes, stats);
  }
};

}  // namespace

std::expected<DecodedObject, Error> decode_object(const SaveFile& file, const Object& object) {
  if (object.class_index >= file.classes.size()) return std::unexpected(Error::BadClassIndex);
  if (object.bytes.size() < 12) return std::unexpected(Error::BadObjectTable);
  const Class& cls = file.classes[object.class_index];

  Decoder d;
  Cursor c(object.payload());
  std::vector<std::string_view> names;
  std::vector<Value> values;
  names.reserve(cls.attributes.size());
  values.reserve(cls.attributes.size());
  for (const auto& a : cls.attributes) {
    auto v = d.attribute(a, c, 0);
    if (!v) return std::unexpected(Error::BadObjectTable);
    names.push_back(a.name);
    values.push_back(std::move(*v));
  }
  if (!c.done()) return std::unexpected(Error::BadObjectTable);

  DecodedObject out;
  out.class_index = object.class_index;
  out.id = object.id;
  out.class_name = cls.name;
  out.fields = Value::make_struct(std::move(names), std::move(values));
  out.stats = d.stats;
  return out;
}

ObjectIndex::ObjectIndex(const SaveFile& file) : file_(&file) {
  by_id_.reserve(file.objects.size());
  for (std::size_t i = 0; i < file.objects.size(); ++i) {
    by_id_.try_emplace(file.objects[i].id, static_cast<std::uint32_t>(i));
  }
}

const Object* ObjectIndex::find(std::uint64_t id) const noexcept {
  const auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : &file_->objects[it->second];
}

}  // namespace rct3::save
