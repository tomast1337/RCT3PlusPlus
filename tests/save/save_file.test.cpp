// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/save_file.hpp"

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::save;

namespace
{

    // Synthetic file builder. Class and field names are invented.
    struct Builder
    {
        std::vector<std::byte> bytes;

        void u8(std::uint8_t v)
        {
            bytes.push_back(static_cast<std::byte>(v));
        }
        void u16(std::uint16_t v)
        {
            put(&v, 2);
        }
        void u32(std::uint32_t v)
        {
            put(&v, 4);
        }
        void u64(std::uint64_t v)
        {
            put(&v, 8);
        }
        void str16(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            for (const char c : s)
                u8(static_cast<std::uint8_t>(c));
        }
        void put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            bytes.insert(bytes.end(), b, b + n);
        }
        // name, type, size, child count (children appended by the caller)
        void attribute(const std::string& name, const std::string& type, std::uint32_t size, std::uint32_t children = 0)
        {
            str16(name);
            str16(type);
            u32(size);
            u32(children);
        }
        void header(std::uint32_t version)
        {
            u32(0);
            u32(0);
            u32(0xF11EDA2Au);
            u32(0x40);
            u32(version);
            for (int i = 0; i < 15; ++i)
                u32(0);
        }
        /// The short form some effect files carry: its own magic, a 1, then twelve words.
        void short_header(std::uint32_t version)
        {
            u32(0);
            u32(0);
            u32(0xF11EDA1Au);
            u32(1);
            u32(version);
            for (int i = 0; i < 11; ++i)
                u32(0);
        }
        void checksum()
        {
            const auto sum = compute_checksum(bytes);
            u32(sum);
        }
    };

    // Two classes: "Widget" with a fixed u32 and a variable blob, "Gadget" with a compound field.
    void class_table(Builder& b)
    {
        b.u32(2);
        b.str16("Widget");
        b.u32(2);
        b.attribute("count", "u32", 4);
        b.attribute("label", "text", 0);
        b.str16("Gadget");
        b.u32(1);
        b.attribute("items", "seq", 0, 1);
        b.attribute("element", "u16", 2);
    }

    // Three objects: Widget, Gadget, Widget.
    void objects(Builder& b)
    {
        b.u32(3);
        b.u32(0);
        b.u64(7);
        b.u32(42);
        b.u32(3);
        b.u8('a');
        b.u8('b');
        b.u8('c');
        b.u32(1);
        b.u64(9);
        b.u32(6); // payload length
        b.u32(1); // element count
        b.u16(0xBEEF);
        b.u32(0);
        b.u64(11);
        b.u32(0);
        b.u32(0);
    }

    std::vector<std::byte> headerless()
    {
        Builder b;
        class_table(b);
        objects(b);
        return b.bytes;
    }

    std::vector<std::byte> headered(std::uint32_t version = 1)
    {
        Builder b;
        b.header(version);
        class_table(b);
        objects(b);
        b.checksum();
        return b.bytes;
    }

} // namespace

TEST_CASE("checksum matches an independently computed value")
{
    const std::byte data[] = { std::byte { 0 }, std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 250 }, std::byte { 17 } };
    CHECK(compute_checksum(data) == 882u);
    CHECK(compute_checksum({ }) == 0u);
}

TEST_CASE("headerless file parses into classes and objects")
{
    const auto bytes = headerless();
    const auto file = read_save(bytes);
    REQUIRE(file);
    CHECK_FALSE(file->has_header);
    CHECK_FALSE(file->has_checksum);
    REQUIRE(file->classes.size() == 2);
    CHECK(file->classes[0].name == "Widget");
    REQUIRE(file->classes[0].attributes.size() == 2);
    CHECK(file->classes[0].attributes[0].size == 4);
    CHECK(file->classes[0].attributes[1].size == 0);
    REQUIRE(file->classes[1].attributes.size() == 1);
    REQUIRE(file->classes[1].attributes[0].children.size() == 1);
    CHECK(file->classes[1].attributes[0].children[0].type == "u16");

    REQUIRE(file->objects.size() == 3);
    CHECK(file->objects[0].class_index == 0);
    CHECK(file->objects[0].id == 7);
    CHECK(file->objects[0].payload().size() == 4 + 4 + 3);
    CHECK(file->objects[1].class_index == 1);
    CHECK(file->objects[1].id == 9);
    CHECK(file->objects[2].id == 11);

    // Sections tile the input exactly and objects lie inside the object table.
    std::size_t at = 0;
    for (const auto& s : file->sections)
    {
        CHECK(s.offset == at);
        at += s.size;
    }
    CHECK(at == bytes.size());
    REQUIRE(file->sections.size() == 2);
    CHECK(file->sections[1].kind == SectionKind::ObjectTable);
}

