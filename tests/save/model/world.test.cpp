// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/world.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <string>
#include <vector>

using namespace rct3::save;

namespace
{

    // ---- Synthetic byte builders ---------------------------------------------------------------------------

    struct Bytes
    {
        std::vector<std::byte> v;
        Bytes& u8(std::uint8_t x)
        {
            v.push_back(static_cast<std::byte>(x));
            return *this;
        }
        Bytes& u16(std::uint16_t x)
        {
            return put(&x, 2);
        }
        Bytes& u32(std::uint32_t x)
        {
            return put(&x, 4);
        }
        Bytes& i32(std::int32_t x)
        {
            return put(&x, 4);
        }
        Bytes& u64(std::uint64_t x)
        {
            return put(&x, 8);
        }
        Bytes& f32(float x)
        {
            return put(&x, 4);
        }
        Bytes& put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& cat(const Bytes& o)
        {
            v.insert(v.end(), o.v.begin(), o.v.end());
            return *this;
        }
        Bytes& name(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            for (const char c : s)
                u8(static_cast<std::uint8_t>(c));
            return *this;
        }
        Bytes& blob(const Bytes& payload)
        {
            u32(static_cast<std::uint32_t>(payload.v.size()));
            return cat(payload);
        }
        Bytes& text(const std::u16string& s)
        {
            Bytes p;
            p.u32(0xEFEFEFEFu);
            for (const auto c : s)
                p.u16(c);
            return blob(p);
        }
        [[nodiscard]] std::span<const std::byte> span() const
        {
            return v;
        }
    };

    // A terrain cell in the 24-byte layout. Invented values.
    Bytes& cell24(Bytes& b, float h0, float h1, float h2, float h3, std::uint8_t surface, std::uint8_t cliff, std::uint8_t flags)
    {
        b.f32(h0).f32(h1).f32(h2).f32(h3).u8(surface).u8(cliff).u8(0).u8(0).u8(flags).u8(0xFF).u8(0).u8(0);
        return b;
    }

    Bytes& cell20(Bytes& b, float h0, float h1, float h2, float h3, std::uint8_t surface, std::uint8_t cliff, std::uint8_t flag_nibble)
    {
        b.f32(h0).f32(h1).f32(h2).f32(h3).u8(surface).u8(cliff).u8(0).u8(flag_nibble);
        return b;
    }

