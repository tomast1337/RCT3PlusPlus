// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Windows Media Video 9 (WMV3) frame decoder: the frames of one stream in, Y, U and V pictures out. WmvMovie
// reads the ASF file, feeds it and turns its pictures into RGBA. Which decoder is built depends on the
// platform: a file ending in _ffmpeg.cpp everywhere but Windows, one ending in _windows.cpp on Windows.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace rct3::video::wmv
{

    /// The stream's format, as the ASF header gives it.
    struct Format
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::span<const std::byte> extra; ///< The codec bytes after the bitmap header
    };

    /// A decoded picture: 8-bit 4:2:0 Y, U and V planes (U and V at half the width and height, rounded up),
    /// each `strides[i]` bytes a row, holding every row of the picture.
    struct Picture
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::array<std::span<const std::uint8_t>, 3> planes { };
        std::array<std::size_t, 3> strides { };
    };

    class Decoder
    {
    public:
        /// None for a format the decoder cannot take.
        [[nodiscard]] static std::optional<Decoder> open(const Format& format);

        Decoder(Decoder&&) noexcept;
        Decoder& operator=(Decoder&&) noexcept;
        ~Decoder();

        /// Decodes one whole frame of the stream. The picture (valid until the next call) when it gives one;
        /// none when it gives none yet, or cannot be read. An empty frame gives the last picture still held
        /// back.
        [[nodiscard]] std::optional<Picture> decode(std::span<const std::byte> frame);

    private:
        struct State;
        explicit Decoder(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };

} // namespace rct3::video::wmv
