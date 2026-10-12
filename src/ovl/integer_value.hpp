// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integer values: named whole numbers stored directly in the symbol table, with no data of their own.
// Used for small settings such as flags and indices.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

struct IntegerItem {
  std::string_view name;  ///< Symbol name without the tag, a view into the part's bytes
  std::int32_t value = 0;
};

/// Integer values of one part of an overlay: the symbols tagged "int" that hold a value rather than a
/// pointer. The symbol's data field is the number as a 32-bit two's complement value. Only values from
/// 0 to 8 are present in files seen, so the signedness is not confirmed.
[[nodiscard]] std::vector<IntegerItem> read_integers(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
