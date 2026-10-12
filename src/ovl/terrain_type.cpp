// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/terrain_type.hpp"

#include "ovl/field_values.hpp"
#include "ovl/reference_names.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTerrainTag = "ter";
        constexpr std::string_view kTextureTag = "tex";
        constexpr std::size_t kSize = 60;

        std::optional<TerrainType> read_terrain(const AddressSpace& space, const ReferenceNames& refs, const PointerFields& pointers, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kSize);
            if (!raw)
                return std::nullopt;
            const auto b = *raw;

            TerrainType t;
            t.unknown_00 = fields::u32(b, 0x00);
            t.unknown_04 = fields::u32(b, 0x04);
            t.unknown_08 = fields::u32(b, 0x08);
            t.index = fields::u32(b, 0x0c);
            t.unknown_10 = fields::u32(b, 0x10);
            t.texture = refs.find(address + 0x14, kTextureTag);
            t.help_name = fields::string_field(space, pointers, address + 0x18, fields::u32(b, 0x18));
            t.label_name = fields::string_field(space, pointers, address + 0x1c, fields::u32(b, 0x1c));
            t.colours = { fields::u32(b, 0x20), fields::u32(b, 0x24) };
            for (std::size_t i = 0; i < t.parameters.size(); ++i)
                t.parameters[i] = fields::f32(b, 0x28 + i * 4);
            return t;
        }

    } // namespace

    std::vector<TerrainTypeEntry> read_terrain_types(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);
        const PointerFields pointers(overlay);

        std::vector<TerrainTypeEntry> types;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTerrainTag)
                continue;
            types.push_back({ parts.name, read_terrain(space, refs, pointers, symbol.data) });
        }
        return types;
    }

} // namespace rct3::ovl
