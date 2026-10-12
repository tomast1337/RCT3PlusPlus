// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/manifold_mesh.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_manifold_meshes;

namespace
{

    // Name block: "A:mam" at 0, padded to 16. The structure block follows at address 16.
    constexpr std::uint32_t kBase = 16;
    constexpr std::uint32_t kVertices = 48; // 3 vertices * 16
    constexpr std::uint32_t kIndices = 96;  // 1 triangle * 6 bytes

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void real(std::vector<std::byte>& out, float v)
    {
        append_u32(out, std::bit_cast<std::uint32_t>(v));
    }

    std::vector<std::byte> names()
    {
        std::string text("A:mam\0", 6);
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

    std::vector<std::byte> structure(std::uint32_t vertices = 3, std::uint32_t triangles = 1)
    {
        std::vector<std::byte> out;
        for (float v : { -1.0f, -2.0f, -3.0f, 0.0f, 4.0f, 5.0f, 6.0f, 0.0f })
            real(out, v);
        words(out, { vertices, triangles, kBase + kVertices, kBase + kIndices });
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            for (float v : { float(i), float(i) + 0.5f, -float(i), 0.0f })
                real(out, v);
        }
        for (std::uint16_t i : { 0, 1, 2 })
            append_u16(out, i);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("manifold meshes expose bounds, vertices and indices")
{
    const auto overlay = make(structure());
    const auto items = read_manifold_meshes(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "A");
    REQUIRE(items[0].mesh.has_value());

    const auto& m = *items[0].mesh;
    CHECK(m.min[1] == -2.0f);
    CHECK(m.max[2] == 6.0f);
    REQUIRE(m.vertices.size() == 3);
    CHECK(m.vertices[2][0] == 2.0f);
    CHECK(m.vertices[2][1] == 2.5f);
    CHECK(m.vertices[2][2] == -2.0f);
    REQUIRE(m.indices.size() == 3);
    CHECK(m.indices[2] == 2);
}

TEST_CASE("manifold mesh edge cases")
{
    SUBCASE("a vertex count that cannot fit is refused")
    {
        const auto overlay = make(structure(0x7FFFFFFF));
        CHECK_FALSE(read_manifold_meshes(overlay, overlay.common())[0].mesh.has_value());
    }

    SUBCASE("a triangle list that leaves the block makes the mesh unreadable")
    {
        const auto overlay = make(structure(3, 2));
        CHECK_FALSE(read_manifold_meshes(overlay, overlay.common())[0].mesh.has_value());
    }

    SUBCASE("a mesh without geometry reads")
    {
        const auto overlay = make(structure(0, 0));
        const auto items = read_manifold_meshes(overlay, overlay.common());
        REQUIRE(items[0].mesh.has_value());
        CHECK(items[0].mesh->vertices.empty());
        CHECK(items[0].mesh->indices.empty());
    }
}
