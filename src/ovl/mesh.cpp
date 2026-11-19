// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/mesh.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <set>

namespace rct3::ovl
{

    namespace
    {

        void append_part_header(Mesh& mesh, std::string_view texture, std::string_view style, u32 transparency, u32 flags, u32 sides)
        {
            MeshPart part;
            part.texture = std::string(texture);
            part.style = std::string(style);
            part.first_index = static_cast<u32>(mesh.indices.size());
            part.transparency = transparency;
            part.flags = flags;
            part.sides = sides;
            mesh.parts.push_back(std::move(part));
        }

        // Append the indices of one source part, rebased onto the shared vertex arrays. `get` returns the
        // index at a position. Triangles that reference a vertex the part does not have are skipped.
        template<typename Get> void append_triangles(Mesh& mesh, u32 base, u32 vertex_count, u32 index_count, Get get)
        {
            for (u32 t = 0; t + 2 < index_count; t += 3)
            {
                const u32 a = get(t), b = get(t + 1), c = get(t + 2);
                if (a >= vertex_count || b >= vertex_count || c >= vertex_count)
                    continue;
                mesh.indices.push_back(base + a);
                mesh.indices.push_back(base + b);
                mesh.indices.push_back(base + c);
            }
            auto& part = mesh.parts.back();
            part.index_count = static_cast<u32>(mesh.indices.size()) - part.first_index;
        }

        void push_vertex(Mesh& mesh, const std::array<f32, 3>& p, const std::array<f32, 3>& n, const std::array<f32, 2>& uv)
        {
            mesh.positions.push_back(p);
            mesh.normals.push_back(n);
            mesh.uvs.push_back(uv);
        }

        f32 finite(f32 v)
        {
            return std::isfinite(v) ? v : 0.0f;
        }

        void append_number(std::string& out, f32 v)
        {
            char buf[48];
            const int n = std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(finite(v))); // c-api
            out.append(buf, n > 0 ? static_cast<usize>(n) : 0);
        }

    } // namespace

    Mesh mesh_from_static_shape(const StaticShape& shape)
    {
        Mesh mesh;
        for (const auto& source : shape.meshes)
        {
            append_part_header(mesh, source.texture, source.style, source.transparency, source.texture_flags, source.sides);
            // A reader result never has fewer vertex bytes than the count claims, but a hand-built one might.
            const usize available = source.vertices.size() / kShapeVertexSize;
            const u32 vertex_count = static_cast<u32>(std::min<usize>(source.vertex_count, available));
            const u32 base = static_cast<u32>(mesh.positions.size());
            for (u32 i = 0; i < vertex_count; ++i)
            {
                const ShapeVertex v = source.vertex(i);
                push_vertex(mesh, v.position, v.normal, v.texcoord);
            }
            const u32 index_count = static_cast<u32>(std::min<usize>(source.index_count, source.indices.size() / 4));
            append_triangles(mesh, base, vertex_count, index_count, [&](u32 i) { return source.index(i); });
        }
        return mesh;
    }

    Mesh mesh_from_bone_shape(const BoneShape& shape)
    {
        Mesh mesh;
        for (const auto& source : shape.meshes)
        {
            append_part_header(mesh, source.texture, source.style, source.transparency, source.texture_flags, source.sides);
            const usize available = source.vertices.size() / kBoneVertexSize;
            const u32 vertex_count = static_cast<u32>(std::min<usize>(source.vertex_count, available));
            const u32 base = static_cast<u32>(mesh.positions.size());
            for (u32 i = 0; i < vertex_count; ++i)
            {
                const BoneVertex v = source.vertex(i);
                push_vertex(mesh, v.position, v.normal, v.texcoord);
            }
            const u32 index_count = static_cast<u32>(std::min<usize>(source.index_count, source.indices.size() / 2));
            append_triangles(mesh, base, vertex_count, index_count, [&](u32 i) -> u32 { return source.index(i); });
        }
        return mesh;
    }

    std::string material_name(const MeshPart& part)
    {
        if (part.texture.empty())
            return "untextured";
        std::string out = part.texture;
        for (char& c : out)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!ok)
                c = '_';
        }
        return out;
    }

    std::string write_obj(const Mesh& mesh, std::string_view material_library)
    {
        std::string out;
        out += "# rct3++ mesh export\n";
        if (!material_library.empty())
        {
            out += "mtllib ";
            out += material_library;
            out += '\n';
        }
        for (const auto& p : mesh.positions)
        {
            out += "v ";
            append_number(out, p[0]);
            out += ' ';
            append_number(out, p[1]);
            out += ' ';
            append_number(out, 0.0f - p[2]);
            out += '\n';
        }
        for (const auto& uv : mesh.uvs)
        {
            out += "vt ";
            append_number(out, uv[0]);
            out += ' ';
            append_number(out, 1.0f - finite(uv[1]));
            out += '\n';
        }
        for (const auto& n : mesh.normals)
        {
            out += "vn ";
            append_number(out, n[0]);
            out += ' ';
            append_number(out, n[1]);
            out += ' ';
            append_number(out, 0.0f - n[2]);
            out += '\n';
        }
        const usize vertices = mesh.positions.size();
        for (usize k = 0; k < mesh.parts.size(); ++k)
        {
            const auto& part = mesh.parts[k];
            out += std::format("g part{}\n", k);
            out += std::format("# transparency {} flags {} sides {}\n", part.transparency, part.flags, part.sides);
            out += std::format("usemtl {}\n", material_name(part));
            for (u32 i = 0; i + 2 < part.index_count; i += 3)
            {
                out += 'f';
                // Mirroring z turns the left-handed model space into OBJ's right-handed one, which reverses the
                // winding: write the corners in the order 0, 2, 1.
                for (const u32 c : { 0u, 2u, 1u })
                {
                    const usize at = usize { part.first_index } + i + c;
                    const usize v = at < mesh.indices.size() ? mesh.indices[at] : vertices;
                    if (v >= vertices)
                    { // a hand-built mesh with a bad index: point at the first vertex
                        out += " 1/1/1";
                        continue;
                    }
                    const std::string n = std::to_string(v + 1);
                    out += ' ' + n + '/' + n + '/' + n;
                }
                out += '\n';
            }
        }
        return out;
    }

    std::string write_mtl(const Mesh& mesh)
    {
        std::string out = "# rct3++ material library\n";
        std::set<std::string> done;
        for (const auto& part : mesh.parts)
        {
            const std::string name = material_name(part);
            if (!done.insert(name).second)
                continue;
            out += std::format("newmtl {}\n", name);
            out += "Ka 1 1 1\nKd 1 1 1\nKs 0 0 0\n";
            if (!part.texture.empty())
                out += std::format("map_Kd {}.png\n", name);
            out += '\n';
        }
        return out;
    }

} // namespace rct3::ovl
