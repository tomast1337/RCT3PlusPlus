// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the interface's lists of things lay their rows: flat bands of one height, every other one tinted, and
// a light band for what stands out, such as the row picked or a heading over a run of rows. A scrolled list
// keeps a gutter at its right for the bar.
#pragma once

#include "ui/draw_list.hpp"

#include <cstddef>

namespace rct3::ui
{

    struct RowBands
    {
        float height = 16;
        Color stripe { };     ///< Behind the first row, and every other one after it
        Color plain = kWhite; ///< Behind the others
        Color band { };       ///< Behind a row that stands out
        float gutter = 18;    ///< Kept clear at the right for the scroll bar
    };

    /// Fills `row`, the `index`th of its list, with its stripe.
    inline void fill_row(DrawList& list, const RowBands& bands, const Rect& row, std::size_t index)
    {
        list.fill(row, index % 2 == 0 ? bands.stripe : bands.plain);
    }

} // namespace rct3::ui