    Bytes terrain_header(int w, int h, float ox = -8, float oz = -12, float tx = 4, float tz = 4)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(w)).u8(static_cast<std::uint8_t>(h)).f32(ox).f32(oz).f32(tx).f32(tz);
        return b;
    }

    // A 3x2 terrain with a recognisable height pattern: corner of vertex (x,z) = x + 10 * z.
    Bytes make_terrain_3x2(bool legacy = false)
    {
        Bytes b = terrain_header(3, 2);
        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const auto v = [&](int dx, int dz) { return static_cast<float>(col + dx + 10 * (row + dz)); };
                const std::uint8_t flags = static_cast<std::uint8_t>(col == 0 ? 0x02 : col == 1 ? 0x05 : 0x40);
                if (legacy)
                    cell20(b, v(0, 0), v(1, 0), v(0, 1), v(1, 1), static_cast<std::uint8_t>(col), 1, flags & 0x0f);
                else
                    cell24(b, v(0, 0), v(1, 0), v(0, 1), v(1, 1), static_cast<std::uint8_t>(col), 1, flags);
            }
        }
        return b;
    }

    Bytes make_water(int w, int h, const std::vector<std::pair<float, std::vector<WaterTri>>>& bodies)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(w)).u8(static_cast<std::uint8_t>(h)).u32(static_cast<std::uint32_t>(bodies.size()));
        for (const auto& [level, tris] : bodies)
        {
            b.f32(level).u32(static_cast<std::uint32_t>(tris.size()));
            for (const auto& t : tris)
                b.u8(t.col).u8(t.row).u8(t.triangle).u8(t.mask);
        }
        return b;
    }

    // ---- Synthetic save builder ----------------------------------------------------------------------------

    struct Attr
    {
        std::string name, type;
        std::uint32_t size = 0;
        std::vector<Attr> children;
    };

    struct Cls
    {
        std::string name;
        std::vector<Attr> attrs;
    };

    struct Obj
    {
        std::uint32_t cls;
        std::uint64_t id;
        Bytes payload;
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
        for (const auto& c : a.children)
            write_attr(b, c);
    }

    std::vector<std::byte> make_save(const std::vector<Cls>& classes, const std::vector<Obj>& objects)
    {
        Bytes b;
        b.u32(static_cast<std::uint32_t>(classes.size()));
        for (const auto& c : classes)
        {
            b.name(c.name).u32(static_cast<std::uint32_t>(c.attrs.size()));
            for (const auto& a : c.attrs)
                write_attr(b, a);
        }
        b.u32(static_cast<std::uint32_t>(objects.size()));
        for (const auto& o : objects)
            b.u32(o.cls).u64(o.id).cat(o.payload);
        return b.v;
    }

    // Class indices of the synthetic schema.
    enum : std::uint32_t
    {
        kTerrain,
        kWater,
        kPathFlying,
        kPathQueue,
        kPathPlain,
        kSurface,
        kScenery,
        kEntry,
        kWeather,
        kSky,
        kTime,
        kCamera,
        kQueueLine,
        kGroundSurface,
        kBin,
        kSign,
        kBench,
        kPool,
    };

    Attr u8a(const char* n)
    {
        return { n, "uint8", 1, { } };
    }
    Attr i32a(const char* n)
    {
        return { n, "int32", 4, { } };
    }
    Attr f32a(const char* n)
    {
        return { n, "float32", 4, { } };
    }
    Attr refa(const char* n)
    {
        return { n, "reference", 8, { } };
    }

    Attr colours(const char* n)
    {
        return { n, "struct", 12, { i32a("COL0"), i32a("COL1"), i32a("COL2") } };
    }
    Attr rgb(const char* n)
    {
        return { n, "struct", 12, { f32a("Blue"), f32a("Green"), f32a("Red") } };
    }

    std::vector<Cls> schema(bool with_adjust = true)
    {
        std::vector<Attr> data = { i32a("CORNER"), i32a("DIRECTION"), i32a("HEIGHT") };
        if (with_adjust)
            data.push_back(f32a("HEIGHTADJUST"));
        data.push_back(i32a("POSX"));
        data.push_back(i32a("POSZ"));
        const std::uint32_t data_size = with_adjust ? 24 : 20;
        const std::vector<Attr> path_base = { u8a("ColIndex"), u8a("Direction"), u8a("PathType"), u8a("RowIndex"), refa("Surface"), u8a("SurfaceType") };
        auto flying = path_base;
        for (const auto& a : { u8a("SlopeType"), i32a("BaseHeight"), i32a("QuantisedHeight"), refa("SceneryItem") })
            flying.push_back(a);
        auto queue = flying;
        for (const auto& a : { u8a("StartDirection"), u8a("EndDirection"), colours("FenceFlexiColours"), refa("QueueLine"), refa("FenceEntry") })
            queue.push_back(a);
        return {
            { "RCT3Terrain", { { "EngineTerrain", "GE_Terrain", 0, { } }, { "SkirtTrees", "SkirtTrees", 0, { } }, { "BlockingScenery", "BlockingScenery", 0, { } } } },
            { "WaterManager", { { "WaterManager", "WaterManager", 0, { } } } },
            { "PathFlying", flying },
            { "PathQueue", queue },
            { "PathTile", path_base },
            { "PathTypeDatabaseEntry", { { "SystemName", "string", 0, { } } } },
            { "SceneryItem", { { "SceneryItemDataField", "struct", data_size, data }, refa("DATABASEENTRY"), refa("Owner"), colours("FlexiColourField"), i32a("BREAKFLAGS"), { "IsHidden", "bool", 1, { } }, { "BehaviourArray", "array", 0, { refa("BehaviourReference") } } } },
            { "SIDDatabaseEntry", { { "SYMBOLNAME", "string", 0, { } }, { "OVERLAYFILENAME", "string", 0, { } } } },
            { "Weather", { i32a("Type"), i32a("ForecastType"), f32a("Temperature"), f32a("ThunderTime"), { "FogSetting", "struct", 24, { rgb("FColourValue"), f32a("Far"), f32a("Near"), f32a("ShadowDamper") } } } },
            { "Sky", { i32a("BackgroundSave") } },
            { "GameTime", { f32a("DayNightTime"), f32a("Time"), f32a("ZeroTime") } },
            { "MainCamera", { { "Or", "orientation", 12, { } }, { "Pos", "vector3", 12, { } } } },
            { "QueueLine", { refa("HeadTile"), i32a("QueueSize"), refa("RideInstance"), i32a("StationIndex") } },
            { "QueueTypeGroundSurface", { colours("Colours"), refa("QueueType") } },
            { "LitterBinBehaviour", { refa("\"SceneryItem\""), f32a("Capacity"), f32a("Contents"), f32a("LastEmptied") } },
            { "SignBehaviour", { refa("\"SceneryItem\""), i32a("SignType"), { "UserText", "string", 0, { } } } },
            { "ParkBenchBehaviour", { refa("\"SceneryItem\"") } },
            { "PoolSegment", { i32a("Col"), i32a("Depth"), { "Prototype", "bool", 1, { } }, i32a("Row"), i32a("SegmentType"), i32a("SurfaceHeight") } },
        };
    }

    Obj terrain_obj(std::uint64_t id, const Bytes& blob, const Bytes& skirt = Bytes().u32(0), const Bytes& blocking = Bytes().i32(-1))
    {
        Bytes p;
        p.blob(blob).blob(skirt).blob(blocking);
        return { kTerrain, id, p };
    }

    Obj water_obj(std::uint64_t id, const Bytes& blob)
    {
        Bytes p;
        p.blob(blob);
        return { kWater, id, p };
    }

    Obj path_obj(std::uint32_t cls, std::uint64_t id, std::uint8_t col, std::uint8_t row, std::uint8_t kind, std::uint64_t scenery = 0, std::int32_t base = -1, std::int32_t quant = 0, std::uint8_t slope = 0, std::uint64_t fence = 0)
    {
        Bytes p;
        p.u8(col).u8(255).u8(kind).u8(row).u64(0).u8(255);
        if (cls != kPathPlain)
        {
            p.u8(slope).i32(base).i32(quant).u64(scenery);
            if (cls == kPathQueue)
                p.u8(1).u8(2).i32(7).i32(0).i32(0).u64(500).u64(fence);
        }
        return { cls, id, p };
    }

    Obj scenery_obj(std::uint64_t id, std::int32_t col, std::int32_t row, std::int32_t height, std::uint64_t def, std::uint64_t owner, bool with_adjust = true, std::uint32_t behaviours = 0)
    {
        Bytes p;
        p.i32(2).i32(1).i32(height);
        if (with_adjust)
            p.f32(0.25f);
        p.i32(col).i32(row);
        p.u64(def).u64(owner).i32(3).i32(1).i32(2).i32(35).u8(0);
        Bytes list;
        list.u32(behaviours);
        for (std::uint32_t i = 0; i < behaviours; ++i)
            list.u64(900 + i);
        p.blob(list);
        return { kScenery, id, p };
    }

    Obj entry_obj(std::uint64_t id, const std::u16string& symbol, const std::u16string& overlay = u"Scenery/Trees")
    {
        Bytes p;
        p.text(symbol);
        p.text(overlay);
        return { kEntry, id, p };
    }

    struct Loaded
    {
        std::vector<std::byte> bytes;
        SaveFile file;
        std::expected<WorldModel, Error> world = std::unexpected(Error::Truncated);
    };

    std::unique_ptr<Loaded> load(const std::vector<Cls>& classes, const std::vector<Obj>& objects)
    {
        auto l = std::make_unique<Loaded>();
        l->bytes = make_save(classes, objects);
        auto f = read_save(l->bytes, { .verify_checksum = false });
        REQUIRE(f);
        l->file = std::move(*f);
        const ObjectIndex index(l->file);
        l->world = load_world(l->file, index);
        return l;
    }

} // namespace

