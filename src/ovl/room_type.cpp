// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/room_type.hpp"

#include "ovl/field_values.hpp"
#include "ovl/reference_names.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kRoomTag = "chg";
constexpr std::string_view kGalleryTag = "vwg";
constexpr std::size_t kRoomSize = 76;
constexpr std::size_t kGallerySize = 72;

std::optional<ChangingRoom> read_room(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address) {
  const auto raw = space.bytes_at(address, kRoomSize);
  if (!raw) return std::nullopt;
  const auto b = *raw;
  ChangingRoom r;
  r.scenery_item = refs.find(address + 0x04, "sid");
  r.spline = refs.find(address + 0x08, "spl");
  r.unknown_0c = fields::u32(b, 0x0c);
  r.name_text = refs.find(address + 0x10, "txt");
  r.description_text = refs.find(address + 0x14, "txt");
  r.icon = refs.find(address + 0x18, "gsi");
  r.unknown_2c = fields::u32(b, 0x2c);
  r.second_spline = refs.find(address + 0x30, "spl");
  r.unknown_40 = fields::i32(b, 0x40);
  r.unknown_44 = fields::u32(b, 0x44);
  r.unknown_48 = fields::u32(b, 0x48);
  std::size_t i = 0;
  for (std::size_t at : {0x1c, 0x20, 0x24, 0x28, 0x34, 0x38, 0x3c}) r.reserved[i++] = fields::u32(b, at);
  return r;
}

std::optional<ViewingGallery> read_gallery(const AddressSpace& space, const ReferenceNames& refs,
                                           std::uint32_t address) {
  const auto raw = space.bytes_at(address, kGallerySize);
  if (!raw) return std::nullopt;
  const auto b = *raw;
  ViewingGallery g;
  g.unknown_04 = fields::u32(b, 0x04);
  g.name_text = refs.find(address + 0x08, "txt");
  g.description_text = refs.find(address + 0x0c, "txt");
  g.icon = refs.find(address + 0x10, "gsi");
  g.unknown_24 = fields::u32(b, 0x24);
  g.spline = refs.find(address + 0x28, "spl");
  g.unknown_38 = fields::i32(b, 0x38);
  g.unknown_3c = fields::u32(b, 0x3c);
  std::size_t i = 0;
  for (std::size_t at : {0x14, 0x18, 0x1c, 0x20, 0x2c, 0x30, 0x34, 0x40, 0x44}) g.reserved[i++] = fields::u32(b, at);
  return g;
}

}  // namespace

std::vector<ChangingRoomEntry> read_changing_rooms(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceNames refs(part, space);
  std::vector<ChangingRoomEntry> out;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag == kRoomTag) out.push_back({parts.name, read_room(space, refs, symbol.data)});
  }
  return out;
}

std::vector<ViewingGalleryEntry> read_viewing_galleries(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceNames refs(part, space);
  std::vector<ViewingGalleryEntry> out;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag == kGalleryTag) out.push_back({parts.name, read_gallery(space, refs, symbol.data)});
  }
  return out;
}

}  // namespace rct3::ovl
