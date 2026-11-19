// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound items: an 80-byte descriptor holding the PCM format and a pointer to the samples.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Size of a sound descriptor in the file.
    inline constexpr std::size_t kSoundDescriptorSize = 80;

    struct SoundInfo
    {
        // Wave format. `format_tag` is 1 for uncompressed PCM.
        std::uint16_t format_tag = 0;
        std::uint16_t channels = 0;
        std::uint32_t sample_rate = 0;
        std::uint32_t bytes_per_second = 0;
        std::uint16_t block_align = 0;
        std::uint16_t bits_per_sample = 0;

        /// Volume envelope and distance fade. Times are in seconds, levels are fractions of full volume.
        /// The two curve fields choose how a ramp is shaped: 0 is the usual value in every file seen.
        std::uint32_t fade_in_curve = 0; ///< 0 when the fade in is a plain ramp, anything else selects a curve
        float fade_in_seconds = 0;       ///< Time over which a started sound ramps up from silence
        float rise_seconds = 0;          ///< Time of the second ramp, up to `peak_level`
        float peak_level = 0;            ///< Volume the second ramp reaches, 1 in every file seen
        std::uint32_t decay_curve = 0;   ///< Curve of the ramp after the peak
        float hold_seconds = 0;          ///< Time spent at the peak before the tail ramp. Huge (1.5 million) in every file seen, i.e. until stopped
        std::uint32_t tail_curve = 0;    ///< Curve of the fade out
        float fade_out_seconds = 0;      ///< Time over which a stopped sound ramps down to silence
        /// A positioned sound is at full volume within `near_distance` of the listener, fades out linearly
        /// to silence at `far_distance`. World units (metres).
        float near_distance = 0;
        float far_distance = 0;
        bool loops = false; ///< Play again from the start when the samples end

        std::uint32_t data_address = 0; ///< Address of the samples in the shared address space
        std::uint32_t data_size = 0;    ///< Size of the samples in bytes
        /// The samples, or nothing when the data pointer does not resolve to `data_size` bytes of one block.
        std::optional<std::span<const std::byte>> samples;
    };

    struct SoundItem
    {
        std::string_view name;         ///< Symbol name without the tag
        std::optional<SoundInfo> info; ///< Nothing when the descriptor itself is unreadable
    };

    /// Sound items of one part of an overlay: pointer symbols tagged "snd".
    [[nodiscard]] std::vector<SoundItem> read_sounds(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
