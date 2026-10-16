// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/enclosure_fence.hpp"

#include "ovl/item_record.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "enc";
        constexpr std::size_t kSize = 0x54; // up to and including the strength
        constexpr std::size_t kName = 0x04;
        constexpr std::size_t kIcon = 0x08;
        constexpr std::size_t kPieces = 0x0C;
        constexpr std::size_t kFenceCost = 0x30;
        constexpr std::size_t kFenceRefund = 0x34;
        constexpr std::size_t kGateCost = 0x38;
        constexpr std::size_t kGateRefund = 0x3C;
        constexpr std::size_t kStrength = 0x50;

    } // namespace

    std::vector<EnclosureFenceItem> read_enclosure_fences(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        std::vector<EnclosureFenceItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;
            EnclosureFenceItem item { parts.name, std::nullopt };
            if (const auto record = ItemRecord::open(space, refs, symbol.data, kSize))
            {
                EnclosureFence f;
                f.name_text = record->ref(kName, "txt");
                f.icon = record->ref(kIcon, "gsi");
                for (std::size_t i = 0; i < f.pieces.size(); ++i)
                    f.pieces[i] = record->text(kPieces + 4 * i);
                f.fence_cost = record->i32(kFenceCost);
                f.fence_refund = record->i32(kFenceRefund);
                f.gate_cost = record->i32(kGateCost);
                f.gate_refund = record->i32(kGateRefund);
                f.strength = record->real(kStrength);
                item.fence = f;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
