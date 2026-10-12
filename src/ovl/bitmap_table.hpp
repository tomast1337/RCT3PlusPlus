// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bitmap tables: the pixel data of the textures in a file, stored once as a list of bitmaps with their
// mip chains. A table has no symbol name, it is attached to an anonymous loader row.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// How the pixels of a bitmap format are stored, deduced from the sizes of the data in files.
enum class BitmapLayout {
  Pixels32,  ///< 4 bytes per pixel
  Blocks8,   ///< 4x4 pixel blocks of 8 bytes
  Blocks16,  ///< 4x4 pixel blocks of 16 bytes
};

/// Layout of a stored format value: 2 is 32 bits per pixel, 18 and 19 are 4x4 block formats of 8 and 16
/// bytes. Nothing for any other value, which does not occur in shipped files.
[[nodiscard]] std::optional<BitmapLayout> bitmap_layout(std::uint32_t format) noexcept;

/// Bytes of all `levels` mip levels of a bitmap, stored from the largest level down with each level
/// halving both sides down to 1. Nothing for an unknown format.
[[nodiscard]] std::optional<std::uint64_t> bitmap_data_size(std::uint32_t format, std::uint32_t width,
                                                            std::uint32_t height, std::uint32_t levels) noexcept;

struct Bitmap {
  std::uint32_t format = 0;
  std::uint32_t width = 0;   ///< Power of two, at most 2048
  std::uint32_t height = 0;  ///< Power of two, at most 2048
  std::uint32_t levels = 0;  ///< Mip levels, 1 to 11
  std::span<const std::byte> data;  ///< All levels in order, `bitmap_data_size` bytes
};

struct BitmapTable {
  std::vector<Bitmap> bitmaps;
};

struct BitmapTableItem {
  std::size_t row = 0;                ///< Index of the loader row the table belongs to
  std::optional<BitmapTable> table;   ///< Nothing when the row's data or chunks are unreadable
};

/// Bitmap tables of one part of an overlay: loader rows of type "btbl". Spans in the result point into
/// the part's bytes.
[[nodiscard]] std::vector<BitmapTableItem> read_bitmap_tables(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
