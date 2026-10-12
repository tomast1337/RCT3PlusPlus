// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/research_category.hpp"

#include "ovl/field_values.hpp"
#include "ovl/reference_names.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kCategoryTag = "rcg";
constexpr std::string_view kTextTag = "txt";
constexpr std::size_t kSize = 40;
// Real categories have at most two prerequisites.
constexpr std::uint32_t kMaxPrerequisites = 1u << 8;

std::optional<ResearchCategory> read_category(const AddressSpace& space, const ReferenceNames& refs,
                                              const PointerFields& pointers, std::uint32_t address) {
  const auto raw = space.bytes_at(address, kSize);
  if (!raw) return std::nullopt;
  const auto b = *raw;

  ResearchCategory c;
  c.kind = fields::u32(b, 0x00);
  c.name = fields::string_field(space, pointers, address + 4, fields::u32(b, 0x04));
  c.name_text = refs.find(address + 8, kTextTag);
  c.unknown_0c = fields::f32(b, 0x0c);
  c.unknown_10 = fields::f32(b, 0x10);

  const std::uint32_t count = fields::u32(b, 0x14);
  if (count > kMaxPrerequisites) return std::nullopt;
  if (count != 0) {
    const std::uint32_t slots = fields::u32(b, 0x18);
    if (!space.bytes_at(slots, std::size_t{count} * 4)) return std::nullopt;
    c.prerequisites.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) c.prerequisites.push_back(refs.find(slots + i * 4, kCategoryTag));
  }
  return c;
}

}  // namespace

std::vector<ResearchCategoryEntry> read_research_categories(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  const ReferenceNames refs(part, space);
  const PointerFields pointers(overlay);

  std::vector<ResearchCategoryEntry> categories;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kCategoryTag) continue;
    categories.push_back({parts.name, read_category(space, refs, pointers, symbol.data)});
  }
  return categories;
}

}  // namespace rct3::ovl
