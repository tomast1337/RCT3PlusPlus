// SPDX-License-Identifier: GPL-3.0-or-later
//
// Flexi textures: one or more 8-bit paletted frames, an optional alpha plane per frame and an
// animation sequence over the frames.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// Bytes of a frame's palette: 256 entries of 4 bytes.
inline constexpr std::size_t kFlexiPaletteSize = 1024;

struct FlexiFrame {
  std::uint32_t log2_size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t flags = 0;  ///< Same value as the texture's `flags` in all files seen

  /// 256 palette entries, 4 bytes each. Byte order appears to be blue, green, red, then a fourth byte
  /// that is zero in most textures. That order is inferred from colours, not confirmed.
  std::span<const std::byte> palette;
  std::span<const std::byte> pixels;  ///< width * height palette indices
  /// width * height alpha values, absent when the frame has no alpha plane.
  std::optional<std::span<const std::byte>> alpha;
};

struct FlexiTexture {
  std::uint32_t log2_size = 0;  ///< Texture is square: width == height == 1 << log2_size
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t animation = 0;  ///< 0 for single-frame textures, non-zero for all multi-frame ones
  std::uint32_t flags = 0;      ///< Bit mask, 0..7 in files seen; probably which colours are recolourable

  /// Order in which frames are shown. Each entry is an index into `frames`.
  std::vector<std::uint32_t> sequence;
  std::vector<FlexiFrame> frames;
};

struct FlexiTextureItem {
  std::string_view name;               ///< Symbol name without the tag
  std::optional<FlexiTexture> texture;  ///< Nothing when any part of the structure is unreadable
};

/// Flexi textures of one part of an overlay: pointer symbols tagged "ftx". Spans in the result point
/// into the overlay's bytes.
[[nodiscard]] std::vector<FlexiTextureItem> read_flexi_textures(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
