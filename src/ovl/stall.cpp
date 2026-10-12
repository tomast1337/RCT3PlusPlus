// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/stall.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kTag = "sta";

detail::AttractionLayout layout(std::uint32_t version) {
  if (version == 1) {
    // The scenery item sits at 0x30 in some items and 0x38 in others, so it is left out.
    return {.type_id = 0x00, .name_text = 0x04, .description_text = 0x08, .icon = 0x0C, .spline = 0x24,
            .value_a = 0x10, .value_b = 0x14, .value_c = 0x20};
  }
  return {.style = 0x28};
}

}  // namespace

std::vector<StallItem> read_stalls(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceIndex refs(part, space);
  const std::uint32_t version = part.file.header.version;
  const auto at = layout(version);
  const std::size_t size = version == 1 ? 0x28 : 0x2C;

  std::vector<StallItem> items;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kTag) continue;

    StallItem item{parts.name, std::nullopt};
    if (const auto record = ItemRecord::open(space, refs, symbol.data, size)) {
      item.stall = Stall{detail::read_attraction(*record, at, version)};
    }
    items.push_back(std::move(item));
  }
  return items;
}

}  // namespace rct3::ovl
