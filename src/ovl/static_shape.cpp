// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/static_shape.hpp"

#include "core/types.hpp"
#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kShapeTag = "shs";
        constexpr std::string_view kFlexiTag = "ftx";
        constexpr std::string_view kStyleTag = "txs";
        constexpr usize kHeaderSize = 56; // fourteen 32-bit words
        constexpr usize kMeshSize = 40;   // ten 32-bit words
        constexpr usize kMatrixSize = 64; // sixteen floats
        constexpr u32 kMaxMeshes = 1u << 16;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        f32 f32_at(std::span<const std::byte> b, usize at)
        {
            f32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<ShapeMesh> read_mesh(const AddressSpace& space, const ReferenceNames& refs, u32 address)
        {
            const auto raw = space.bytes_at(address, kMeshSize);
            if (!raw)
                return std::nullopt;

            ShapeMesh mesh;
            mesh.support_type = u32_at(*raw, 0);
            mesh.texture = refs.find(address + 4, kFlexiTag);
            mesh.style = refs.find(address + 8, kStyleTag);
            mesh.transparency = u32_at(*raw, 12);
            mesh.texture_flags = u32_at(*raw, 16);
            mesh.sides = u32_at(*raw, 20);
            mesh.vertex_count = u32_at(*raw, 24);
            mesh.index_count = u32_at(*raw, 28);

            const auto vertices = space.bytes_at(u32_at(*raw, 32), usize { mesh.vertex_count } * kShapeVertexSize);
            if (!vertices)
                return std::nullopt;
            mesh.vertices = *vertices;

            const auto indices = space.bytes_at(u32_at(*raw, 36), usize { mesh.index_count } * 4);
            if (!indices)
                return std::nullopt;
            mesh.indices = *indices;
            return mesh;
        }

        std::optional<StaticShape> read_shape(const AddressSpace& space, const ReferenceNames& refs, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            StaticShape shape;
            for (usize i = 0; i < 3; ++i)
            {
                shape.bounds_min[i] = f32_at(*header, i * 4);
                shape.bounds_max[i] = f32_at(*header, 12 + i * 4);
            }
            shape.vertex_total = u32_at(*header, 24);
            shape.index_total = u32_at(*header, 28);
            shape.secondary_count = u32_at(*header, 32);
            const u32 mesh_count = u32_at(*header, 36);
            const u32 mesh_array = u32_at(*header, 40);
            const u32 effect_count = u32_at(*header, 44);
            const u32 matrices = u32_at(*header, 48);
            const u32 names = u32_at(*header, 52);

            if (mesh_count > kMaxMeshes)
                return std::nullopt;
            if (mesh_count != 0)
            {
                const auto pointers = space.bytes_at(mesh_array, usize { mesh_count } * 4);
                if (!pointers)
                    return std::nullopt;
                shape.meshes.reserve(mesh_count);
                for (u32 i = 0; i < mesh_count; ++i)
                {
                    auto mesh = read_mesh(space, refs, u32_at(*pointers, usize { i } * 4));
                    if (!mesh)
                        return std::nullopt;
                    shape.meshes.push_back(*mesh);
                }
            }

            if (effect_count != 0)
            {
                const auto transforms = space.bytes_at(matrices, usize { effect_count } * kMatrixSize);
                const auto pointers = space.bytes_at(names, usize { effect_count } * 4);
                if (!transforms || !pointers)
                    return std::nullopt;
                shape.effects.reserve(effect_count);
                for (u32 i = 0; i < effect_count; ++i)
                {
                    ShapeEffect effect;
                    if (const auto name = space.cstring_at(u32_at(*pointers, usize { i } * 4)))
                        effect.name = *name;
                    for (usize k = 0; k < 16; ++k)
                        effect.transform[k] = f32_at(*transforms, usize { i } * kMatrixSize + k * 4);
                    shape.effects.push_back(effect);
                }
            }
            return shape;
        }

    } // namespace

    ShapeVertex ShapeMesh::vertex(usize i) const
    {
        ShapeVertex v;
        const auto raw = vertices.subspan(i * kShapeVertexSize, kShapeVertexSize);
        for (usize k = 0; k < 3; ++k)
        {
            v.position[k] = f32_at(raw, k * 4);
            v.normal[k] = f32_at(raw, 12 + k * 4);
        }
        for (usize k = 0; k < 4; ++k)
            v.colour[k] = static_cast<u8>(raw[24 + k]);
        v.texcoord[0] = f32_at(raw, 28);
        v.texcoord[1] = f32_at(raw, 32);
        return v;
    }

    u32 ShapeMesh::index(usize i) const
    {
        return u32_at(indices, i * 4);
    }

    std::vector<StaticShapeItem> read_static_shapes(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();

        const ReferenceNames refs(part, space);

        std::vector<StaticShapeItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kShapeTag)
                continue;
            items.push_back({ parts.name, read_shape(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
