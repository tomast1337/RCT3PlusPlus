// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/ride_train.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kTag = "rit";
constexpr std::size_t kFixedSize = 184;

}  // namespace

std::vector<RideTrainItem> read_ride_trains(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceIndex refs(part, space);

  std::vector<RideTrainItem> items;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kTag) continue;

    RideTrainItem item{parts.name, std::nullopt};
    if (const auto record = ItemRecord::open(space, refs, symbol.data, kFixedSize)) {
      RideTrain train;
      train.name = record->text(0x00);
      train.display_name = record->text(0x04);
      for (std::size_t i = 0; i < train.cars.size(); ++i) train.cars[i] = record->ref(0x08 + i * 4, "ric");
      train.references = record->references(static_cast<std::uint32_t>(kFixedSize));
      train.record = record->bytes();
      item.train = std::move(train);
    }
    items.push_back(std::move(item));
  }
  return items;
}

}  // namespace rct3::ovl
