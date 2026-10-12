// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/float_value.hpp"

#include <bit>
#include <cstdint>

namespace rct3::ovl {

namespace {
constexpr std::string_view kFloatTag = "flt";
}

std::vector<FloatItem> read_floats(const Overlay& /*overlay*/, const Part& part) {
  std::vector<FloatItem> items;
  for (const auto& symbol : part.symbols) {
    if (symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kFloatTag) continue;
    items.push_back({parts.name, std::bit_cast<float>(static_cast<std::uint32_t>(symbol.data))});
  }
  return items;
}

}  // namespace rct3::ovl
