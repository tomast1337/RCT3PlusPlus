// SPDX-License-Identifier: GPL-3.0-or-later
//
// Float values: named single-precision numbers stored directly in the symbol table, with no data of
// their own. Used for tuning values such as sizes and offsets.
#pragma once

#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

struct FloatItem {
  std::string_view name;  ///< Symbol name without the tag, a view into the part's bytes
  float value = 0;
};

/// Float values of one part of an overlay: the symbols tagged "flt" that hold a value rather than a
/// pointer. The symbol's data field is the IEEE-754 bit pattern of the number.
[[nodiscard]] std::vector<FloatItem> read_floats(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