// ---- Terrain -------------------------------------------------------------------------------------------

TEST_CASE("terrain: 24-byte cells decode in row-major order")
{
    const auto blob = make_terrain_3x2();
    const auto g = decode_terrain(blob.span());
    REQUIRE(g);
    CHECK(g->width == 3);
    CHECK(g->height == 2);
    CHECK(g->origin_x == -8.0f);
    CHECK(g->origin_z == -12.0f);
    CHECK(g->tile_size_x == 4.0f);
    CHECK(g->layout == TerrainCellLayout::Cell24);
    CHECK(g->trailing_bytes == 0);
    REQUIRE(g->cells.size() == 6);

    // cell (col 2, row 1): corners are the vertices (2,1) (3,1) (2,2) (3,2)
    const auto& c = g->at(2, 1);
    CHECK(c.corners[0] == 12.0f);
    CHECK(c.corners[1] == 13.0f);
    CHECK(c.corners[2] == 22.0f);
    CHECK(c.corners[3] == 23.0f);
    CHECK(c.surface == 2);
    CHECK(c.cliff == 1);
    CHECK(c.min_height() == 12.0f);
    CHECK(c.max_height() == 23.0f);
    CHECK(g->at(0, 0).surface == 0);
    CHECK(g->at(1, 0).corners[0] == 1.0f);  // moving one column increases the first corner by one
    CHECK(g->at(0, 1).corners[0] == 10.0f); // moving one row by ten

    CHECK(g->world_x(2) == 0.0f);
    CHECK(g->world_z(1) == -8.0f);
    CHECK(g->contains(2, 1));
    CHECK_FALSE(g->contains(3, 0));
    CHECK_FALSE(g->contains(0, -1));
}

TEST_CASE("terrain: flag bits and ownership")
{
    const auto g = decode_terrain(make_terrain_3x2().span());
    REQUIRE(g);
    CHECK(g->at(0, 0).has(TileFlag::Owned));
    CHECK_FALSE(g->at(0, 0).has(TileFlag::ForSale));
    CHECK(g->at(0, 0).ownership() == Ownership::Owned);
    CHECK(g->at(1, 0).ownership() == Ownership::ForSaleRights);
    CHECK(g->at(2, 0).ownership() == Ownership::Locked);

    TerrainCell c;
    CHECK(c.ownership() == Ownership::Unowned);
    c.flags = 0x06;
    CHECK(c.ownership() == Ownership::OwnedRights);
    c.flags = 0x22; // owned plus the transient entrance bit
    CHECK(c.ownership() == Ownership::Owned);
    CHECK(c.has(TileFlag::EntranceFront));
    c.flags = 0x01;
    CHECK(c.ownership() == Ownership::ForSale);
    c.flags = 0x80; // unknown bit survives but means nothing
    CHECK(c.ownership() == Ownership::Unowned);
}

TEST_CASE("terrain: 20-byte legacy cells")
{
    const auto blob = make_terrain_3x2(true);
    CHECK(blob.v.size() == 18 + 6 * 20);
    const auto g = decode_terrain(blob.span());
    REQUIRE(g);
    CHECK(g->layout == TerrainCellLayout::Cell20);
    CHECK(g->at(2, 1).corners[3] == 23.0f);
    CHECK(g->at(1, 1).surface == 1);
    CHECK(g->at(0, 0).flags == 0x02);
    CHECK(g->at(1, 0).flags == 0x05);
    CHECK(g->at(2, 0).flags == 0x00); // 0x40 does not fit the legacy nibble
}

