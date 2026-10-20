// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generic value decoding for the object records of a save file.
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

    enum class ValueKind : std::uint8_t
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
        [[nodiscard]] std::int64_t as_integer() const noexcept
        {
            return static_cast<std::int64_t>(bits_);
        }
        [[nodiscard]] float as_float() const noexcept;
        [[nodiscard]] std::uint64_t as_reference() const noexcept
        {
            return bits_;
        }

        [[nodiscard]] std::string_view text() const noexcept;
        [[nodiscard]] std::span<const float> floats() const noexcept;           ///< Vector3, Orientation, Matrix44, FloatArray
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
        static Value make_integer(std::int64_t v);
        static Value make_float(float v);
        static Value make_reference(std::uint64_t id);
        static Value make_raw(std::span<const std::byte> bytes);
        static Value make_string(std::string text);
        static Value make_floats(ValueKind kind, std::vector<float> values);
        static Value make_list(std::vector<Value> items);
        static Value make_struct(std::vector<std::string_view> names, std::vector<Value> items);

    private:
        ValueKind kind_ = ValueKind::Raw;
        std::uint64_t bits_ = 0;
        std::span<const std::byte> raw_;
        std::unique_ptr<detail::ValueExtra> extra_;
    };

    /// Counters describing how an object decoded.
    struct DecodeStats
    {
        std::size_t values = 0;    ///< Values produced, counting nested ones
        std::size_t opaque = 0;    ///< Attributes kept raw because their type is not understood
        std::size_t fallbacks = 0; ///< Understood types whose bytes did not fit; kept raw
    };

    struct DecodedObject
    {
        std::uint32_t class_index = 0;
        std::uint64_t id = 0;
        std::string_view class_name;
        Value fields; ///< A Struct with one field per class attribute, in schema order
        DecodeStats stats;

        /// Attribute value by name.
        [[nodiscard]] std::optional<const Value&> find(std::string_view name) const noexcept
        {
            return fields.field(name);
        }
    };

    /// Decodes every attribute of `object` using its class layout. Fails only if the object does not belong to
    /// `file` or its framing is broken, which read_save already rules out for objects it returned.
    [[nodiscard]] std::expected<DecodedObject, Error> decode_object(const SaveFile& file, const Object& object);

    /// Object lookup by id. Ids are unique in well-formed files; on duplicates the first object wins.
    class ObjectIndex
    {
    public:
        explicit ObjectIndex(const SaveFile& file);
        [[nodiscard]] std::optional<const Object&> find(std::uint64_t id) const noexcept;
        [[nodiscard]] std::size_t size() const noexcept
        {
            return by_id_.size();
        }

    private:
        const SaveFile* file_;
        std::unordered_map<std::uint64_t, std::uint32_t> by_id_;
    };

} // namespace rct3::save
