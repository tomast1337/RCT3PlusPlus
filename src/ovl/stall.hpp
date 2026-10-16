// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stalls: shops, food stands, toilets and the like.
#pragma once

#include "ovl/attraction.hpp"
#include "ovl/overlay.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One thing a stall sells and the price it starts at.
    struct StallGoods
    {
        std::string_view item;  ///< "cid" item
        std::int32_t price = 0; ///< In hundredths of the currency unit
    };

    struct Stall
    {
        /// Version 1 files hold all of it but the scenery item and the goods.
        AttractionInfo info;
        /// Version 4 and 5 files only.
        std::vector<StallGoods> goods;
    };

    struct StallItem
    {
        std::string_view name;      ///< Symbol name without the tag
        std::optional<Stall> stall; ///< Nothing when the start of the record does not fit its block
    };

    /// Stalls of one part of an overlay: pointer symbols tagged "sta".
    ///
    /// Version 4 and 5 items start with a word of -1, the number of goods and a pointer to them, then a
    /// pointer at 0x24 to the attraction fields and the scenery item at 0x28. The goods follow as pairs of
    /// a carried-item reference and a price, and the attraction fields follow them, laid out as in a version
    /// 1 item. When the pointer to the fields is null, only the type is read, from where the fields would
    /// follow the goods. Version 1 items are read from their start only. Views in the result point into the
    /// overlay's bytes.
    [[nodiscard]] std::vector<StallItem> read_stalls(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
