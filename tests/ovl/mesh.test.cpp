// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/mesh.hpp"

#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace rct3::ovl;

namespace
{

    void put(std::vector<std::byte>& out, const void* p, std::size_t n)
    {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    }
    void put_f(std::vector<std::byte>& out, float v)
    {
        put(out, &v, 4);
    }
    void put_u32(std::vector<std::byte>& out, std::uint32_t v)
    {
        put(out, &v, 4);
    }
    void put_u16(std::vector<std::byte>& out, std::uint16_t v)
    {
        put(out, &v, 2);
    }

    struct ShapeData
    {
        std::vector<std::byte> vertices;
        std::vector<std::byte> indices;
    };

    // Three vertices of a static shape vertex: position, normal, colour, u, v.
    void shape_vertex(std::vector<std::byte>& out, std::array<float, 3> p, std::array<float, 3> n, float u, float v)
    {
        for (float f : p)
            put_f(out, f);
        for (float f : n)
            put_f(out, f);
        put_u32(out, 0xFFFFFFFFu);
        put_f(out, u);
        put_f(out, v);
    }

    // A unit cube centred on the origin, outward normals per face, wound clockwise seen from outside in a
    // left-handed space (the stored convention): the geometric normal (b-a)x(c-a) agrees with the stored one.
    ShapeData cube()
    {
        ShapeData d;
        struct Face
        {
            std::array<float, 3> n, a, b, c, e;
        };
        // Each face: normal and four corners listed so that (b-a)x(c-a) points along the normal.
        const Face faces[] = {
            { { 0, 0, 1 }, { -.5f, -.5f, .5f }, { .5f, -.5f, .5f }, { .5f, .5f, .5f }, { -.5f, .5f, .5f } },      { { 0, 0, -1 }, { .5f, -.5f, -.5f }, { -.5f, -.5f, -.5f }, { -.5f, .5f, -.5f }, { .5f, .5f, -.5f } }, { { 1, 0, 0 }, { .5f, -.5f, .5f }, { .5f, -.5f, -.5f }, { .5f, .5f, -.5f }, { .5f, .5f, .5f } },
            { { -1, 0, 0 }, { -.5f, -.5f, -.5f }, { -.5f, -.5f, .5f }, { -.5f, .5f, .5f }, { -.5f, .5f, -.5f } }, { { 0, 1, 0 }, { -.5f, .5f, .5f }, { .5f, .5f, .5f }, { .5f, .5f, -.5f }, { -.5f, .5f, -.5f } },      { { 0, -1, 0 }, { -.5f, -.5f, -.5f }, { .5f, -.5f, -.5f }, { .5f, -.5f, .5f }, { -.5f, -.5f, .5f } },
        };
        std::uint32_t base = 0;
        for (const auto& f : faces)
        {
            shape_vertex(d.vertices, f.a, f.n, 0.0f, 0.0f);
            shape_vertex(d.vertices, f.b, f.n, 1.0f, 0.0f);
            shape_vertex(d.vertices, f.c, f.n, 1.0f, 0.25f);
            shape_vertex(d.vertices, f.e, f.n, 0.0f, 0.25f);
            for (std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
                put_u32(d.indices, base + i);
            base += 4;
        }
        return d;
    }

    ShapeMesh make_part(const ShapeData& d, std::string_view texture, std::uint32_t vertices, std::uint32_t indices)
    {
        ShapeMesh m;
        m.texture = texture;
        m.style = "Style";
        m.transparency = 1;
        m.texture_flags = 4;
        m.sides = 3;
        m.vertex_count = vertices;
        m.index_count = indices;
        m.vertices = d.vertices;
        m.indices = d.indices;
        return m;
    }

    std::vector<std::string> lines_with(const std::string& text, std::string_view prefix)
    {
        std::vector<std::string> out;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);)
        {
            if (line.rfind(prefix, 0) == 0)
                out.push_back(line);
        }
        return out;
    }

} // namespace

