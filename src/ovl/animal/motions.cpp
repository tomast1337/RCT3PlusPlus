// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal/motions.hpp"

#include "core/types.hpp"
#include "ovl/item_record.hpp"

namespace rct3::ovl::animal
{

    namespace
    {

        constexpr std::string_view kTag = "wad";
        constexpr usize kSize = 0x14; // up to and including the lengths' pointer
        constexpr usize kCount = 0x08;
        constexpr usize kAnimations = 0x0C;
        constexpr usize kSeconds = 0x10;
        constexpr u32 kMaxSlots = 256;

    } // namespace

    std::vector<AnimalMotionsItem> read_animal_motions(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        std::vector<AnimalMotionsItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;
            AnimalMotionsItem item { parts.name, std::nullopt };
            const auto record = ItemRecord::open(space, refs, symbol.data, kSize);
            const u32 count = record ? record->read_u32(kCount) : 0;
            const auto animations = record && count <= kMaxSlots ? ItemRecord::open(space, refs, record->read_u32(kAnimations), usize { count } * 4) : std::nullopt;
            const auto seconds = record && count <= kMaxSlots ? ItemRecord::open(space, refs, record->read_u32(kSeconds), usize { count } * 4) : std::nullopt;
            if (animations && seconds)
            {
                AnimalMotions m;
                for (u32 i = 0; i < count; ++i)
                {
                    m.animations.push_back(animations->ref(usize { i } * 4, "modelanim"));
                    m.seconds.push_back(seconds->real(usize { i } * 4));
                }
                item.motions = std::move(m);
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl::animal