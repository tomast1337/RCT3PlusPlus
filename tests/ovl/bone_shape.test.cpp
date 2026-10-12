// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/bone_shape.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_bone_shapes;

namespace
{

    // Name block, 32 bytes: "B:bsh" 0, "Tex:ftx" 6, "Sty:txs" 14, "root" 22, "arm" 27. The structure block
    // follows at address 32.
    constexpr std::uint32_t kBase = 32;
    constexpr std::uint32_t kNameShape = 0;
    constexpr std::uint32_t kNameTexture = 6;
    constexpr std::uint32_t kNameStyle = 14;
    constexpr std::uint32_t kNameRoot = 22;
    constexpr std::uint32_t kNameArm = 27;

    // Offsets inside the structure block.
    constexpr std::uint32_t kMeshArray = 0;  // 1 pointer
    constexpr std::uint32_t kMesh = 4;       // 40 bytes
    constexpr std::uint32_t kHeader = 44;    // 60 bytes
    constexpr std::uint32_t kBones = 104;    // 2 * 8
    constexpr std::uint32_t kLocals = 120;   // 2 * 64
    constexpr std::uint32_t kWorlds = 248;   // 2 * 64
    constexpr std::uint32_t kVertices = 376; // 3 * 44
    constexpr std::uint32_t kIndices = 508;  // 3 * 2
    constexpr std::uint32_t kEnd = 514;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void real(std::vector<std::byte>& out, float v)
    {
        append_u32(out, std::bit_cast<std::uint32_t>(v));
    }

    void matrix(std::vector<std::byte>& out, float x, float y, float z)
    {
        for (std::size_t r = 0; r < 4; ++r)
        {
            for (std::size_t c = 0; c < 4; ++c)
                real(out, r == c ? 1.0f : 0.0f);
        }
        // Translation lives in the last row.
        const auto at = out.size() - 16;
        std::vector<std::byte> row;
        real(row, x), real(row, y), real(row, z), real(row, 1.0f);
        std::copy(row.begin(), row.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
    }

    std::vector<std::byte> names()
    {
        std::string text("B:bsh\0Tex:ftx\0Sty:txs\0root\0arm\0", 31);
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        words(out, { kNameShape, kBase + kHeader });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> reference_rows(std::uint32_t second_name = kNameStyle)
    {
        std::vector<std::byte> out;
        words(out, { kBase + kMesh + 4, kNameTexture, 0, 0 });
        words(out, { kBase + kMesh + 8, second_name, 0, 0 });
        return out;
    }

    struct Options
    {
        std::uint32_t mesh_count = 1;
        std::uint32_t bone_count = 2;
        std::uint32_t arm_parent = 0;
        std::uint32_t indices = kBase + kIndices;
    };

    std::vector<std::byte> structure(const Options& o = { })
    {
        std::vector<std::byte> out;
        words(out, { kBase + kMesh }); // mesh array
        words(out, { 0xFFFFFFFF, 0, 0, 1, 12, 3, 3, 3, kBase + kVertices, o.indices });
        for (float f : { -1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f })
            real(out, f); // bounds
        words(out, { 3, 3, 1, o.mesh_count, kBase + kMeshArray, o.bone_count, kBase + kBones, kBase + kLocals, kBase + kWorlds });
        words(out, { kNameRoot, 0xFFFFFFFF, kNameArm, o.arm_parent });
        matrix(out, 0, 1, 0); // locals
        matrix(out, 1, 0, 0);
        matrix(out, 0, 1, 0); // worlds
        matrix(out, 1, 1, 0);
        for (std::uint8_t v = 0; v < 3; ++v)
        {
            for (std::size_t k = 0; k < 3; ++k)
                real(out, static_cast<float>(v + k)); // position
            for (float f : { 0.0f, 1.0f, 0.0f })
                real(out, f); // normal
            for (std::uint8_t b : { v, std::uint8_t { 1 }, std::uint8_t { 0 }, std::uint8_t { 0 } })
                out.push_back(std::byte { b });
            for (std::uint8_t w : { 200, 55, 0, 0 })
                out.push_back(std::byte { w });
            for (int k = 0; k < 4; ++k)
                out.push_back(std::byte { 255 }); // colour
            real(out, 0.25f * v);
            real(out, 0.5f);
        }
        append_u16(out, 2), append_u16(out, 1), append_u16(out, 0);
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block, const std::vector<std::byte>& refs = reference_rows())
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row(), { }, refs } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("bone shapes expose meshes, weights and bones")
{
    const auto overlay = make(structure());
    const auto items = read_bone_shapes(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "B");
    REQUIRE(items[0].shape.has_value());

    const auto& s = *items[0].shape;
    CHECK(s.bounds_min[2] == -3.0f);
    CHECK(s.bounds_max[0] == 1.0f);
    CHECK(s.vertex_total == 3);
    CHECK(s.index_total == 3);

    REQUIRE(s.meshes.size() == 1);
    const auto& m = s.meshes[0];
    CHECK(m.support_type == 0xFFFFFFFF);
    CHECK(m.texture == "Tex");
    CHECK(m.style == "Sty");
    CHECK(m.transparency == 1);
    CHECK(m.texture_flags == 12);
    CHECK(m.sides == 3);
    REQUIRE(m.vertices.size() == 132);
    REQUIRE(m.indices.size() == 6);
    CHECK(m.index(0) == 2);
    CHECK(m.index(2) == 0);

    const auto v = m.vertex(2);
    CHECK(v.position[0] == 2.0f);
    CHECK(v.position[2] == 4.0f);
    CHECK(v.normal[1] == 1.0f);
    CHECK(v.bones[0] == 2);
    CHECK(v.bones[1] == 1);
    CHECK(v.weights[0] == 200);
    CHECK(v.weights[1] == 55);
    CHECK(v.colour[3] == 255);
    CHECK(v.texcoord[0] == 0.5f);
    CHECK(v.texcoord[1] == 0.5f);

    REQUIRE(s.bones.size() == 2);
    CHECK(s.bones[0].name == "root");
    CHECK(s.bones[0].parent == -1);
    CHECK(s.bones[0].local[13] == 1.0f);
    CHECK(s.bones[0].world[13] == 1.0f);
    CHECK(s.bones[1].name == "arm");
    CHECK(s.bones[1].parent == 0);
    CHECK(s.bones[1].local[12] == 1.0f);
    CHECK(s.bones[1].world[12] == 1.0f);
    CHECK(s.bones[1].world[13] == 1.0f);
    CHECK(s.bones[1].world[15] == 1.0f);
}

TEST_CASE("bone shape edge cases")
{
    SUBCASE("an index array that leaves its block makes the shape unreadable")
    {
        Options o;
        o.indices = kBase + kEnd - 4;
        const auto overlay = make(structure(o));
        const auto items = read_bone_shapes(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].shape.has_value());
    }

    SUBCASE("a parent index past the last bone makes the shape unreadable")
    {
        Options o;
        o.arm_parent = 2;
        const auto overlay = make(structure(o));
        const auto items = read_bone_shapes(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].shape.has_value());
    }