TEST_CASE("a static shape becomes one mesh with a part per source mesh")
{
    const ShapeData cube_data = cube();
    const ShapeData tri = [] {
        ShapeData d;
        shape_vertex(d.vertices, { 0, 0, 0 }, { 0, 0, 1 }, 0, 0);
        shape_vertex(d.vertices, { 1, 0, 0 }, { 0, 0, 1 }, 1, 0);
        shape_vertex(d.vertices, { 0, 1, 0 }, { 0, 0, 1 }, 0, 1);
        for (std::uint32_t i : { 0u, 1u, 2u })
            put_u32(d.indices, i);
        return d;
    }();

    StaticShape shape;
    shape.meshes.push_back(make_part(cube_data, "Brick", 24, 36));
    shape.meshes.push_back(make_part(tri, "", 3, 3));

    const Mesh mesh = mesh_from_static_shape(shape);
    REQUIRE(mesh.positions.size() == 27);
    CHECK(mesh.normals.size() == 27);
    CHECK(mesh.uvs.size() == 27);
    REQUIRE(mesh.indices.size() == 39);
    REQUIRE(mesh.parts.size() == 2);
    CHECK(mesh.parts[0].texture == "Brick");
    CHECK(mesh.parts[0].style == "Style");
    CHECK(mesh.parts[0].first_index == 0);
    CHECK(mesh.parts[0].index_count == 36);
    CHECK(mesh.parts[0].transparency == 1);
    CHECK(mesh.parts[0].flags == 4);
    CHECK(mesh.parts[0].sides == 3);
    CHECK(mesh.parts[1].texture.empty());
    CHECK(mesh.parts[1].first_index == 36);
    CHECK(mesh.parts[1].index_count == 3);
    // The second part's indices are rebased onto the shared vertex arrays.
    CHECK(mesh.indices[36] == 24);
    CHECK(mesh.indices[38] == 26);
    CHECK(mesh.positions[25][0] == 1.0f);
    CHECK(mesh.uvs[26][1] == 1.0f);
}

TEST_CASE("cube geometry is consistent")
{
    StaticShape shape;
    const ShapeData d = cube();
    shape.meshes.push_back(make_part(d, "T", 24, 36));
    const Mesh mesh = mesh_from_static_shape(shape);
    REQUIRE(mesh.indices.size() == 36);
    for (std::size_t t = 0; t < 12; ++t)
    {
        const auto& a = mesh.positions[mesh.indices[t * 3]];
        const auto& b = mesh.positions[mesh.indices[t * 3 + 1]];
        const auto& c = mesh.positions[mesh.indices[t * 3 + 2]];
        const auto& n = mesh.normals[mesh.indices[t * 3]];
        const std::array<float, 3> u { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const std::array<float, 3> v { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        const std::array<float, 3> cross { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        CHECK(cross[0] * n[0] + cross[1] * n[1] + cross[2] * n[2] > 0.0f);
        CHECK(std::abs(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] - 1.0f) < 1e-6f);
    }
}

TEST_CASE("malformed parts are made well formed")
{
    ShapeData d;
    shape_vertex(d.vertices, { 0, 0, 0 }, { 0, 1, 0 }, 0, 0);
    shape_vertex(d.vertices, { 1, 0, 0 }, { 0, 1, 0 }, 0, 0);
    shape_vertex(d.vertices, { 0, 0, 1 }, { 0, 1, 0 }, 0, 0);
    // One good triangle, one with an index past the vertices, and two stray indices.
    for (std::uint32_t i : { 0u, 1u, 2u, 0u, 1u, 9u, 2u, 1u })
        put_u32(d.indices, i);

    StaticShape shape;
    shape.meshes.push_back(make_part(d, "T", 3, 8));
    const Mesh mesh = mesh_from_static_shape(shape);
    CHECK(mesh.indices.size() == 3);
    CHECK(mesh.parts[0].index_count == 3);

    // A vertex count larger than the bytes available is clamped, and indices then fall out.
    shape.meshes[0].vertex_count = 100;
    const Mesh clamped = mesh_from_static_shape(shape);
    CHECK(clamped.positions.size() == 3);
    for (auto i : clamped.indices)
        CHECK(i < clamped.positions.size());

    CHECK(mesh_from_static_shape(StaticShape { }).parts.empty());
}

TEST_CASE("bone shapes export their stored rest pose")
{
    std::vector<std::byte> vertices;
    const float pos[3][3] = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 3, 0 } };
    for (const auto& p : pos)
    {
        for (float f : p)
            put_f(vertices, f);
        for (float f : { 0.0f, 0.0f, 1.0f })
            put_f(vertices, f); // normal
        for (std::uint8_t b : { 0, 0, 0, 0 })
            vertices.push_back(static_cast<std::byte>(b)); // bones
        for (std::uint8_t b : { 255, 0, 0, 0 })
            vertices.push_back(static_cast<std::byte>(b)); // weights
        for (std::uint8_t b : { 255, 255, 255, 255 })
            vertices.push_back(static_cast<std::byte>(b)); // colour
        put_f(vertices, p[0] / 4.0f);
        put_f(vertices, 0.5f);
    }
    std::vector<std::byte> indices;
    for (std::uint16_t i : { 0, 1, 2 })
        put_u16(indices, i);

    BoneShape shape;
    BoneMesh m;
    m.texture = "Skin";
    m.vertex_count = 3;
    m.index_count = 3;
    m.vertices = vertices;
    m.indices = indices;
    m.sides = 1;
    shape.meshes.push_back(m);

    const Mesh mesh = mesh_from_bone_shape(shape);
    REQUIRE(mesh.positions.size() == 3);
    CHECK(mesh.positions[2][1] == 3.0f);
    CHECK(mesh.uvs[1][0] == 0.5f);
    CHECK(mesh.indices == std::vector<std::uint32_t> { 0, 1, 2 });
    CHECK(mesh.parts[0].sides == 1);
}

