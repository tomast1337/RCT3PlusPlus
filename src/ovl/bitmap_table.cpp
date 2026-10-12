// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/bitmap_table.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace rct3::ovl {

namespace {

constexpr std::string_view kTableTag = "btbl";
constexpr std::size_t kRowDataSize = 8;    // {reserved, bitmap count}
constexpr std::size_t kPrefixSize = 8;     // two words before the bitmap descriptions
constexpr std::size_t kBitmapSize = 16;    // {format, width, height, levels}
constexpr std::uint32_t kMaxSide = 2048;
constexpr std::uint32_t kMaxLevels = 11;

std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, b.data() + at, sizeof v);
  return v;
}

std::optional<std::span<const std::byte>> chunk_bytes(const Part& part, const Chunk& chunk) {
  if (chunk.file_offset > part.bytes.size() || part.bytes.size() - chunk.file_offset < chunk.size) {
    return std::nullopt;
  }
  return std::span<const std::byte>(part.bytes).subspan(chunk.file_offset, chunk.size);
}

std::optional<BitmapTable> read_table(const Overlay& overlay, const Part& part, std::size_t row) {
  const LoaderEntry& entry = part.loaders.entries[row];
  const auto& chunks = part.loaders.extra[row];
  if (chunks.size() != 2) return std::nullopt;

  const auto header = overlay.space().bytes_at(entry.data, kRowDataSize);
  if (!header) return std::nullopt;
  const std::uint32_t count = u32_at(*header, 4);

  const auto descriptions = chunk_bytes(part, chunks[0]);
  const auto pixels = chunk_bytes(part, chunks[1]);
  if (!descriptions || !pixels) return std::nullopt;
  if (descriptions->size() != kPrefixSize + std::uint64_t{count} * kBitmapSize) return std::nullopt;

  BitmapTable table;
  table.bitmaps.reserve(count);
  std::size_t used = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::size_t at = kPrefixSize + std::size_t{i} * kBitmapSize;
    Bitmap bitmap;
    bitmap.format = u32_at(*descriptions, at);
    bitmap.width = u32_at(*descriptions, at + 4);
    bitmap.height = u32_at(*descriptions, at + 8);
    bitmap.levels = u32_at(*descriptions, at + 12);
    if (bitmap.width > kMaxSide || bitmap.height > kMaxSide) return std::nullopt;
    if (bitmap.levels == 0 || bitmap.levels > kMaxLevels) return std::nullopt;

    const auto size = bitmap_data_size(bitmap.format, bitmap.width, bitmap.height, bitmap.levels);
    if (!size || *size > pixels->size() - used) return std::nullopt;
    bitmap.data = pixels->subspan(used, static_cast<std::size_t>(*size));
    used += static_cast<std::size_t>(*size);
    table.bitmaps.push_back(bitmap);
  }
  if (used != pixels->size()) return std::nullopt;
  return table;
}

}  // namespace

std::optional<BitmapLayout> bitmap_layout(std::uint32_t format) noexcept {
  switch (format) {
    case 2: return BitmapLayout::Pixels32;
    case 18: return BitmapLayout::Blocks8;
    case 19: return BitmapLayout::Blocks16;
    default: return std::nullopt;
  }
}

std::optional<std::uint64_t> bitmap_data_size(std::uint32_t format, std::uint32_t width, std::uint32_t height,
                                              std::uint32_t levels) noexcept {
  const auto layout = bitmap_layout(format);
  if (!layout || levels > 32) return std::nullopt;

  std::uint64_t total = 0;
  for (std::uint32_t level = 0; level < levels; ++level) {
    const std::uint64_t w = std::max<std::uint32_t>(1, width >> level);
    const std::uint64_t h = std::max<std::uint32_t>(1, height >> level);
    switch (*layout) {
      case BitmapLayout::Pixels32: total += w * h * 4; break;
      case BitmapLayout::Blocks8: total += ((w + 3) / 4) * ((h + 3) / 4) * 8; break;
      case BitmapLayout::Blocks16: total += ((w + 3) / 4) * ((h + 3) / 4) * 16; break;
    }
  }
  return total;
}

std::vector<BitmapTableItem> read_bitmap_tables(const Overlay& overlay, const Part& part) {
  std::vector<BitmapTableItem> items;
  const auto& loaders = part.file.header.loaders;
  for (std::size_t row = 0; row < part.loaders.entries.size(); ++row) {
    const std::uint32_t type = part.loaders.entries[row].loader_type;
    if (type >= loaders.size() || loaders[type].tag != kTableTag) continue;
    items.push_back({row, read_table(overlay, part, row)});
  }
  return items;
}

}  // namespace rct3::ovl
