// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal species for zoo enclosures: one item per species, with what the park builder shows of it and the
// four animals of the species a park can buy (adult male and female, young male and female).
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl::animal
{

    /// One animal of a species that can be bought.
    struct AnimalVariant
    {
        std::string_view model;            ///< "mdl" item (animal/model.hpp)
        std::string_view motions;          ///< "wad" item (animal/motions.hpp)
        std::string_view description_text; ///< "txt" item
        std::string_view icon;             ///< "gsi" item; adults of both sexes often share one
        i32 price = 0;                     ///< Hundredths of the currency unit
        f32 rarity = 0;                    ///< From 0, common, to 1, endangered
    };

    /// The order of `AnimalSpecies::variants`.
    enum class AnimalAge : u8
    {
        AdultMale,
        AdultFemale,
        YoungMale,
        YoungFemale,
    };

    /// What keeps the animals of a species well, from its data.
    struct SpeciesCare
    {
        i32 space = 0;       ///< Tiles of enclosure each animal wants
        i32 appetite = 0;    ///< How much an animal eats, from 1 to 20 at most; each animal's own lies near it
        i32 sociability = 0; ///< How much it wants others of its kind about, from 0 to 20 at most; likewise
    };

    struct AnimalSpecies
    {
        std::string_view own_name;         ///< As the data spells it; the keys of the species' facts start with it
        std::string_view name_text;        ///< "txt" item
        std::string_view description_text; ///< "txt" item
        std::string_view icon;             ///< "gsi" item
        std::string_view shelter;          ///< Name of the shelter the species needs, as stored (any case)
        f32 fence_strength = 0;            ///< The least strength of fence that holds the species
        SpeciesCare care { };              ///< Zero where its blocks are missing or do not fit
        /// Nothing where the pointer is null or the record does not fit its block.
        std::array<std::optional<AnimalVariant>, 4> variants;
    };

    struct AnimalSpeciesItem
    {
        std::string_view name;                ///< Symbol name without the tag
        std::optional<AnimalSpecies> species; ///< Nothing when the record does not fit its block
    };

    /// Animal species of one part of an overlay: pointer symbols tagged "was". Parks name a species by its
    /// symbol name. The record holds a pointer to its own name at 0x04, its name, description and icon references from 0x08, four pointers to the animals
    /// (each with its model, motion set, description, icon and price), a pointer to the name of its shelter and the fence strength it needs. Two more
    /// pointers lead to blocks of its care: at 0x28 the middle of its animals' nature (appetite at 0x00, sociability at 0x08, both i32) and at 0x2C its
    /// statistics (the space each animal wants, i32 at 0x0C). Views in the result point into the overlay's bytes.
    [[nodiscard]] std::vector<AnimalSpeciesItem> read_animal_species(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl::animal