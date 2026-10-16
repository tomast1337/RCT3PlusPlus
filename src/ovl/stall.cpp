// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/stall.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "sta";
        constexpr std::size_t kVersion1Size = 0x28;
        constexpr std::size_t kLaterSize = 0x2C;      // up to and including the scenery item
        constexpr std::size_t kGoodsSize = 8;         // a carried-item reference and a price
        constexpr std::size_t kAttractionSize = 0x28; // the fields shared by all attractions
        constexpr std::uint32_t kMostGoods = 64;      // far more than any stall sells; guards a bad count

        // The attraction fields: at the start of version 1 items, in a block of their own that later items
        // point at. Version 1 items keep the scenery item at 0x30 in some and 0x38 in others, so it is left out.
        constexpr detail::AttractionLayout kFields { .type_id = 0x00, .name_text = 0x04, .description_text = 0x08, .icon = 0x0C, .spline = 0x24, .value_a = 0x10, .value_b = 0x14, .value_c = 0x20 };

        Stall read_later(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address, const ItemRecord& record, std::uint32_t version)
        {
            Stall stall;
            const std::uint32_t count = record.u32(0x04);
            const std::uint32_t fields = record.u32(0x24);
            if (fields != 0)
            {
                if (const auto block = ItemRecord::open(space, refs, fields, kAttractionSize))
                    stall.info = detail::read_attraction(*block, kFields, version);
            }
            else if (count <= kMostGoods)
            {
                // No pointer to the fields: take at least the type, their first word, from where they follow
                // the goods in the record, if the record reaches that far.
                const std::size_t type_at = kLaterSize + count * kGoodsSize;
                if (const auto whole = ItemRecord::open(space, refs, address, type_at + 4))
                    stall.info = detail::read_attraction(*whole, { .type_id = static_cast<int>(type_at) }, version);
            }
            stall.info.style = record.ref(0x28, "sid");
            if (count > 0 && count <= kMostGoods)
            {
                if (const auto goods = record.u32(0x08) == 0 ? std::nullopt : ItemRecord::open(space, refs, record.u32(0x08), count * kGoodsSize))
                {
                    for (std::uint32_t i = 0; i < count; ++i)
                        stall.goods.push_back({ goods->ref(i * kGoodsSize, "cid"), goods->i32(i * kGoodsSize + 4) });
                }
            }
            return stall;
        }

    } // namespace

    std::vector<StallItem> read_stalls(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const std::uint32_t version = part.file.header.version;
        const std::size_t size = version == 1 ? kVersion1Size : kLaterSize;

        std::vector<StallItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;

            StallItem item { parts.name, std::nullopt };
            if (const auto record = ItemRecord::open(space, refs, symbol.data, size))
            {
                if (version == 1)
                    item.stall = Stall { detail::read_attraction(*record, kFields, version), { } };
                else
                    item.stall = read_later(space, refs, symbol.data, *record, version);
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
