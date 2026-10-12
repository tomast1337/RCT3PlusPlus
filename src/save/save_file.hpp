// SPDX-License-Identifier: GPL-3.0-or-later
//
// Container reader for the original game's park and scenario save files.
//
// A save is a plain, self-describing stream: an optional fixed header, a class table that describes the
// layout of every kind of object in the file, a flat list of object records and, when the header is
// present, a trailing checksum. Nothing is compressed or encrypted. This reader validates the framing and
// hands out views into the caller's bytes; it does not interpret attribute payloads.
//
// Everything is little-endian. Names in the class table are byte strings, returned as views.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::save
{

    enum class Error
    {
        Truncated,          ///< The input ends inside a structure
        BadHeader,          ///< Header prefix present but its length field or magic is not understood
        UnsupportedVersion, ///< Header version outside the range this reader knows
        ImplausibleCount,   ///< A count cannot fit in the bytes that remain, or exceeds a hard limit
        BadClassTable,      ///< Class table is malformed (nesting too deep, ...)
        BadClassIndex,      ///< An object names a class that is not in the table
        BadObjectTable,     ///< An object's attributes run past the end of the data
        TrailingData,       ///< Bytes remain after the object table that are not the checksum
        ChecksumMismatch,   ///< Stored checksum differs from the computed one
    };

    [[nodiscard]] const char* to_string(Error e) noexcept;

    /// Hard limits that keep recursion and allocation bounded on hostile input.
    inline constexpr std::uint32_t kMaxClasses = 0x3fff;
    inline constexpr std::size_t kMaxAttributeDepth = 16;

    /// Size of the optional header (prefix plus body) in bytes.
    inline constexpr std::size_t kHeaderSize = 0x50;

    /// Header of the newer file generation. Older files have none: the class table starts at offset 0.
    struct Header
    {
        /// The 16 words after the 16-byte prefix, as stored. Index 0 is the format generation.
        std::array<std::uint32_t, 16> words { };
        [[nodiscard]] std::uint32_t version() const noexcept
        {
            return words[0];
        }
    };

    /// One field of a class. Fixed fields have `size > 0` and occupy exactly that many bytes in every object.
    /// Variable fields have `size == 0` and are stored as a u32 byte length followed by that many bytes.
    struct Attribute
    {
        std::string_view name;
        std::string_view type;
        std::uint32_t size = 0;
        std::vector<Attribute> children; ///< Element layout for compound types, empty otherwise
    };

    struct Class
    {
        std::string_view name;
        std::vector<Attribute> attributes;
    };

    /// One object record. `bytes` covers the whole record including the class index and id.
    struct Object
    {
        std::uint32_t class_index = 0;
        std::uint64_t id = 0;
        std::size_t offset = 0; ///< Offset of the record in the input
        std::span<const std::byte> bytes;
        /// The attribute data after the class index and id.
        [[nodiscard]] std::span<const std::byte> payload() const noexcept
        {
            return bytes.subspan(12);
        }
    };

    enum class SectionKind
    {
        Header,
        ClassTable,
        ObjectTable,
        Checksum
    };

    struct Section
    {
        SectionKind kind;
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    [[nodiscard]] const char* to_string(SectionKind k) noexcept;

    struct SaveFile
    {
        bool has_header = false;
        Header header;
        std::vector<Class> classes;
        std::vector<Object> objects;
        std::vector<Section> sections;       ///< In file order, tiling the input exactly
        bool has_checksum = false;           ///< True exactly when the file has a header
        std::uint32_t stored_checksum = 0;   ///< Valid when has_checksum
        std::uint32_t computed_checksum = 0; ///< Valid when has_checksum
    };

    /// The running sum stored in the trailer: a byte-wise stream-cipher recurrence over the whole file before
    /// the trailer, summing the produced cipher bytes into a wrapping u32.
    [[nodiscard]] std::uint32_t compute_checksum(std::span<const std::byte> data) noexcept;

    struct ReadOptions
    {
        /// When false a checksum mismatch is not an error; the values are still reported.
        bool verify_checksum = true;
    };

    /// Parses and validates the container. The returned views point into `data`, which must outlive them.
    /// The reader allocates at most a small multiple of the input size.
    [[nodiscard]] std::expected<SaveFile, Error> read_save(std::span<const std::byte> data, const ReadOptions& options = { });

} // namespace rct3::save
