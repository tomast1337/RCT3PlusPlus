// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/static_shape.hpp"

#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kShapeTag = "shs";
        constexpr std::string_view kFlexiTag = "ftx";
        constexpr std::string_view kStyleTag = "txs";
        constexpr std::size_t kHeaderSize = 56; // fourteen 32-bit words
        constexpr std::size_t kMeshSize = 40;   // ten 32-bit words
        constexpr std::size_t kMatrixSize = 64; // sixteen floats
        constexpr std::uint32_t kMaxMeshes = 1u << 16;

        std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        float f32_at(std::span<const std::byte> b, std::size_t at)
        {
            float v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<ShapeMesh> read_mesh(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address)
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

            const auto vertices = space.bytes_at(u32_at(*raw, 32), std::size_t { mesh.vertex_count } * kShapeVertexSize);
            if (!vertices)
                return std::nullopt;
            mesh.vertices = *vertices;

            const auto indices = space.bytes_at(u32_at(*raw, 36), std::size_t { mesh.index_count } * 4);
            if (!indices)
                return std::nullopt;
            mesh.indices = *indices;
            return mesh;
        }

        std::optional<StaticShape> read_shape(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            StaticShape shape;
            for (std::size_t i = 0; i < 3; ++i)
            {
                shape.bounds_min[i] = f32_at(*header, i * 4);
                shape.bounds_max[i] = f32_at(*header, 12 + i * 4);
            }
            shape.vertex_total = u32_at(*header, 24);
            shape.index_total = u32_at(*header, 28);
            shape.secondary_count = u32_at(*header, 32);
            const std::uint32_t mesh_count = u32_at(*header, 36);
            const std::uint32_t mesh_array = u32_at(*header, 40);
            const std::uint32_t effect_count = u32_at(*header, 44);
            const std::uint32_t matrices = u32_at(*header, 48);
            const std::uint32_t names = u32_at(*header, 52);

            if (mesh_count > kMaxMeshes)
                return std::nullopt;
            if (mesh_count != 0)
            {
                const auto pointers = space.bytes_at(mesh_array, std::size_t { mesh_count } * 4);
                if (!pointers)
                    return std::nullopt;
                shape.meshes.reserve(mesh_count);
                for (std::uint32_t i = 0; i < mesh_count; ++i)
                {
                    auto mesh = read_mesh(space, refs, u32_at(*pointers, std::size_t { i } * 4));
                    if (!mesh)
                        return std::nullopt;
                    shape.meshes.push_back(*mesh);
                }
            }

            if (effect_count != 0)
            {
                const auto transforms = space.bytes_at(matrices, std::size_t { effect_count } * kMatrixSize);
                const auto pointers = space.bytes_at(names, std::size_t { effect_count } * 4);
                if (!transforms || !pointers)
                    return std::nullopt;
                shape.effects.reserve(effect_count);
                for (std::uint32_t i = 0; i < effect_count; ++i)
                {
                    ShapeEffect effect;
                    if (const auto name = space.cstring_at(u32_at(*pointers, std::size_t { i } * 4)))
                        effect.name = *name;
                    for (std::size_t k = 0; k < 16; ++k)
                        effect.transform[k] = f32_at(*transforms, std::size_t { i } * kMatrixSize + k * 4);
                    shape.effects.push_back(effect);
                }
            }
            return shape;
        }

    } // namespace

    ShapeVertex ShapeMesh::vertex(std::size_t i) const
    {
        ShapeVertex v;
        const auto raw = vertices.subspan(i * kShapeVertexSize, kShapeVertexSize);
        for (std::size_t k = 0; k < 3; ++k)
        {
            v.position[k] = f32_at(raw, k * 4);
            v.normal[k] = f32_at(raw, 12 + k * 4);
        }
        for (std::size_t k = 0; k < 4; ++k)
            v.colour[k] = static_cast<std::uint8_t>(raw[24 + k]);
        v.texcoord[0] = f32_at(raw, 28);
        v.texcoord[1] = f32_at(raw, 32);
        return v;
    }

    std::uint32_t ShapeMesh::index(std::size_t i) const
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
