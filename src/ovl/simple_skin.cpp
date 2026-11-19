// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/simple_skin.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSkinTag = "ssk";
        constexpr usize kHeaderSize = 28; // part count, ids, vertex counts, triangle count, indices, vertex count, vertices
        constexpr usize kVertexSize = 32;
        constexpr u32 kMaxParts = 4096;
        constexpr u32 kMaxElements = 1u << 24;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        bool read_list(const AddressSpace& space, u32 address, u32 count, std::vector<u32>& out)
        {
            const auto raw = space.bytes_at(address, usize { count } * 4);
            if (!raw)
                return false;
            out.resize(count);
            if (count != 0)
                std::memcpy(out.data(), raw->data(), raw->size());
            return true;
        }

        std::optional<SimpleSkin> read_skin(const AddressSpace& space, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            const u32 parts = u32_at(*header, 0);
            const u32 triangles = u32_at(*header, 12);
            const u32 vertex_count = u32_at(*header, 20);
            if (parts > kMaxParts || triangles > kMaxElements || vertex_count > kMaxElements)
                return std::nullopt;

            SimpleSkin skin;
            if (!read_list(space, u32_at(*header, 4), parts, skin.part_ids))
                return std::nullopt;
            if (!read_list(space, u32_at(*header, 8), parts, skin.part_vertex_counts))
                return std::nullopt;

            if (triangles != 0)
            {
                const usize count = usize { triangles } * 3;
                const auto raw = space.bytes_at(u32_at(*header, 16), count * 2);
                if (!raw)
                    return std::nullopt;
                skin.indices.resize(count);
                std::memcpy(skin.indices.data(), raw->data(), raw->size());
            }

            if (vertex_count != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 24), usize { vertex_count } * kVertexSize);
                if (!raw)
                    return std::nullopt;
                skin.vertices.resize(vertex_count);
                for (u32 i = 0; i < vertex_count; ++i)
                {
                    const std::byte* p = raw->data() + usize { i } * kVertexSize;
                    SimpleSkinVertex& v = skin.vertices[i];
                    std::memcpy(v.position.data(), p, 12);
                    std::memcpy(v.normal.data(), p + 12, 12);
                    std::memcpy(v.uv.data(), p + 24, 8);
                }
            }
            return skin;
        }

    } // namespace

    std::vector<SimpleSkinItem> read_simple_skins(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<SimpleSkinItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kSkinTag)
                continue;
            items.push_back({ parts.name, read_skin(space, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