    SUBCASE("a parent index below -1 makes the shape unreadable")
    {
        Options o;
        o.arm_parent = 0xFFFFFFFE;
        const auto overlay = make(structure(o));
        const auto items = read_bone_shapes(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].shape.has_value());
    }

    SUBCASE("counts that cannot fit are refused")
    {
        Options o;
        o.bone_count = 0x7FFFFFFF;
        auto overlay = make(structure(o));
        auto items = read_bone_shapes(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].shape.has_value());

        o = { };
        o.mesh_count = 0x7FFFFFFF;
        overlay = make(structure(o));
        items = read_bone_shapes(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].shape.has_value());
    }

    SUBCASE("a reference with the wrong tag is not used")
    {
        const auto overlay = make(structure(), reference_rows(kNameRoot));
        const auto items = read_bone_shapes(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].shape.has_value());
        CHECK(items[0].shape->meshes[0].texture == "Tex");
        CHECK(items[0].shape->meshes[0].style.empty());
    }

    SUBCASE("a file without references still reads, with empty names")
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { structure() }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_bone_shapes(*overlay, overlay->common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].shape.has_value());
        CHECK(items[0].shape->meshes[0].texture.empty());
        CHECK(items[0].shape->bones[1].name == "arm");
    }

    SUBCASE("a header shorter than 60 bytes is unreadable")
    {
        std::vector<std::byte> row;
        words(row, { kNameShape, kBase + kEnd - 8 });
        append_u16(row, 1);
        append_u16(row, 0);
        append_u32(row, 0);
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { structure() }, { row } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_bone_shapes(*overlay, overlay->common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].shape.has_value());
    }
}
