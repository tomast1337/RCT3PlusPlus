// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stalls: shops, food stands, toilets and the like.
#pragma once

#include "ovl/attraction.hpp"
#include "ovl/overlay.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct Stall
    {
        /// Version 1 files hold all of it. Version 4 and 5 files give only `info.style`, because their items
        /// start with a different layout that is not read yet.
        AttractionInfo info;
    };

    struct StallItem
    {
        std::string_view name;      ///< Symbol name without the tag
        std::optional<Stall> stall; ///< Nothing when the start of the record does not fit its block
    };

    /// Stalls of one part of an overlay: pointer symbols tagged "sta". Only the start of each item is read.
    /// The rest is a list of carried items sold with their prices and further references, which is not
    /// interpreted yet. Views in the result point into the overlay's bytes.
    [[nodiscard]] std::vector<StallItem> read_stalls(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
