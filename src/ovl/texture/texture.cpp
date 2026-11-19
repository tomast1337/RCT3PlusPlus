// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/texture/texture.hpp"

#include "core/types.hpp"
#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl::texture
{

    namespace
    {

        constexpr std::string_view kTextureTag = "tex";
        constexpr std::string_view kStyleTag = "txs";
        constexpr usize kHeaderSize = 0x38; // up to and including the node pointer
        constexpr usize kTableOffset = 0;
        constexpr usize kParametersOffset = 0x20;
        constexpr usize kStyleOffset = 0x2c;
        constexpr usize kCountsOffset = 0x30;
        constexpr usize kNodeOffset = 0x34;
        constexpr usize kNodeSize = 12;
        constexpr u16 kTableValue = 7;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        u16 u16_at(std::span<const std::byte> b, usize at)
        {
            u16 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<Texture> read_texture(const AddressSpace& space, const ReferenceNames& refs, u32 address)
        {
            const auto raw = space.bytes_at(address, kHeaderSize);
            if (!raw)
                return std::nullopt;

            Texture texture;
            for (usize i = 0; i < texture.table.size(); ++i)
            {
                texture.table[i] = u16_at(*raw, kTableOffset + i * 2);
                // The one other record seen starts with bytes of a different value and is laid out differently.
                if (texture.table[i] != kTableValue)
                    return std::nullopt;
            }
            for (usize i = 0; i < texture.parameters.size(); ++i)
            {
                texture.parameters[i] = u32_at(*raw, kParametersOffset + i * 4);
            }
            texture.style = refs.find(address + kStyleOffset, kStyleTag);
            texture.first = u16_at(*raw, kCountsOffset);
            texture.count = u16_at(*raw, kCountsOffset + 2);

            // A pointer to a cell that holds the address of the node, or 0.
            if (const u32 cell_address = u32_at(*raw, kNodeOffset); cell_address != 0)
            {
                const auto cell = space.bytes_at(cell_address, 4);
                if (!cell)
                    return std::nullopt;
                const auto node = space.bytes_at(u32_at(*cell, 0), kNodeSize);
                if (!node)
                    return std::nullopt;
                TextureNode out;
                out.address = u32_at(*cell, 0);
                out.first = u32_at(*node, 0);
                out.second = u32_at(*node, 4);
                std::memcpy(&out.scale, node->data() + 8, sizeof out.scale);
                texture.node = out;
            }
            return texture;
        }

    } // namespace

    std::vector<TextureItem> read_textures(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);

        std::vector<TextureItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTextureTag)
                continue;
            items.push_back({ parts.name, read_texture(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl::texture