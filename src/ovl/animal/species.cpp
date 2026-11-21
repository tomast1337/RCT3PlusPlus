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
        constexpr usize kOwnName = 0x04;
        constexpr usize kName = 0x08;
        constexpr usize kDescription = 0x0C;
        constexpr usize kIcon = 0x10;
        constexpr usize kVariants = 0x18;
        constexpr usize kNature = 0x28;
        constexpr usize kStatistics = 0x2C;
        constexpr usize kShelter = 0x30;
        constexpr usize kFenceStrength = 0x38;

        // The species' nature and statistics blocks, as far as read.
        constexpr usize kNatureSize = 0x0C;
        constexpr usize kAppetite = 0x00;
        constexpr usize kSociability = 0x08;
        constexpr usize kStatisticsSize = 0x10;
        constexpr usize kPreyCount = 0x00;
        constexpr usize kPreyList = 0x04;
        constexpr usize kSpace = 0x0C;
        constexpr i32 kMostPrey = 64; // Far over the dozen or so a meat-eater of the game hunts

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
                s.own_name = record->text(kOwnName);
                s.name_text = record->ref(kName, "txt");
                s.description_text = record->ref(kDescription, "txt");
                s.icon = record->ref(kIcon, "gsi");
                s.shelter = record->text(kShelter);
                s.fence_strength = record->real(kFenceStrength);
                if (const u32 at = record->read_u32(kNature); at != 0)
                    if (const auto nature = ItemRecord::open(space, refs, at, kNatureSize))
                    {
                        s.care.appetite = nature->read_i32(kAppetite);
                        s.care.sociability = nature->read_i32(kSociability);
                    }
                if (const u32 at = record->read_u32(kStatistics); at != 0)
                    if (const auto stats = ItemRecord::open(space, refs, at, kStatisticsSize))
                    {
                        s.care.space = stats->read_i32(kSpace);
                        const i32 prey = stats->read_i32(kPreyCount);
                        if (prey > 0 && prey <= kMostPrey)
                            if (const auto list = ItemRecord::open(space, refs, stats->read_u32(kPreyList), 4 * static_cast<usize>(prey)))
                                for (usize i = 0; i < static_cast<usize>(prey); ++i)
                                    // Not ItemRecord::text: a pointer of 0 here is the start of the data, not null.
                                    if (const auto name = space.cstring_at(list->read_u32(4 * i)))
                                        s.prey.push_back(*name);
                    }
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