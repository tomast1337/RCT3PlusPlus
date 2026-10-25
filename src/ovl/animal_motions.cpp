// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_motions.hpp"

#include "ovl/item_record.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "wad";
        constexpr std::size_t kSize = 0x14; // up to and including the lengths' pointer
        constexpr std::size_t kCount = 0x08;
        constexpr std::size_t kAnimations = 0x0C;
        constexpr std::size_t kSeconds = 0x10;
        constexpr std::uint32_t kMaxSlots = 256;

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
            const std::uint32_t count = record ? record->u32(kCount) : 0;
            const auto animations = record && count <= kMaxSlots ? ItemRecord::open(space, refs, record->u32(kAnimations), std::size_t { count } * 4) : std::nullopt;
            const auto seconds = record && count <= kMaxSlots ? ItemRecord::open(space, refs, record->u32(kSeconds), std::size_t { count } * 4) : std::nullopt;
            if (animations && seconds)
            {
                AnimalMotions m;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    m.animations.push_back(animations->ref(std::size_t { i } * 4, "modelanim"));
                    m.seconds.push_back(seconds->real(std::size_t { i } * 4));
                }
                item.motions = std::move(m);
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
