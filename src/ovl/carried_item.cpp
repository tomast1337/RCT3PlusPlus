// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/carried_item.hpp"

#include "ovl/field_values.hpp"
#include "ovl/reference_names.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kItemTag = "cid";
        constexpr std::string_view kExtraTag = "ced";
        constexpr std::string_view kTextTag = "txt";
        constexpr std::string_view kIconTag = "gsi";
        constexpr std::string_view kShapeTag = "shs";
        constexpr std::string_view kTextureTag = "ftx";

        constexpr std::size_t kItemSize = 0x9c;
        constexpr std::size_t kExtraSize = 28;
        // Real items have at most three extras.
        constexpr std::uint32_t kMaxExtras = 1u << 10;

        std::optional<CarriedItem> read_item(const AddressSpace& space, const ReferenceNames& refs, const PointerFields& pointers, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kItemSize);
            if (!raw)
                return std::nullopt;
            const auto b = *raw;
            using fields::f32;
            using fields::i32;
            using fields::u32;

            CarriedItem item;
            item.unknown_00 = u32(b, 0x00);
            item.unknown_04 = u32(b, 0x04);
            item.unknown_08 = u32(b, 0x08);
            item.name_text = refs.find(address + 0x0c, kTextTag);
            item.plural_text = refs.find(address + 0x10, kTextTag);
            item.icon = refs.find(address + 0x14, kIconTag);
            item.shapes = { refs.find(address + 0x18, kShapeTag), refs.find(address + 0x1c, kShapeTag) };
            item.unknown_20 = f32(b, 0x20);
            item.remains = refs.find(address + 0x24, kItemTag);
            item.flags = u32(b, 0x28);
            item.unknown_2c = i32(b, 0x2c);
            item.unknown_30 = u32(b, 0x30);
            for (std::size_t i = 0; i < 3; ++i)
                item.offset[i] = f32(b, 0x34 + i * 4);
            for (std::size_t i = 0; i < 4; ++i)
                item.amounts[i] = u32(b, 0x40 + i * 4);
            item.unknown_50 = f32(b, 0x50);
            item.unknown_54 = i32(b, 0x54);
            item.unknown_58 = u32(b, 0x58);

            // The extras are reference fields stored right after the structure.
            const std::uint32_t extra_count = u32(b, 0x5c);
            if (extra_count > kMaxExtras)
                return std::nullopt;
            if (extra_count != 0)
            {
                const std::uint32_t slots = u32(b, 0x60);
                if (!space.bytes_at(slots, std::size_t { extra_count } * 4))
                    return std::nullopt;
                item.extras.reserve(extra_count);
                for (std::uint32_t i = 0; i < extra_count; ++i)
                    item.extras.push_back(refs.find(slots + i * 4, kExtraTag));
            }

            item.unknown_70 = u32(b, 0x70);
            item.texture = refs.find(address + 0x74, kTextureTag);
            item.scale = { f32(b, 0x78), f32(b, 0x7c) };
            item.unknown_80 = u32(b, 0x80);
            for (std::size_t i = 0; i < 4; ++i)
            {
                const std::size_t at = 0x84 + i * 4;
                item.part_names[i] = fields::string_field(space, pointers, address + static_cast<std::uint32_t>(at), u32(b, at));
            }
            item.unknown_94 = u32(b, 0x94);
            item.style_name = fields::string_field(space, pointers, address + 0x98, u32(b, 0x98));
            return item;
        }

        std::optional<CarriedItemExtra> read_extra(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kExtraSize);
            if (!raw)
                return std::nullopt;
            const auto b = *raw;
            CarriedItemExtra extra;
            extra.unknown_00 = fields::u32(b, 0);
            extra.name_text = refs.find(address + 4, kTextTag);
            extra.icon = refs.find(address + 8, kIconTag);
            extra.unknown_0c = fields::u32(b, 0x0c);
            for (std::size_t i = 0; i < 3; ++i)
                extra.offset[i] = fields::f32(b, 0x10 + i * 4);
            return extra;
        }

    } // namespace

    std::vector<CarriedItemEntry> read_carried_items(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);
        const PointerFields pointers(overlay);

        std::vector<CarriedItemEntry> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kItemTag)
                continue;
            items.push_back({ parts.name, read_item(space, refs, pointers, symbol.data) });
        }
        return items;
    }

    std::vector<CarriedItemExtraEntry> read_carried_item_extras(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);

        std::vector<CarriedItemExtraEntry> extras;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kExtraTag)
                continue;
            extras.push_back({ parts.name, read_extra(space, refs, symbol.data) });
        }
        return extras;
    }

} // namespace rct3::ovl