TEST_CASE("terrain: a few unused trailing bytes are tolerated and counted")
{
    auto blob = make_terrain_3x2();
    for (int i = 0; i < 8; ++i)
        blob.u8(0xAA);
    const auto g = decode_terrain(blob.span());
    REQUIRE(g);
    CHECK(g->trailing_bytes == 8);
    blob.u8(0);
    CHECK(decode_terrain(blob.span()).error() == Error::ImplausibleCount);
}

TEST_CASE("terrain: wrong sizes and hostile headers are errors")
{
    SUBCASE("shorter than the header")
    {
        const Bytes b = Bytes().u8(3).u8(3);
        CHECK(decode_terrain(b.span()).error() == Error::Truncated);
        CHECK(decode_terrain({ }).error() == Error::Truncated);
    }
    SUBCASE("cells cut short")
    {
        auto blob = make_terrain_3x2();
        blob.v.pop_back();
        CHECK(decode_terrain(blob.span()).error() == Error::Truncated);
        blob.v.resize(18 + 6 * 20 - 1);
        CHECK(decode_terrain(blob.span()).error() == Error::Truncated);
    }
    SUBCASE("oversized dimensions with a tiny blob do not allocate")
    {
        const Bytes b = terrain_header(255, 255);
        const auto r = decode_terrain(b.span());
        REQUIRE_FALSE(r);
        CHECK(r.error() == Error::Truncated);
    }
    SUBCASE("a size that fits neither layout")
    {
        Bytes b = terrain_header(2, 2);
        for (int i = 0; i < 4 * 24 + 9; ++i)
            b.u8(0); // more than one layout plus slack
        CHECK(decode_terrain(b.span()).error() == Error::ImplausibleCount);
    }
    SUBCASE("zero dimension")
    {
        CHECK(decode_terrain(terrain_header(0, 4).span()).error() == Error::ImplausibleCount);
        CHECK(decode_terrain(terrain_header(4, 0).span()).error() == Error::ImplausibleCount);
    }
    SUBCASE("tile size must be positive and finite")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Bytes a = terrain_header(1, 1, 0, 0, 0.0f, 4.0f);
        cell24(a, 0, 0, 0, 0, 0, 0, 0);
        CHECK(decode_terrain(a.span()).error() == Error::BadHeader);
        Bytes b = terrain_header(1, 1, nan, 0, 4.0f, 4.0f);
        cell24(b, 0, 0, 0, 0, 0, 0, 0);
        CHECK(decode_terrain(b.span()).error() == Error::BadHeader);
    }
    SUBCASE("full-size map decodes")
    {
        Bytes b = terrain_header(255, 255);
        b.v.resize(18 + 255u * 255u * 24u);
        const auto g = decode_terrain(b.span());
        REQUIRE(g);
        CHECK(g->cells.size() == kMaxTerrainCells);
    }
}

TEST_CASE("terrain: height queries follow the triangle split")
{
    const auto g = decode_terrain(make_terrain_3x2().span());
    REQUIRE(g);
    // Origin (-8,-12), tile 4. The first tile spans x -8..-4, z -12..-8 with corners 0,1,10,11.
    CHECK(*g->height_at(-8, -12) == doctest::Approx(0.0f));
    CHECK(*g->height_at(-4, -12) == doctest::Approx(1.0f));
    CHECK(*g->height_at(-8, -8) == doctest::Approx(10.0f));
    CHECK(*g->height_at(-4, -8) == doctest::Approx(11.0f));
    CHECK(*g->height_at(-6, -10) == doctest::Approx(5.5f));
    // The surface in this test is planar, so both interpolations agree.
    CHECK(*g->height_bilinear_at(-5, -11) == doctest::Approx(*g->height_at(-5, -11)));
    CHECK(*g->height_at(-5, -11) == doctest::Approx(0.75f + 2.5f));
    // The far edge of the map is inside, anything beyond is not.
    CHECK(g->height_at(4, -4).has_value());
    CHECK_FALSE(g->height_at(4.1f, 0).has_value());
    CHECK_FALSE(g->height_at(-8.1f, 0).has_value());
    CHECK_FALSE(g->height_at(0, std::numeric_limits<float>::quiet_NaN()).has_value());
    CHECK_FALSE(TerrainGrid { }.height_at(0, 0).has_value());
}

TEST_CASE("terrain: the two interpolations differ on a warped tile")
{
    Bytes b = terrain_header(1, 1, 0, 0, 1, 1);
    cell24(b, 0, 0, 0, 4, 0, 0, 0);
    const auto g = decode_terrain(b.span());
    REQUIRE(g);
    // Centre of the tile: bilinear averages all four corners, the triangle split lies on the 1-2 diagonal.
    CHECK(*g->height_bilinear_at(0.5f, 0.5f) == doctest::Approx(1.0f));
    CHECK(*g->height_at(0.5f, 0.5f) == doctest::Approx(0.0f));
}

// ---- Water ---------------------------------------------------------------------------------------------