TEST_CASE("headered file has header section and verified checksum")
{
    const auto bytes = headered(2);
    const auto file = read_save(bytes);
    REQUIRE(file);
    CHECK(file->has_header);
    CHECK(file->header.version() == 2);
    REQUIRE(file->has_checksum);
    CHECK(file->stored_checksum == file->computed_checksum);
    REQUIRE(file->sections.size() == 4);
    CHECK(file->sections[0].kind == SectionKind::Header);
    CHECK(file->sections[0].size == kHeaderSize);
    CHECK(file->sections[3].kind == SectionKind::Checksum);
    CHECK(file->objects.size() == 3);
}

TEST_CASE("the short header is read like the full one")
{
    Builder b;
    b.short_header(2);
    class_table(b);
    objects(b);
    b.checksum();
    const auto file = read_save(b.bytes);
    REQUIRE(file);
    CHECK(file->header.version() == 2);
    CHECK(file->sections[0].size == 0x40);
    CHECK(file->stored_checksum == file->computed_checksum);
    CHECK(file->objects.size() == 3);

    // Its mark must be 1.
    auto marked = b.bytes;
    marked[12] = std::byte { 2 };
    CHECK(read_save(marked).error() == Error::BadHeader);
}

TEST_CASE("checksum mismatch is an error unless verification is off")
{
    auto bytes = headered();
    bytes.back() ^= std::byte { 0xFF };
    CHECK(read_save(bytes).error() == Error::ChecksumMismatch);
    const auto file = read_save(bytes, ReadOptions { .verify_checksum = false });
    REQUIRE(file);
    CHECK(file->stored_checksum != file->computed_checksum);

    // A corrupted body byte is caught the same way.
    auto body = headered();
    body[kHeaderSize + 8] ^= std::byte { 0x01 };
    const auto r = read_save(body);
    REQUIRE_FALSE(r);
}

TEST_CASE("unsupported versions and bad header")
{
    SUBCASE("version zero")
    {
        CHECK(read_save(headered(0)).error() == Error::UnsupportedVersion);
    }
    SUBCASE("version too new")
    {
        CHECK(read_save(headered(3)).error() == Error::UnsupportedVersion);
    }
    SUBCASE("wrong body length")
    {
        auto bytes = headered();
        bytes[12] = std::byte { 0x41 };
        CHECK(read_save(bytes).error() == Error::BadHeader);
    }
}

TEST_CASE("every truncation of a valid file fails cleanly")
{
    for (const auto& bytes : { headerless(), headered() })
    {
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            // Eight zero bytes are, by themselves, a valid headerless file with no classes and no objects.
            if (n == 8)
                continue;
            const auto prefix = std::span(bytes).first(n);
            CHECK_FALSE(read_save(prefix));
        }
    }
}

TEST_CASE("structural corruption")
{
    SUBCASE("object names an unknown class")
    {
        auto bytes = headerless();
        // First object's class index follows the class table and the u32 object count.
        Builder t;
        class_table(t);
        bytes[t.bytes.size() + 4] = std::byte { 9 };
        CHECK(read_save(bytes).error() == Error::BadClassIndex);
    }
    SUBCASE("variable field length runs past the end")
    {
        auto bytes = headerless();
        Builder t;
        class_table(t);
        // First object: class, id, fixed u32, then the blob length.
        const auto length_at = t.bytes.size() + 4 + 12 + 4;
        bytes[length_at + 3] = std::byte { 0x7F };
        CHECK(read_save(bytes).error() == Error::BadObjectTable);
    }
    SUBCASE("trailing bytes after the objects")
    {
        auto bytes = headerless();
        bytes.push_back(std::byte { 0 });
        CHECK(read_save(bytes).error() == Error::TrailingData);
    }
    SUBCASE("object count larger than the data")
    {
        Builder b;
        class_table(b);
        b.u32(0xFFFFFFFFu);
        CHECK(read_save(b.bytes).error() == Error::ImplausibleCount);
    }
    SUBCASE("class count over the hard limit")
    {
        Builder b;
        b.u32(kMaxClasses + 1);
        b.bytes.resize(b.bytes.size() + 4 * (kMaxClasses + 1) * 2);
        CHECK(read_save(b.bytes).error() == Error::ImplausibleCount);
    }
    SUBCASE("attribute count larger than the data")
    {
        Builder b;
        b.u32(1);
        b.str16("Widget");
        b.u32(0x40000000u);
        CHECK(read_save(b.bytes).error() == Error::ImplausibleCount);
    }
    SUBCASE("nesting beyond the limit")
    {
        Builder b;
        b.u32(1);
        b.str16("Widget");
        b.u32(1);
        for (std::size_t d = 0; d <= kMaxAttributeDepth + 1; ++d)
            b.attribute("n", "seq", 0, 1);
        b.attribute("leaf", "u8", 1);
        b.u32(0);
        CHECK(read_save(b.bytes).error() == Error::BadClassTable);
    }
}

TEST_CASE("empty and tiny inputs")
{
    CHECK_FALSE(read_save({ }));
    const std::byte three[3] = { };
    CHECK_FALSE(read_save(three));
    Builder zero; // zero classes, zero objects: valid, empty
    zero.u32(0);
    zero.u32(0);
    const auto r = read_save(zero.bytes);
    REQUIRE(r);
    CHECK(r->classes.empty());
    CHECK(r->objects.empty());
}
