// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Windows Media Audio (versions 1 and 2) packet decoder: the packets of one stream in, float samples out.
// WmaStream reads the ASF file and feeds it. Which decoder is built depends on the platform: a file ending in
// _ffmpeg.cpp everywhere but Windows, one ending in _windows.cpp on Windows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace rct3::audio::wma
{

    /// The stream's format, as the ASF header gives it.
    struct Format
    {
        int version = 0; ///< 1 or 2
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        std::uint32_t bit_rate = 0;       ///< Bits per second
        std::uint16_t block_align = 0;    ///< Bytes in each packet
        std::span<const std::byte> extra; ///< The codec bytes after the wave format
    };

    class Decoder
    {
    public:
        /// None for a format the decoder cannot take.
        [[nodiscard]] static std::optional<Decoder> open(const Format& format);

        Decoder(Decoder&&) noexcept;
        Decoder& operator=(Decoder&&) noexcept;
        ~Decoder();

        /// Decodes one packet of block_align bytes and returns the samples it gives for each channel, one
        /// plane a channel in `planes` (valid until the next call). 0 when the packet gives none yet, or cannot
        /// be read. An empty packet gives the last samples still held back.
        int decode(std::span<const std::byte> packet, const float* const*& planes);

        /// Samples for each channel at the start of what decode() gives that come before the sound.
        [[nodiscard]] int delay() const;

        /// Forgets the data carried between packets, to start the stream again.
        void reset();

    private:
        struct State;
        explicit Decoder(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };

} // namespace rct3::audio::wma
