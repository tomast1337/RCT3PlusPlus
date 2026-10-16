// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal species for zoo enclosures: one item per species, with what the park builder shows of it and the
// four animals of the species a park can buy (adult male and female, young male and female).
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One animal of a species that can be bought.
    struct AnimalVariant
    {
        std::string_view description_text; ///< "txt" item
        std::string_view icon;             ///< "gsi" item; adults of both sexes often share one
        std::int32_t price = 0;            ///< Hundredths of the currency unit
        float rarity = 0;                  ///< From 0, common, to 1, endangered
    };

    /// The order of `AnimalSpecies::variants`.
    enum class AnimalAge : std::uint8_t
    {
        AdultMale,
        AdultFemale,
        YoungMale,
        YoungFemale,
    };

    struct AnimalSpecies
    {
        std::string_view name_text;        ///< "txt" item
        std::string_view description_text; ///< "txt" item
        std::string_view icon;             ///< "gsi" item
        std::string_view shelter;          ///< Name of the shelter the species needs, as stored (any case)
        float fence_strength = 0;          ///< The least strength of fence that holds the species
        /// Nothing where the pointer is null or the record does not fit its block.
        std::array<std::optional<AnimalVariant>, 4> variants;
    };

    struct AnimalSpeciesItem
    {
        std::string_view name;                ///< Symbol name without the tag
        std::optional<AnimalSpecies> species; ///< Nothing when the record does not fit its block
    };

    /// Animal species of one part of an overlay: pointer symbols tagged "was". Parks name a species by its
    /// symbol name. The record holds its name, description and icon references from 0x08, four pointers to the animals
    /// (each with its description, icon and price), a pointer to the name of its shelter and the fence strength it needs. Views in the
    /// result point into the overlay's bytes.
    [[nodiscard]] std::vector<AnimalSpeciesItem> read_animal_species(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