TEST_CASE("obj output converts handedness and texture origin")
{
    StaticShape shape;
    ShapeData d;
    shape_vertex(d.vertices, { 1, 2, 3 }, { 0, 0, 1 }, 0.25f, 0.25f);
    shape_vertex(d.vertices, { 4, 5, 6 }, { 0, 0, 1 }, 0.5f, 0.25f);
    shape_vertex(d.vertices, { 7, 8, 9 }, { 0, 0, 1 }, 0.5f, 1.0f);
    for (std::uint32_t i : { 0u, 1u, 2u })
        put_u32(d.indices, i);
    shape.meshes.push_back(make_part(d, "Wall paper", 3, 3));
    const Mesh mesh = mesh_from_static_shape(shape);

    const std::string obj = write_obj(mesh, "shape.mtl");
    CHECK(obj.find("mtllib shape.mtl\n") != std::string::npos);
    const auto v = lines_with(obj, "v ");
    REQUIRE(v.size() == 3);
    CHECK(v[0] == "v 1 2 -3");
    CHECK(v[2] == "v 7 8 -9");
    const auto vt = lines_with(obj, "vt ");
    REQUIRE(vt.size() == 3);
    CHECK(vt[0] == "vt 0.25 0.75");
    CHECK(vt[2] == "vt 0.5 0");
    CHECK(lines_with(obj, "vn ")[0] == "vn 0 0 -1");
    CHECK(lines_with(obj, "usemtl ")[0] == "usemtl Wall_paper");
    // Corners 2 and 3 are swapped.
    CHECK(lines_with(obj, "f ")[0] == "f 1/1/1 3/3/3 2/2/2");

    const std::string mtl = write_mtl(mesh);
    CHECK(mtl.find("newmtl Wall_paper\n") != std::string::npos);
    CHECK(mtl.find("map_Kd Wall_paper.png\n") != std::string::npos);
}

TEST_CASE("obj and mtl edge cases")
{
    Mesh mesh;
    mesh.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { std::numeric_limits<float>::quiet_NaN(), 0, 0 } };
    mesh.normals.assign(3, { 0, 1, 0 });
    mesh.uvs.assign(3, { 0, 0 });
    mesh.indices = { 0, 1, 2, 0, 1, 77 }; // second triangle has a bad index
    mesh.parts.push_back({ "", "", 0, 6, 0, 0, 3 });

    const std::string obj = write_obj(mesh, "");
    CHECK(obj.find("mtllib") == std::string::npos);
    CHECK(lines_with(obj, "v ")[2] == "v 0 0 0"); // NaN written as zero
    const auto f = lines_with(obj, "f ");
    REQUIRE(f.size() == 2);
    CHECK(f[1].find("1/1/1") != std::string::npos); // a bad index points at the first vertex
    CHECK(lines_with(obj, "usemtl ")[0] == "usemtl untextured");

    const std::string mtl = write_mtl(mesh);
    CHECK(mtl.find("newmtl untextured") != std::string::npos);
    CHECK(mtl.find("map_Kd") == std::string::npos);

    CHECK(write_mtl(Mesh { }).find("newmtl") == std::string::npos);
    CHECK(lines_with(write_obj(Mesh { }, ""), "f ").empty());
}

TEST_CASE("material names are made safe")
{
    MeshPart part;
    part.texture = "a b/c.d-e_9";
    CHECK(material_name(part) == "a_b_c.d-e_9");
    part.texture.clear();
    CHECK(material_name(part) == "untextured");
}
