// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generic value decoding and encoding for the object records of a save file.
//
// The class table of a save describes every attribute with a type string, a fixed size (or 0 for
// variable) and nested child descriptors. This module turns an object's raw attribute bytes into a tree of
// typed values using only that schema; it knows nothing about what any class means.
//
// Encodings, all little-endian:
//  - fixed scalars: bool (1 byte, non-zero is true), int8/uint8, uint16, int32/uint32, float32;
//  - vector3 and orientation: three float32; matrix44: sixteen float32;
//  - reference and managedobjectptr: a u64 object id, 0 meaning null;
//  - string: payload is a four byte marker followed by UTF-16LE code units (older files may hold one byte
//    per character instead, with no marker);
//  - list and array: u32 element count, then each element as the child layout; a single child is the
//    element itself, several children make the element a struct of them;
//  - struct: its children in order, fixed fields inline and variable fields with a u32 length prefix;
//  - a few named compound types (see value.cpp) have their own small layouts.
// Any type that is not recognised, or a recognised type whose bytes do not fit its layout, is kept as raw
// bytes. Decoding never fails because of an attribute.
#pragma once

#include "core/types.hpp"
#include "save/save_file.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rct3::save
{

    enum class ValueKind : u8
    {
        Raw, ///< Opaque bytes: unknown type, or a known type that did not fit its layout
        Bool,
        Integer,     ///< Signed or unsigned up to 32 bits, held sign-correct in an int64
        Float,       ///< float32
        String,      ///< UTF-8, converted from the file's encoding
        Vector3,     ///< three floats
        Orientation, ///< three floats
        Matrix44,    ///< sixteen floats, in file order
        FloatArray,  ///< a run of floats of any length
        Reference,   ///< object id; 0 is null
        List,        ///< items() are the elements
        Struct,      ///< names() and items() are parallel, in schema order
    };

    [[nodiscard]] const char* to_string(ValueKind k) noexcept;

    class Value;

    namespace detail
    {
        struct ValueExtra;
    }

    /// One decoded value. Cheap for scalars; compound data lives behind a pointer. Names and raw bytes are views
    /// into the SaveFile and the input it was read from, which must outlive the value.
    class Value
    {
    public:
        Value() noexcept;
        ~Value();
        Value(Value&&) noexcept;
        Value& operator=(Value&&) noexcept;
        Value(const Value&) = delete;
        Value& operator=(const Value&) = delete;

        [[nodiscard]] ValueKind kind() const noexcept
        {
            return kind_;
        }

        [[nodiscard]] bool as_bool() const noexcept
        {
            return bits_ != 0;
        }
        [[nodiscard]] i64 as_integer() const noexcept
        {
            return static_cast<i64>(bits_);
        }
        [[nodiscard]] f32 as_float() const noexcept;
        [[nodiscard]] u64 as_reference() const noexcept
        {
            return bits_;
        }

        [[nodiscard]] std::string_view text() const noexcept;
        [[nodiscard]] std::span<const f32> floats() const noexcept;             ///< Vector3, Orientation, Matrix44, FloatArray
        [[nodiscard]] std::span<const Value> items() const noexcept;            ///< List elements or Struct field values
        [[nodiscard]] std::span<const std::string_view> names() const noexcept; ///< Struct field names
        [[nodiscard]] std::span<const std::byte> raw() const noexcept
        {
            return raw_;
        }

        /// First field of a Struct with this name; none for non-structs.
        [[nodiscard]] std::optional<const Value&> field(std::string_view name) const noexcept;

        // Construction, used by the decoder and by tests.
        static Value make_bool(bool v);
        static Value make_integer(i64 v);
        static Value make_float(f32 v);
        static Value make_reference(u64 id);
        static Value make_raw(std::span<const std::byte> bytes);
        static Value make_string(std::string text);
        static Value make_floats(ValueKind kind, std::vector<f32> values);
        static Value make_list(std::vector<Value> items);
        static Value make_struct(std::vector<std::string_view> names, std::vector<Value> items);

    private:
        ValueKind kind_ = ValueKind::Raw;
        u64 bits_ = 0;
        std::span<const std::byte> raw_;
        std::unique_ptr<detail::ValueExtra> extra_;
    };

    /// Counters describing how an object decoded.
    struct DecodeStats
    {
        usize values = 0;    ///< Values produced, counting nested ones
        usize opaque = 0;    ///< Attributes kept raw because their type is not understood
        usize fallbacks = 0; ///< Understood types whose bytes did not fit; kept raw
    };

    struct DecodedObject
    {
        u32 class_index = 0;
        u64 id = 0;
        std::string_view class_name;
        Value fields; ///< A Struct with one field per class attribute, in schema order
        DecodeStats stats;

        [[nodiscard]] std::optional<const Value&> find(std::string_view name) const noexcept
        {
            return fields.field(name);
        }
    };

    /// Decodes every attribute of `object` using its class layout. Fails only if the object does not belong to
    /// `file` or its framing is broken, which read_save already rules out for objects it returned.
    [[nodiscard]] std::expected<DecodedObject, Error> decode_object(const SaveFile& file, const Object& object);

    /// The inverse of decode_object: `fields`, a Struct with one value per attribute of `cls` in schema order and
    /// named as they are, as the attribute bytes of an object (see ObjectRecord). Raw values are written as they
    /// are; strings with the four byte marker and UTF-16LE. Fails with ValueMismatch when a value's kind, size or
    /// field count does not fit its attribute.
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> encode_object(const Class& cls, const Value& fields);

    /// Marks which attributes of a save loaders look up (Value::field) while it lives, so that what a load left
    /// behind can be told afterwards (see save/left_out.hpp). Attribute names are views into the save's bytes,
    /// so an attribute is told apart by where its name lies in them, whichever read of the bytes decoded it.
    ///
    /// Global: the lookups happen deep inside every loader, and threading a log through all of them would put
    /// it in every loader's signature for one report. One per thread, owned by whoever made it; they nest, the
    /// newest recording until it goes.
    class FieldReads
    {
    public:
        explicit FieldReads(std::span<const std::byte> data);
        ~FieldReads();
        FieldReads(const FieldReads&) = delete;
        FieldReads& operator=(const FieldReads&) = delete;

        /// Whether `name`, a view into the bytes given, was looked up. A name from elsewhere never was.
        [[nodiscard]] bool was_read(std::string_view name) const noexcept;
        /// Marks `name` looked up; called by Value::field on the recording log, if any.
        void mark(std::string_view name) noexcept;

    private:
        [[nodiscard]] std::optional<usize> offset(std::string_view name) const noexcept;

        std::span<const std::byte> data_;
        std::vector<bool> read_;
        FieldReads* outer_ = nullptr;
    };

    /// Object lookup by id. Ids are unique in well-formed files; on duplicates the first object wins.
    class ObjectIndex
    {
    public:
        explicit ObjectIndex(const SaveFile& file);
        [[nodiscard]] std::optional<const Object&> find(u64 id) const noexcept;
        [[nodiscard]] usize size() const noexcept
        {
            return by_id_.size();
        }

    private:
        const SaveFile* file_;
        std::unordered_map<u64, u32> by_id_;
    };

} // namespace rct3::save