TEST_CASE("water: bodies decode and re-encode")
{
    const auto blob = make_water(3, 2, { { 2.5f, { { 0, 0, 0, 7 }, { 0, 0, 1, 1 } } }, { -1.0f, { { 2, 1, 1, 3 } } } });
    const auto w = decode_water(blob.span());
    REQUIRE(w);
    CHECK(w->width == 3);
    CHECK(w->height == 2);
    REQUIRE(w->bodies.size() == 2);
    CHECK(w->bodies[0].level == 2.5f);
    REQUIRE(w->bodies[0].tris.size() == 2);
    CHECK(w->bodies[0].tris[1].triangle == 1);
    CHECK(w->bodies[0].tris[1].mask == 1);
    CHECK(w->bodies[1].tris[0].col == 2);
    CHECK(w->bodies[1].tris[0].row == 1);
    CHECK(w->triangle_count() == 3);
}

TEST_CASE("water: an empty layer is valid")
{
    const auto blob = make_water(128, 128, { });
    CHECK(blob.v.size() == 6);
    const auto w = decode_water(blob.span());
    REQUIRE(w);
    CHECK(w->bodies.empty());
}

TEST_CASE("water: framing errors")
{
    SUBCASE("too short")
    {
        const Bytes b = Bytes().u8(1).u8(1).u16(0);
        CHECK(decode_water(b.span()).error() == Error::Truncated);
    }
    SUBCASE("hostile body count")
    {
        const Bytes b = Bytes().u8(1).u8(1).u32(0xFFFFFFFFu);
        CHECK(decode_water(b.span()).error() == Error::ImplausibleCount);
    }
    SUBCASE("hostile triangle count")
    {
        const Bytes b = Bytes().u8(1).u8(1).u32(1).f32(0).u32(0x7FFFFFFFu).u32(0);
        CHECK(decode_water(b.span()).error() == Error::ImplausibleCount);
    }
    SUBCASE("a body that is cut short")
    {
        auto blob = make_water(2, 2, { { 0.0f, { { 0, 0, 0, 1 }, { 1, 0, 0, 1 } } } });
        blob.v.resize(blob.v.size() - 2);
        CHECK_FALSE(decode_water(blob.span()));
    }
    SUBCASE("trailing bytes")
    {
        auto blob = make_water(2, 2, { });
        blob.u8(0);
        CHECK(decode_water(blob.span()).error() == Error::TrailingData);
    }
    SUBCASE("count promises more bodies than there are")
    {
        const Bytes b = Bytes().u8(1).u8(1).u32(3).f32(0).u32(0);
        CHECK_FALSE(decode_water(b.span()));
    }
}

TEST_CASE("water: submerged masks are recomputed from corner heights")
{
    const auto g = decode_terrain(make_terrain_3x2().span());
    REQUIRE(g);
    // Tile (0,0) corners are 0, 1, 10, 11.
    CHECK(*expected_water_mask(*g, 0.5f, 0, 0, 0) == 0b001); // only corner 0 below
    CHECK(*expected_water_mask(*g, 1.0f, 0, 0, 0) == 0b001); // strictly below: corner 1 is equal
    CHECK(*expected_water_mask(*g, 1.5f, 0, 0, 0) == 0b011);
    CHECK(*expected_water_mask(*g, 100.0f, 0, 0, 0) == 0b111);
    // Triangle 1 uses corners 3, 2, 1 in that order: heights 11, 10, 1.
    CHECK(*expected_water_mask(*g, 5.0f, 0, 0, 1) == 0b100);
    CHECK(*expected_water_mask(*g, 10.5f, 0, 0, 1) == 0b110);
    CHECK_FALSE(expected_water_mask(*g, 0, 3, 0, 0).has_value());
    CHECK_FALSE(expected_water_mask(*g, 0, 0, 0, 2).has_value());
}

// ---- Skirt trees ---------------------------------------------------------------------------------------

TEST_CASE("skirt trees")
{
    SUBCASE("none")
    {
        const auto t = decode_skirt_trees(Bytes().u32(0).span());
        REQUIRE(t);
        CHECK(t->empty());
    }
    SUBCASE("two entries")
    {
        Bytes b;
        b.u32(2).u64(77).f32(1).f32(2).f32(3).u64(78).f32(-4).f32(5).f32(-6);
        const auto t = decode_skirt_trees(b.span());
        REQUIRE(t);
        REQUIRE(t->size() == 2);
        CHECK((*t)[0].scenery_ref == 77);
        CHECK((*t)[1].position.z == -6.0f);
    }
    SUBCASE("hostile count")
    {
        CHECK(decode_skirt_trees(Bytes().u32(0xFFFFFFFFu).span()).error() == Error::ImplausibleCount);
    }
    SUBCASE("length mismatches")
    {
        CHECK(decode_skirt_trees(Bytes().u8(0).span()).error() == Error::Truncated);
        CHECK(decode_skirt_trees(Bytes().u32(0).u8(1).span()).error() == Error::TrailingData);
        Bytes b;
        b.u32(1).u64(1).f32(0).f32(0);
        CHECK_FALSE(decode_skirt_trees(b.span()));
    }
}

// ---- Loader --------------------------------------------------------------------------------------------

