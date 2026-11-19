// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/manifold_mesh.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kMeshTag = "mam";
        constexpr usize kHeaderSize = 48; // two padded vectors, vertex count, triangle count, two pointers
        constexpr usize kVertexSize = 16;
        constexpr u32 kMaxElements = 1u << 24;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<ManifoldMesh> read_mesh(const AddressSpace& space, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            const u32 vertex_count = u32_at(*header, 32);
            const u32 triangle_count = u32_at(*header, 36);
            if (vertex_count > kMaxElements || triangle_count > kMaxElements)
                return std::nullopt;

            ManifoldMesh mesh;
            std::memcpy(mesh.min.data(), header->data(), 12);
            std::memcpy(mesh.max.data(), header->data() + 16, 12);

            if (vertex_count != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 40), usize { vertex_count } * kVertexSize);
                if (!raw)
                    return std::nullopt;
                mesh.vertices.resize(vertex_count);
                for (u32 i = 0; i < vertex_count; ++i)
                {
                    std::memcpy(mesh.vertices[i].data(), raw->data() + usize { i } * kVertexSize, 12);
                }
            }

            if (triangle_count != 0)
            {
                const usize count = usize { triangle_count } * 3;
                const auto raw = space.bytes_at(u32_at(*header, 44), count * 2);
                if (!raw)
                    return std::nullopt;
                mesh.indices.resize(count);
                std::memcpy(mesh.indices.data(), raw->data(), raw->size());
            }
            return mesh;
        }

    } // namespace

    std::vector<ManifoldMeshItem> read_manifold_meshes(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<ManifoldMeshItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kMeshTag)
                continue;
            items.push_back({ parts.name, read_mesh(space, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
