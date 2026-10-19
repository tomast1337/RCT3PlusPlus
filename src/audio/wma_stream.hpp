// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows Media Audio (versions 1 and 2) read from an ASF file a little at a time, for music too long to
// hold decoded. The decoding is FFmpeg's WMA decoder, built in (third_party/ffmpeg); the file is read
// with asf_file.hpp.
//
// The decoder starts two frames late (its windows overlap); those samples are dropped, so the first sample
// read is the first of the sound. A block the decoder cannot read is skipped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace rct3::audio
{

    class WmaStream
    {
    public:
        [[nodiscard]] static std::expected<WmaStream, std::string> open(const std::filesystem::path& path);

        WmaStream(WmaStream&&) noexcept;
        WmaStream& operator=(WmaStream&&) noexcept;
        ~WmaStream();

        [[nodiscard]] std::uint32_t sample_rate() const;
        [[nodiscard]] std::uint16_t channels() const;
        /// How long the file says it plays.
        [[nodiscard]] double seconds() const;

        /// Fills `out` with whole frames, channels interleaved, and returns how many; fewer than fit only at
        /// the end of the sound. An error is a file that cannot be read any more.
        [[nodiscard]] std::expected<std::size_t, std::string> read(std::span<float> out);

        /// Back to the start of the sound.
        [[nodiscard]] std::expected<void, std::string> rewind();

    private:
        struct State;
        explicit WmaStream(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };

} // namespace rct3::audio
