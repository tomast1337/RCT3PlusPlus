// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_species.hpp"

#include "ovl/item_record.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "was";
        constexpr std::size_t kSize = 0x3C; // up to and including the fence strength
        constexpr std::size_t kName = 0x08;
        constexpr std::size_t kDescription = 0x0C;
        constexpr std::size_t kIcon = 0x10;
        constexpr std::size_t kVariants = 0x18;
        constexpr std::size_t kShelter = 0x30;
        constexpr std::size_t kFenceStrength = 0x38;

        // An animal's own block.
        constexpr std::size_t kVariantSize = 0x34;
        constexpr std::size_t kVariantPrice = 0x14;
        constexpr std::size_t kVariantRarity = 0x18;
        constexpr std::size_t kVariantDescription = 0x1C;
        constexpr std::size_t kVariantIcon = 0x30;

    } // namespace

    std::vector<AnimalSpeciesItem> read_animal_species(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        std::vector<AnimalSpeciesItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;
            AnimalSpeciesItem item { parts.name, std::nullopt };
            if (const auto record = ItemRecord::open(space, refs, symbol.data, kSize))
            {
                AnimalSpecies s;
                s.name_text = record->ref(kName, "txt");
                s.description_text = record->ref(kDescription, "txt");
                s.icon = record->ref(kIcon, "gsi");
                s.shelter = record->text(kShelter);
                s.fence_strength = record->real(kFenceStrength);
                for (std::size_t i = 0; i < s.variants.size(); ++i)
                {
                    const std::uint32_t at = record->u32(kVariants + 4 * i);
                    if (at == 0)
                        continue;
                    if (const auto v = ItemRecord::open(space, refs, at, kVariantSize))
                        s.variants[i] = AnimalVariant { v->ref(kVariantDescription, "txt"), v->ref(kVariantIcon, "gsi"), v->i32(kVariantPrice), v->real(kVariantRarity) };
                }
                item.species = s;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