TEST_CASE("load_world: a complete synthetic save")
{
    Bytes skirt;
    skirt.u32(1).u64(300).f32(-50).f32(1).f32(60);

    Bytes weather;
    weather.i32(3).i32(9).f32(21.5f).f32(4).f32(0.1f).f32(0.2f).f32(0.3f).f32(6000).f32(4000).f32(0.5f);
    Bytes time;
    time.f32(0.4f).f32(120).f32(100);
    Bytes camera;
    camera.f32(0.1f).f32(0.2f).f32(0.3f).f32(10).f32(20).f32(30);

    Bytes surface;
    surface.text(u"Style_A");
    Bytes queue_line;
    queue_line.u64(210).i32(5).u64(0).i32(2);

    const auto l = load(
        schema(),
        {
            terrain_obj(1, make_terrain_3x2(), skirt),
            water_obj(2, make_water(3, 2, { { 2.5f, { { 0, 0, 0, 0b011 }, { 1, 0, 0, 0b011 } } } })),
            { kSurface, 60, surface },
            { kPathPlain, 200, path_obj(kPathPlain, 200, 1, 0, 0).payload },
            path_obj(kPathFlying, 201, 2, 1, 1, 400, -1, 4, 1),
            path_obj(kPathQueue, 210, 0, 1, 2, 0, 12, 0, 3, 70),
            { kQueueLine, 500, queue_line },
            entry_obj(70, u"Tree_01"),
            scenery_obj(300, 1, 1, 6, 70, 300, true, 2),
            scenery_obj(400, 2, 1, -32768, 70, 300),
            { kWeather, 3, weather },
            { kSky, 4, Bytes().i32(5) },
            { kTime, 5, time },
            { kCamera, 6, camera },
        });
    REQUIRE(l->world);
    const WorldModel& w = *l->world;
    CHECK(w.issues.empty());

    CHECK(w.terrain.width == 3);
    REQUIRE(w.blocking_scenery.size() == 4);
    REQUIRE(w.skirt_trees.size() == 1);
    CHECK(w.skirt_trees[0].position.x == -50.0f);
    CHECK(w.skirt_trees[0].symbol == "Tree_01"); // resolved through the placed object it names

    CHECK(w.water.bodies.size() == 1);
    CHECK(w.water.triangle_count() == 2);

    REQUIRE(w.paths.size() == 3);
    const auto& plain = w.paths[0];
    CHECK(plain.kind == PathKind::Ground);
    CHECK(plain.col == 1);
    CHECK(plain.direction == kNoDirection);
    CHECK_FALSE(plain.elevation.has_value());
    CHECK_FALSE(plain.queue.has_value());
    const auto& fly = w.paths[1];
    CHECK(fly.kind == PathKind::Flying);
    REQUIRE(fly.elevation.has_value());
    CHECK(fly.elevation->slope == 1);
    CHECK_FALSE(fly.elevation->base_height.has_value()); // -1 is the unset marker
    CHECK(fly.elevation->quantised_height == 4);
    CHECK(fly.elevation->scenery_ref == 400);
    const auto& q = w.paths[2];
    CHECK(q.kind == PathKind::Queue);
    REQUIRE(q.elevation.has_value());
    CHECK(q.elevation->base_height == 12);
    REQUIRE(q.queue.has_value());
    CHECK(q.queue->start_direction == 1);
    CHECK(q.queue->end_direction == 2);
    CHECK(q.queue->fence_colours[0] == 7);
    CHECK(q.queue->queue_line_ref == 500);
    CHECK(q.queue->fence_ref == 70);
    CHECK(q.queue->fence_symbol == "Tree_01");
    CHECK(q.queue->fence_overlay == "Scenery/Trees");

    REQUIRE(w.queue_lines.size() == 1);
    CHECK(w.queue_lines[0].head_ref == 210);
    CHECK(w.queue_lines[0].length == 5);
    CHECK(w.queue_lines[0].station_index == 2);

    REQUIRE(w.objects.size() == 2);
    const auto& o = w.objects[0];
    CHECK(o.id == 300);
    CHECK(o.symbol == "Tree_01");
    CHECK(o.overlay == "Scenery/Trees");
    CHECK(o.col == 1);
    CHECK(o.row == 1);
    CHECK(o.base_height == 6);
    CHECK(o.rotation == 1);
    CHECK(o.quadrant == 2);
    REQUIRE(o.height_adjust.has_value());
    CHECK(*o.height_adjust == 0.25f);
    CHECK(o.free_standing());
    CHECK(o.colours[0] == 3);
    CHECK(o.colours[2] == 2);
    CHECK(o.break_flags == 35);
    CHECK(o.behaviour_count == 2);
    CHECK_FALSE(o.hidden);
    CHECK_FALSE(w.objects[1].base_height.has_value()); // sentinel
    CHECK_FALSE(w.objects[1].free_standing());

    REQUIRE(w.weather.has_value());
    CHECK(w.weather->type == WeatherType::LightRain);
    CHECK(w.weather->forecast == WeatherType::Unknown); // 9 is outside the known range
    CHECK(w.weather->raw_forecast == 9);
    CHECK(w.weather->temperature == 21.5f);
    REQUIRE(w.weather->thunder_time.has_value());
    REQUIRE(w.weather->fog.has_value());
    CHECK(w.weather->fog->colour.r == 0.3f); // stored blue, green, red
    CHECK(w.weather->fog->colour.b == 0.1f);
    CHECK(w.weather->fog->far == 6000.0f);
    CHECK_FALSE(w.weather->cloud_colours.has_value());
    CHECK(w.sky_background == 5);
    REQUIRE(w.time.has_value());
    CHECK(w.time->day_fraction == 0.4f);
    CHECK(w.time->time_origin == 100.0f);
    CHECK_FALSE(w.time->day_night_mode.has_value());
    REQUIRE(w.camera.has_value());
    CHECK(w.camera->orientation.x == 0.1f);
    CHECK(w.camera->position.z == 30.0f);
}

