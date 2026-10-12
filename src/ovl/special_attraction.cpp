// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/special_attraction.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kTag = "sat";

}  // namespace

std::vector<SpecialAttractionItem> read_special_attractions(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceIndex refs(part, space);
  const std::uint32_t version = part.file.header.version;
  const bool first = version == 1;
  const std::size_t size = first ? 52 : 80;
  const detail::AttractionLayout at =
      first ? detail::AttractionLayout{.type_id = 0x00, .name_text = 0x04, .description_text = 0x08,
                                       .icon = 0x0C, .style = 0x30, .spline = 0x24, .value_a = 0x10,
                                       .value_b = 0x14, .value_c = 0x20}
            : detail::AttractionLayout{.type_id = 0x0C, .name_text = 0x10, .description_text = 0x14,
                                       .icon = 0x18, .style = 0x08, .spline = 0x30, .value_a = 0x1C,
                                       .value_b = 0x20, .value_c = 0x2C};

  std::vector<SpecialAttractionItem> items;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kTag) continue;

    SpecialAttractionItem item{parts.name, std::nullopt};
    // No version 4 file holds one, so that layout is not known.
    if (version == 4) {
      items.push_back(std::move(item));
      continue;
    }
    if (const auto record = ItemRecord::open(space, refs, symbol.data, size)) {
      SpecialAttraction a;
      a.info = detail::read_attraction(*record, at, version);
      if (!first) a.name = record->text(0x04);
      item.attraction = std::move(a);
    }
    items.push_back(std::move(item));
  }
  return items;
}

}  // namespace rct3::ovl
