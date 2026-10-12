// SPDX-License-Identifier: GPL-3.0-or-later
//
// Turning texture pixel data into plain 8-bit RGBA images. These are pure functions of the structures the
// readers produce, they do not touch the overlay.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ovl/bitmap_table.hpp"
#include "ovl/flexi_texture.hpp"
#include "ovl/flic.hpp"

namespace rct3::ovl {

/// Largest image side the decoders accept. Shipped bitmaps are at most 2048 wide.
inline constexpr std::uint32_t kMaxImageSide = 4096;

/// A decoded image: `width * height` pixels in row order, top row first, 4 bytes each: red, green, blue,
/// alpha.
struct RgbaImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

/// One frame of a flexi texture. Each palette entry is stored as blue, green, red and an unused byte. The
/// result is opaque unless the frame has an alpha plane, which supplies the alpha channel. Nothing when the
/// frame's spans are shorter than its size requires or a side is zero or above `kMaxImageSide`.
[[nodiscard]] std::optional<RgbaImage> decode_flexi_frame(const FlexiFrame& frame);

/// A single mip level of `format` (a bitmap format value, see `bitmap_layout`) stored in `data`, which
/// must hold at least the level's bytes. Raw pixels are stored as blue, green, red, alpha. Format 18 is
/// the 4x4 block format with a colour pair and 1-bit alpha, 19 the one with explicit 4-bit alpha. Nothing
/// for an unknown format, a bad size or short data.
[[nodiscard]] std::optional<RgbaImage> decode_pixels(std::uint32_t format, std::uint32_t width,
                                                     std::uint32_t height, std::span<const std::byte> data);

/// All mip levels of a bitmap, largest first. Empty when any level cannot be decoded.
[[nodiscard]] std::vector<RgbaImage> decode_bitmap(const Bitmap& bitmap);

/// The image of a revision 2 flic: every level stored in its chunk, largest first. Block formats store no
/// level smaller than 4x4, so the result can have fewer levels than the header states. Empty when the flic
/// has no image or its data is too short.
[[nodiscard]] std::vector<RgbaImage> decode_flic_image(const Flic& flic);

/// An uncompressed 32-bit true colour TGA file of the image, for inspecting results. Empty for an image
/// that has no pixels or whose buffer does not match its size.
[[nodiscard]] std::vector<std::byte> encode_tga(const RgbaImage& image);

}  // namespace rct3::ovl
