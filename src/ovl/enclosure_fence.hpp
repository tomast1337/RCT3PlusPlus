// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal enclosure fences: one item per kind of fence an enclosure can be built with (wood, chain link,
// electric), naming the scenery pieces a fence is made of and what it costs.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct EnclosureFence
    {
        std::string_view name_text; ///< "txt" item
        std::string_view icon;      ///< "gsi" item
        /// Names of the scenery items the fence is built from: full panel, end, corner, edge, inner corner,
        /// gate, and the three panels of a viewing window.
        std::array<std::string_view, 9> pieces;
        /// Hundredths of the currency unit. Building a length of fence costs `fence_cost`; the refund for
        /// taking it down is stored as a negative amount. The gate pair is read the same way. [The split into
        /// cost and refund is inferred from the values: the refunds are a little under the costs.]
        std::int32_t fence_cost = 0;
        std::int32_t fence_refund = 0;
        std::int32_t gate_cost = 0;
        std::int32_t gate_refund = 0;
        /// How strong the fence is: a species needs a fence at least as strong as it says.
        float strength = 0;
    };

    struct EnclosureFenceItem
    {
        std::string_view name;               ///< Symbol name without the tag
        std::optional<EnclosureFence> fence; ///< Nothing when the record does not fit its block
    };

    /// Enclosure fences of one part of an overlay: pointer symbols tagged "enc". The record starts with a
    /// word, then the name and icon references, nine pointers to piece names, the four amounts, four spline references and the strength. Views in
    /// the result point into the overlay's bytes.
    [[nodiscard]] std::vector<EnclosureFenceItem> read_enclosure_fences(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
