// SPDX-License-Identifier: GPL-3.0-or-later
//
// Things built inside animal enclosures: shelters, which are scenery items with extra paths for the animals
// to follow, and toys the animals play with.
#pragma once

#include "ovl/overlay.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct ShelterItem
    {
        std::string_view name;    ///< Symbol name without the tag
        std::string_view scenery; ///< "sid" item the shelter is placed as; it holds the name, icon and price
        std::string_view hut;     ///< "sid" item of the keeper's hut placed with it; empty for none
    };

    /// Shelters of one part of an overlay: pointer symbols tagged "asd", whose first field refers to the
    /// scenery item. Shelters whose record does not fit its block are left out.
    [[nodiscard]] std::vector<ShelterItem> read_shelters(const Overlay& overlay, const Part& part);

    struct AnimalToy
    {
        std::string_view name_text;        ///< "txt" item
        std::string_view description_text; ///< "txt" item
        std::string_view icon;             ///< "gsi" item
        std::string_view shape;            ///< "shs" item it is drawn with
        std::int32_t price = 0;            ///< Hundredths of the currency unit
    };

    struct AnimalToyItem
    {
        std::string_view name;        ///< Symbol name without the tag
        std::optional<AnimalToy> toy; ///< Nothing when the record does not fit its block
    };

    /// Animal toys of one part of an overlay: pointer symbols tagged "wai". Views in the result point into
    /// the overlay's bytes.
    [[nodiscard]] std::vector<AnimalToyItem> read_animal_toys(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
