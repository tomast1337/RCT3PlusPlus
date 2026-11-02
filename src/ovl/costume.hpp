// SPDX-License-Identifier: GPL-3.0-or-later
//
// Entertainers' costumes: each names the body sections an entertainer in it wears, by the names of their
// part sets.
#pragma once

#include "ovl/overlay.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Layout read from every costume in the install; fields other than the sections are not read.
    struct Costume
    {
        /// Part set names ("ppg" items without the tag), head, body and legs first; empty for none.
        std::vector<std::string_view> sections;
        std::string_view name; ///< As stored, not shown to the player
    };

    struct CostumeEntry
    {
        std::string_view name; ///< Symbol name without the tag
        std::optional<Costume> costume;
    };

    /// Costumes of one part of an overlay: pointer symbols tagged "ent". Views point into the overlay's bytes.
    [[nodiscard]] std::vector<CostumeEntry> read_costumes(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
