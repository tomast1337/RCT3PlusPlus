// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal/species.hpp"

#include "core/types.hpp"
#include "ovl/item_record.hpp"

namespace rct3::ovl::animal
{

    namespace
    {

        constexpr std::string_view kTag = "was";
        constexpr usize kSize = 0x3C; // up to and including the fence strength
        constexpr usize kName = 0x08;
        constexpr usize kDescription = 0x0C;
        constexpr usize kIcon = 0x10;
        constexpr usize kVariants = 0x18;
        constexpr usize kShelter = 0x30;
        constexpr usize kFenceStrength = 0x38;

        // An animal's own block.
        constexpr usize kVariantSize = 0x34;
        constexpr usize kVariantModel = 0x00;
        constexpr usize kVariantMotions = 0x04;
        constexpr usize kVariantPrice = 0x14;
        constexpr usize kVariantRarity = 0x18;
        constexpr usize kVariantDescription = 0x1C;
        constexpr usize kVariantIcon = 0x30;

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
                for (usize i = 0; i < s.variants.size(); ++i)
                {
                    const u32 at = record->read_u32(kVariants + 4 * i);
                    if (at == 0)
                        continue;
                    if (const auto v = ItemRecord::open(space, refs, at, kVariantSize))
                        s.variants[i] = AnimalVariant { v->ref(kVariantModel, "mdl"), v->ref(kVariantMotions, "wad"), v->ref(kVariantDescription, "txt"), v->ref(kVariantIcon, "gsi"), v->read_i32(kVariantPrice), v->real(kVariantRarity) };
                }
                item.species = s;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl::animal