TEST_CASE("load_world: surface style names resolve")
{
    Bytes surface;
    surface.text(u"Style_A");
    Bytes p;
    p.u8(0).u8(0).u8(0).u8(0).u64(60).u8(0);
    const auto l = load(schema(), { terrain_obj(1, make_terrain_3x2()), { kSurface, 60, surface }, { kPathPlain, 9, p } });
    REQUIRE(l->world);
    REQUIRE(l->world->paths.size() == 1);
    CHECK(l->world->paths[0].surface_ref == 60);
    CHECK(l->world->paths[0].surface_name == "Style_A");
    CHECK_FALSE(l->world->paths[0].surface_colours.has_value());
}

TEST_CASE("load_world: a queue's ground surface names its style through the queue type, and keeps its colours")
{
    Bytes style;
    style.text(u"Queue_A");
    Bytes ground;
    ground.i32(20).i32(1).i32(5).u64(61);
    Bytes p;
    p.u8(0).u8(0).u8(0).u8(0).u64(60).u8(0);
    const auto l = load(schema(), { terrain_obj(1, make_terrain_3x2()), { kSurface, 61, style }, { kGroundSurface, 60, ground }, { kPathPlain, 9, p } });
    REQUIRE(l->world);
    REQUIRE(l->world->paths.size() == 1);
    CHECK(l->world->paths[0].surface_name == "Queue_A");
    REQUIRE(l->world->paths[0].surface_colours.has_value());
    CHECK(*l->world->paths[0].surface_colours == std::array<std::int32_t, 3> { 20, 1, 5 });
}

TEST_CASE("load_world: older scenery layout without a height adjust")
{
    const auto l = load(schema(false), { terrain_obj(1, make_terrain_3x2()), entry_obj(70, u"Bench"), scenery_obj(300, 0, 0, 3, 70, 300, false) });
    REQUIRE(l->world);
    REQUIRE(l->world->objects.size() == 1);
    CHECK_FALSE(l->world->objects[0].height_adjust.has_value());
    CHECK(l->world->objects[0].base_height == 3);
    CHECK(l->world->objects[0].symbol == "Bench");
}

TEST_CASE("load_world: litter bins and signs keep the object they belong to")
{
    const auto l = load(
        schema(),
        {
            terrain_obj(1, make_terrain_3x2()),
            scenery_obj(300, 0, 0, 3, 0, 300),
            scenery_obj(301, 1, 0, 3, 0, 301),
            { kBin, 400, Bytes().u64(300).f32(2.0f).f32(0.5f).f32(-1.0f) },
            { kBin, 401, Bytes().u64(301).f32(2.0f).f32(0.0f).f32(1234.5f) },
            { kSign, 402, Bytes().u64(301).i32(0).text(u"Welcome") },
            { kBench, 403, Bytes().u64(300) },
        });
    REQUIRE(l->world);
    REQUIRE(l->world->bins.size() == 2);
    CHECK(l->world->bins[0].object_ref == 300);
    CHECK(l->world->bins[0].capacity == 2.0f);
    CHECK(l->world->bins[0].contents == 0.5f);
    CHECK_FALSE(l->world->bins[0].last_emptied); // Never emptied
    CHECK(l->world->bins[1].last_emptied == 1234.5f);
    REQUIRE(l->world->signs.size() == 1);
    CHECK(l->world->signs[0].object_ref == 301);
    CHECK(l->world->signs[0].text == "Welcome");
    CHECK(l->world->benches == std::vector<std::uint64_t> { 300 });
}

TEST_CASE("load_world: dangling references resolve to nothing, not errors")
{
    const auto l = load(schema(), { terrain_obj(1, make_terrain_3x2(), Bytes().u32(1).u64(999).f32(0).f32(0).f32(0)), scenery_obj(300, 0, 0, 3, 12345, 54321) });
    REQUIRE(l->world);
    CHECK(l->world->objects[0].symbol.empty());
    CHECK(l->world->objects[0].definition_ref == 12345);
    CHECK_FALSE(l->world->objects[0].free_standing());
    CHECK(l->world->skirt_trees[0].symbol.empty());
}

TEST_CASE("load_world: no terrain object is an issue, not an error")
{
    const auto l = load(schema(), { { kSky, 4, Bytes().i32(1) } });
    REQUIRE(l->world);
    CHECK(l->world->terrain.empty());
    CHECK(l->world->issue_count(WorldIssueKind::MissingTerrain) == 1);
    CHECK(l->world->sky_background == 1);
}

