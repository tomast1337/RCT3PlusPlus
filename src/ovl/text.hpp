// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text items: named strings stored as NUL-terminated UTF-16LE.
#pragma once

#include "ovl/overlay.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// A text item. `value` is empty (nullopt) when its string pointer does not resolve to a terminated
    /// string inside one block.
    struct TextItem
    {
        std::string_view name; ///< Symbol name without the type tag, a view into the part's bytes
        std::optional<std::u16string> value;
    };

    /// Text items of one part of an overlay: the symbols tagged "txt" that hold a pointer. Names resolve
    /// through `overlay`, which must be the overlay `part` belongs to.
    [[nodiscard]] std::vector<TextItem> read_texts(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
