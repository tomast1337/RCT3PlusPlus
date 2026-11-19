// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/structure.hpp"

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::save;
using namespace rct3::save::model;

namespace
{

    // A minimal writer of the save container, enough for an invented structure file.
    struct Bytes
    {
        std::vector<std::byte> v;
        Bytes& put_u8(std::uint8_t x)
        {
            v.push_back(static_cast<std::byte>(x));
            return *this;
        }
        Bytes& put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& put_u16(std::uint16_t x)
        {
            return put(&x, 2);
        }
        Bytes& put_u32(std::uint32_t x)
        {
            return put(&x, 4);
        }
        Bytes& put_i32(std::int32_t x)
        {
            return put(&x, 4);
        }
        Bytes& put_u64(std::uint64_t x)
        {
            return put(&x, 8);
        }
        Bytes& cat(const Bytes& o)
        {
            v.insert(v.end(), o.v.begin(), o.v.end());
            return *this;
        }
        Bytes& name(const std::string& s)
        {
            put_u16(static_cast<std::uint16_t>(s.size()));
            for (const char c : s)
                put_u8(static_cast<std::uint8_t>(c));
            return *this;
        }
        Bytes& blob(const Bytes& payload)
        {
            put_u32(static_cast<std::uint32_t>(payload.v.size()));
            return cat(payload);
        }
        Bytes& text(const std::u16string& s)
        {
            Bytes p;
            p.put_u32(0xEFEFEFEFu);
            for (const auto c : s)
                p.put_u16(c);
            return blob(p);
        }
    };

    struct Attr
    {
        std::string name, type;
        std::uint32_t size = 0;
        std::vector<Attr> children;
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).put_u32(a.size).put_u32(static_cast<std::uint32_t>(a.children.size()));
        for (const auto& c : a.children)
            write_attr(b, c);
    }

    Attr i32a(const char* n)
    {
        return { n, "int32", 4, { } };
    }

    struct Piece
    {
        std::int32_t col, row, height, dir, corner;
        std::uint64_t sid;
    };

    /// An invented structure of `cols` x `rows` tiles with the pieces given, and two scenery entries (ids 20, 21).
    std::vector<std::byte> structure_file(std::int32_t cols, std::int32_t rows, const std::vector<bool>& selected, const std::vector<Piece>& pieces)
    {
        const std::vector<Attr> piece_fields = { i32a("Col"), i32a("Corner"), i32a("Dir"), i32a("FlexiPick0"), i32a("FlexiPick1"), i32a("FlexiPick2"), i32a("Height"), i32a("Row"), { "SID", "reference", 8, { } } };
        Bytes b;
        b.put_u32(2);
        b.name("SavedSelection").put_u32(5);
        write_attr(b, i32a("Cols"));
        write_attr(b, { "Name", "string", 0, { } });
        write_attr(b, i32a("Rows"));
        write_attr(b, { "Scenery", "array", 0, piece_fields });
        write_attr(b, { "Selected", "array", 0, { { "Selected", "bool", 1, { } } } });
        b.name("SIDDatabaseEntry").put_u32(2);
        write_attr(b, { "OVERLAYFILENAME", "string", 0, { } });
        write_attr(b, { "SYMBOLNAME", "string", 0, { } });

        b.put_u32(3);
        Bytes s;
        s.put_i32(cols).text(u"Gate house").put_i32(rows);
        Bytes list;
        list.put_u32(static_cast<std::uint32_t>(pieces.size()));
        for (const Piece& p : pieces)
            list.put_i32(p.col).put_i32(p.corner).put_i32(p.dir).put_i32(4).put_i32(5).put_i32(6).put_i32(p.height).put_i32(p.row).put_u64(p.sid);
        s.blob(list);
        Bytes marks;
        marks.put_u32(static_cast<std::uint32_t>(selected.size()));
        for (const bool m : selected)
            marks.put_u8(m ? 1 : 0);
        s.blob(marks);
        b.put_u32(0).put_u64(10).cat(s);
        b.put_u32(1).put_u64(20).text(u"Style/Themed/Castle/style").text(u"wall_a");
        b.put_u32(1).put_u64(21).text(u"Style/Themed/Castle/style").text(u"");
        return b.v;
    }

} // namespace

TEST_CASE("a structure file gives its box, its tiles and its items, the lowest at height 0")
{
    const auto bytes = structure_file(2, 1, { true, false }, { { 0, 0, 40, 3, 1, 20 }, { 1, 0, 43, 5, 2, 20 } });
    const auto s = read_structure(bytes);
    REQUIRE(s);
    CHECK(s->name == "Gate house");
    CHECK(s->cols == 2);
    CHECK(s->rows == 1);
    CHECK(s->has_tile(0, 0));
    CHECK_FALSE(s->has_tile(1, 0));
    CHECK_FALSE(s->has_tile(2, 0));
    REQUIRE(s->pieces.size() == 2);
    CHECK(s->pieces[0].symbol == "wall_a");
    CHECK(s->pieces[0].overlay == "Style/Themed/Castle/style");
    CHECK(s->pieces[0].height == 0);
    CHECK(s->pieces[1].height == 3);
    CHECK(s->pieces[1].col == 1);
    CHECK(s->pieces[1].rotation == 1); // 5 turns are one
    CHECK(s->pieces[0].quadrant == 1);
    CHECK(s->pieces[0].colours == std::array<std::int32_t, 3> { 4, 5, 6 });
}

TEST_CASE("a structure item whose entry names nothing is left out, and a short tile list counts as unmarked")
{
    const auto s = read_structure(structure_file(2, 2, { true }, { { 0, 0, 7, 0, 0, 21 }, { 0, 0, 9, 0, 0, 99 }, { 1, 1, 8, 0, 0, 20 } }));
    REQUIRE(s);
    REQUIRE(s->pieces.size() == 1);
    CHECK(s->pieces[0].height == 0);
    CHECK(s->tiles == std::vector<std::uint8_t> { 1, 0, 0, 0 });
}

TEST_CASE("a file without a structure, or with an empty box, is refused")
{
    CHECK(read_structure(structure_file(0, 1, { }, { })).error() == StructureError::NoStructure);
    const std::vector<std::byte> junk(5, std::byte { 7 });
    CHECK(read_structure(junk).error() == StructureError::NotASave);
}
