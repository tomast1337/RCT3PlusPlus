// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_items.hpp"

#include "ovl/item_record.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kShelterTag = "asd";
        constexpr std::size_t kShelterSize = 0x14; // up to and including the hut
        constexpr std::size_t kShelterHut = 0x10;

        constexpr std::string_view kToyTag = "wai";
        constexpr std::size_t kToySize = 0x58; // up to and including the description
        constexpr std::size_t kToyShape = 0x14;
        constexpr std::size_t kToyPrice = 0x1C;
        constexpr std::size_t kToyIcon = 0x38;
        constexpr std::size_t kToyName = 0x3C;
        constexpr std::size_t kToyDescription = 0x54;

        template<typename Read> void each_item(const Overlay& overlay, const Part& part, std::string_view tag, Read read)
        {
            const AddressSpace space = overlay.space();
            const ReferenceIndex refs(part, space);
            for (const auto& symbol : part.symbols)
            {
                if (!symbol.is_pointer)
                    continue;
                const SymbolName parts = split_symbol_name(symbol.name);
                if (parts.tag == tag)
                    read(parts.name, space, refs, symbol.data);
            }
        }

    } // namespace

    std::vector<ShelterItem> read_shelters(const Overlay& overlay, const Part& part)
    {
        std::vector<ShelterItem> items;
        each_item(overlay, part, kShelterTag, [&](std::string_view name, const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t at) {
            if (const auto record = ItemRecord::open(space, refs, at, kShelterSize))
                items.push_back({ name, record->ref(0, "sid"), record->ref(kShelterHut, "sid") });
        });
        return items;
    }

    std::vector<AnimalToyItem> read_animal_toys(const Overlay& overlay, const Part& part)
    {
        std::vector<AnimalToyItem> items;
        each_item(overlay, part, kToyTag, [&](std::string_view name, const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t at) {
            AnimalToyItem item { name, std::nullopt };
            if (const auto record = ItemRecord::open(space, refs, at, kToySize))
                item.toy = AnimalToy { record->ref(kToyName, "txt"), record->ref(kToyDescription, "txt"), record->ref(kToyIcon, "gsi"), record->ref(kToyShape, "shs"), record->i32(kToyPrice) };
            items.push_back(item);
        });
        return items;
    }

} // namespace rct3::ovl
