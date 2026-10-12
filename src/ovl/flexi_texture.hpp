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

  /// 256 palette entries, 4 bytes each, stored as blue, green, red and a fourth byte that is not used as
  /// alpha (it is zero in most textures). Decoded frames look right only in this order. Transparency comes
  /// from the separate alpha plane.
  std::span<const std::byte> palette;
  std::span<const std::byte> pixels;  ///< width * height palette indices
  /// width * height alpha values (0 transparent, 255 opaque), absent when the frame has no alpha plane.
  /// Many are strictly 0 or 255 cut-outs, some are smooth masks.
  std::optional<std::span<const std::byte>> alpha;
};

struct FlexiTexture {
  std::uint32_t log2_size = 0;  ///< Texture is square: width == height == 1 << log2_size
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  /// 0 for single-frame textures, non-zero for all multi-frame ones. It is the playback speed in frames of
  /// the sequence per second: textures that differ only by a "slow" variant carry half the value. Seen
  /// values are 3 to 25.
  std::uint32_t animation = 0;
  /// Bit mask, 0..7: which of three recolourable colour slots the texture uses. The art marks each slot
  /// with a solid colour region: bit 0 blue, bit 1 magenta, bit 2 yellow.
  std::uint32_t flags = 0;

  /// Order in which frames are shown, repeating entries are allowed and the list can be longer than
  /// `frames`. Each entry is an index into `frames`.
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
