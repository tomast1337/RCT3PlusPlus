// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/frame_mesh.hpp"

#include "builder.hpp"
#include "core/types.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_frame_meshes;

namespace
{

    const std::string kNames("M:mms\0Wave\0", 11);
    constexpr u32 kBase = 16; // names padded

    // Offsets inside the structure block: a mesh of two points, one normal, two corners and one animation
    // of two keys.
    constexpr u32 kCorners = 40;
    constexpr u32 kIndices = 64;
    constexpr u32 kAnimation = 72;
    constexpr u32 kTimes = 136;
    constexpr u32 kKeys = 144;
    constexpr u32 kKeySize = 12; // two point words, three normal bytes padded to four
    constexpr u32 kEnd = kKeys + 2 * kKeySize;

    void words(std::vector<std::byte>& out, std::initializer_list<u32> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void real(std::vector<std::byte>& out, f32 v)
    {
        append_u32(out, std::bit_cast<u32>(v));
    }

    std::vector<std::byte> names()
    {
        std::string text = kNames;
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        words(out, { 0, kBase });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    constexpr u32 point_word(u32 x, u32 y, u32 z)
    {
        return x << 21 | y << 11 | z;
    }

    std::vector<std::byte> structure(u32 tracks = 0, u32 keys = 2)
    {
        std::vector<std::byte> out;
        words(out, { 2, 3, 2, 1, tracks, 1, kBase + kCorners, kBase + kIndices, 0, kBase + kAnimation });
        // Corners: {point, normal} then u, v.
        words(out, { 0 });
        real(out, 0.125f);
        real(out, 0.875f);
        words(out, { 1 });
        real(out, 0.25f);
        real(out, 0.75f);
        for (u16 i : { 0, 1, 1, 0 })
            append_u16(out, i);
        for (f32 f : { 1.0f, 2.0f, 3.0f, 0.0f, 0.5f, 0.25f, 2.0f, 0.0f })
            real(out, f);
        words(out, { 6, keys, kBase + kTimes, kBase + kKeys, 0, 0, 0, 0 });
        real(out, 0.5f);
        real(out, 1.0f);
        words(out, { point_word(3, 2, 1), point_word(2047, 1023, 2047) });
        out.insert(out.end(), { std::byte { 255 }, std::byte { 0 }, std::byte { 255 }, std::byte { 0 } });
        words(out, { point_word(0, 0, 0), point_word(1, 1, 1) });
        out.insert(out.end(), { std::byte { 0 }, std::byte { 255 }, std::byte { 0 }, std::byte { 0 } });
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("frame meshes expose corners, triangles and their motions")
{
    const auto overlay = make(structure());
    const auto items = read_frame_meshes(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "M");
    REQUIRE(items[0].mesh.has_value());
    const auto& mesh = *items[0].mesh;
    CHECK(mesh.points == 2);
    CHECK(mesh.normals == 1);
    CHECK(mesh.tracks == 0);
    CHECK(mesh.key_size() == kKeySize);
    REQUIRE(mesh.corners.size() == 2);
    CHECK(mesh.corners[1].point == 1);
    CHECK(mesh.corners[1].normal == 0);
    CHECK(mesh.corners[1].uv[1] == 0.75f);
    CHECK(mesh.indices == std::vector<u16> { 0, 1, 1 });
    REQUIRE(mesh.animations.size() == 1);
    const auto& wave = mesh.animations[0];
    CHECK(wave.name == "Wave");
    CHECK(wave.times == std::vector<f32> { 0.5f, 1.0f });
    CHECK(wave.keys.size() == 2 * kKeySize);
}

TEST_CASE("a key's points are steps from the motion's offset, its normals bytes onto -1..1")
{
    const auto overlay = make(structure());
    const auto items = read_frame_meshes(overlay, overlay.common());
    const auto& mesh = *items[0].mesh;
    const auto key = decode_key(mesh, mesh.animations[0], 0);
    REQUIRE(key.points.size() == 2);
    CHECK(key.points[0] == std::array { 2.5f, 2.5f, 5.0f });
    CHECK(key.points[1] == std::array { 1.0f + 2047 * 0.5f, 2.0f + 1023 * 0.25f, 3.0f + 2047 * 2.0f });
    REQUIRE(key.normals.size() == 1);
    CHECK(key.normals[0] == std::array { 1.0f, -1.0f, 1.0f });
    const auto second = decode_key(mesh, mesh.animations[0], 1);
    CHECK(second.points[0] == std::array { 1.0f, 2.0f, 3.0f });
    CHECK(second.normals[0] == std::array { -1.0f, 1.0f, -1.0f });
    CHECK(decode_key(mesh, mesh.animations[0], 2).points.empty());
}

TEST_CASE("frame mesh edge cases")
{
    SUBCASE("sockets are read and marked")
    {
        const auto overlay = make(structure(1));
        CHECK(read_frame_meshes(overlay, overlay.common())[0].mesh->tracks == 1);
    }

    SUBCASE("keys that leave the block make the mesh unreadable")
    {
        const auto overlay = make(structure(0, 3));
        CHECK_FALSE(read_frame_meshes(overlay, overlay.common())[0].mesh.has_value());
    }

    SUBCASE("a key count that cannot fit is refused")
    {
        const auto overlay = make(structure(0, 0x7FFFFFFF));
        CHECK_FALSE(read_frame_meshes(overlay, overlay.common())[0].mesh.has_value());
    }
}