TEST_CASE("load_world: malformed blobs leave their part empty and are reported")
{
    auto bad = make_terrain_3x2();
    bad.v.resize(bad.v.size() - 5);
    const auto l = load(schema(), { terrain_obj(1, bad, Bytes().u32(50)), water_obj(2, Bytes().u8(1)) });
    REQUIRE(l->world);
    CHECK(l->world->terrain.empty());
    CHECK(l->world->water.bodies.empty());
    CHECK(l->world->skirt_trees.empty());
    CHECK(l->world->issue_count(WorldIssueKind::BadTerrainBlob) == 1);
    CHECK(l->world->issue_count(WorldIssueKind::BadWaterBlob) == 1);
    CHECK(l->world->issue_count(WorldIssueKind::BadSkirtBlob) == 1);
    CHECK(l->world->issue_count(WorldIssueKind::MissingTerrain) == 0);
}

TEST_CASE("load_world: consistency findings")
{
    const auto l = load(
        schema(),
        {
            terrain_obj(1, make_terrain_3x2()),
            // Layer written for a different map size, an out-of-range tile, a bad triangle and a wrong mask.
            water_obj(2, make_water(4, 2, { { 0.5f, { { 0, 0, 0, 0b001 }, { 7, 7, 0, 1 }, { 0, 0, 2, 1 }, { 1, 0, 0, 0b111 } } } })),
            path_obj(kPathFlying, 201, 2, 1, 1, 400),   // scenery on another tile
            path_obj(kPathFlying, 202, 0, 0, 1, 12345), // scenery link not found
            path_obj(kPathFlying, 203, 9, 9, 1),        // outside the map
            path_obj(kPathFlying, 204, 0, 0, 0),        // class says flying, kind byte says ground
            path_obj(kPathQueue, 205, 0, 0, 2, 777),    // queue links are not checked
            scenery_obj(400, 0, 0, 1, 70, 400),
            scenery_obj(401, 50, 0, 1, 70, 401), // outside the map
            { kSky, 10, Bytes().i32(1) },
            { kSky, 11, Bytes().i32(2) }, // second singleton
        });
    REQUIRE(l->world);
    const WorldModel& w = *l->world;
    CHECK(w.issue_count(WorldIssueKind::WaterSizeMismatch) == 1);
    CHECK(w.issue_count(WorldIssueKind::WaterTileOutOfRange) == 1);
    CHECK(w.issue_count(WorldIssueKind::WaterBadTriangle) == 1);
    CHECK(w.issue_count(WorldIssueKind::WaterMaskMismatch) == 1);
    CHECK(w.issue_count(WorldIssueKind::PathSceneryMismatch) == 1);
    CHECK(w.issue_count(WorldIssueKind::PathSceneryDangling) == 1);
    CHECK(w.issue_count(WorldIssueKind::PathTileOutOfRange) == 1);
    CHECK(w.issue_count(WorldIssueKind::PathKindMismatch) == 1);
    CHECK(w.issue_count(WorldIssueKind::ObjectOutOfRange) == 1);
    CHECK(w.issue_count(WorldIssueKind::DuplicateSingleton) == 1);
    CHECK(w.sky_background == 1); // the first one wins
    for (const auto& i : w.issues)
        CHECK(i.count >= 1);
}

TEST_CASE("load_world: non-finite heights are reported")
{
    Bytes b = terrain_header(1, 1, 0, 0, 1, 1);
    cell24(b, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0, 0, 0);
    const auto l = load(schema(), { terrain_obj(1, b) });
    REQUIRE(l->world);
    CHECK(l->world->issue_count(WorldIssueKind::NonFiniteHeight) == 1);
}

TEST_CASE("load_world: an attribute of the wrong type is ignored, not fatal")
{
    // Weather with a Type attribute declared as a string: reads as zero rather than failing.
    auto classes = schema();
    classes[kWeather] = { "Weather", { { "Type", "string", 0, { } } } };
    Bytes p;
    p.text(u"x");
    const auto l = load(classes, { terrain_obj(1, make_terrain_3x2()), { kWeather, 3, p } });
    REQUIRE(l->world);
    REQUIRE(l->world->weather.has_value());
    CHECK(l->world->weather->raw_type == 0);
    CHECK(l->world->weather->type == WeatherType::Sunny);
}

TEST_CASE("enum names")
{
    CHECK(std::string(to_string(WeatherType::Snowstorm)) == "snowstorm");
    CHECK(weather_type_from(-3) == WeatherType::Unknown);
    CHECK(weather_type_from(6) == WeatherType::Snowstorm);
    CHECK(std::string(to_string(PathKind::Queue)) == "queue");
    CHECK(std::string(to_string(Ownership::Locked)) == "locked");
}

TEST_CASE("load_world: the pool squares that hold water, at their surface and depth")
{
    const auto pool = [](std::uint64_t id, std::int32_t col, std::int32_t row, std::int32_t type, bool prototype) { return Obj { kPool, id, Bytes().i32(col).i32(2).u8(prototype ? 1 : 0).i32(row).i32(type).i32(-5) }; };
    const auto l = load(schema(), { terrain_obj(1, make_terrain_3x2()), pool(500, 1, 0, 0, false), pool(501, 2, 1, 0, false), pool(502, 0, 0, 1, false), pool(503, 0, 1, 0, true) });
    REQUIRE(l->world);
    REQUIRE(l->world->pool_water.size() == 2);
    CHECK(l->world->pool_water[0].col == 1);
    CHECK(l->world->pool_water[0].row == 0);
    CHECK(l->world->pool_water[0].surface == -5);
    CHECK(l->world->pool_water[0].depth == 2);
    CHECK(l->world->pool_water[1].col == 2);
}
