// SPDX-License-Identifier: GPL-3.0-or-later
//
// Special attractions: park facilities that are not rides or stalls, such as the medical centre, the
// toilets and the cash machine.
#pragma once

#include "ovl/attraction.hpp"
#include "ovl/overlay.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct SpecialAttraction
    {
        AttractionInfo info;
        std::string_view name; ///< Name string stored in the item, version 5 files only
    };

    struct SpecialAttractionItem
    {
        std::string_view name;                       ///< Symbol name without the tag
        std::optional<SpecialAttraction> attraction; ///< Nothing when the record does not fit its block
    };

    /// Special attractions of one part of an overlay: pointer symbols tagged "sat". Items are 52 bytes in
    /// version 1 files and 80 bytes in version 5 files. The part after the fields shared by all
    /// attractions is not interpreted. Views in the result point into the overlay's bytes.
    [[nodiscard]] std::vector<SpecialAttractionItem> read_special_attractions(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
