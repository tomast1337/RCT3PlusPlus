// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Windows Media Video 9 movie (the first release's startup logos), its pictures read as they come due. The
// file is an ASF file (audio/asf_file.hpp), held whole: the movies are small. Its frames are decoded by
// decoder.hpp and turned from Y, U and V into RGBA here. The sound
// beside the pictures is the file's audio stream, which audio::wma::WmaStream (or MusicPlayer) plays from the same
// file.
#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rct3::video::wmv
{

    /// 4:2:0 Y, U and V (U and V at half the width and height, rounded up), limited range with the
    /// standard-definition colours, to RGBA: `width` by `height` pixels, four bytes each, row by row.
    void yuv_to_rgba(u32 width, u32 height, std::span<const u8> y, usize y_stride, std::span<const u8> u, usize u_stride, std::span<const u8> v, usize v_stride, std::span<u8> rgba);

    class WmvMovie
    {
    public:
        [[nodiscard]] static std::expected<WmvMovie, std::string> open(const std::filesystem::path& path);
        [[nodiscard]] static std::expected<WmvMovie, std::string> open(std::vector<std::byte> file);

        WmvMovie(WmvMovie&&) noexcept;
        WmvMovie& operator=(WmvMovie&&) noexcept;
        ~WmvMovie();

        [[nodiscard]] u32 width() const;
        [[nodiscard]] u32 height() const;
        /// How long the file says it plays.
        [[nodiscard]] f64 seconds() const;

        /// Decodes the frames due by `seconds` into the movie. True when the picture changed.
        bool advance(f64 seconds);
        /// The picture last decoded, width() by height() RGBA; empty before the first.
        [[nodiscard]] std::span<const u8> picture() const;
        /// No frames are left.
        [[nodiscard]] bool finished() const;

    private:
        struct State;
        explicit WmvMovie(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };

} // namespace rct3::video::wmv
