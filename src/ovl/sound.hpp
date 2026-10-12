// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound items: an 80-byte descriptor holding the PCM format and a pointer to the samples.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// Size of a sound descriptor in the file.
inline constexpr std::size_t kSoundDescriptorSize = 80;

struct SoundInfo {
  // Wave format. `format_tag` is 1 for uncompressed PCM.
  std::uint16_t format_tag = 0;
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t bytes_per_second = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits_per_sample = 0;

  /// Nine floating point values whose meaning is not known yet.
  std::array<float, 9> parameters{};
  std::uint32_t flag = 0;  ///< 0 or 1, meaning unknown

  std::uint32_t data_address = 0;  ///< Address of the samples in the shared address space
  std::uint32_t data_size = 0;     ///< Size of the samples in bytes
  /// The samples, or nothing when the data pointer does not resolve to `data_size` bytes of one block.
  std::optional<std::span<const std::byte>> samples;
};

struct SoundItem {
  std::string_view name;           ///< Symbol name without the tag
  std::optional<SoundInfo> info;   ///< Nothing when the descriptor itself is unreadable
};

/// Sound items of one part of an overlay: pointer symbols tagged "snd".
[[nodiscard]] std::vector<SoundItem> read_sounds(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
