// SPDX-License-Identifier: GPL-3.0-or-later
//
// Advanced Systems Format (ASF), the container of Windows Media Audio and Video files, as far as reading one
// audio stream and one video stream needs it.
//
// The file starts with a header object holding smaller objects: the file's properties (the size of its
// data packets, how many there are, how long it plays) and each stream's properties (for audio, a
// WAVEFORMATEX: codec tag, channels, rate, bytes per second, block size and codec bytes). A data object
// follows with packets all of the same size.
//
// Each packet carries payloads, pieces of a stream's media objects: a payload names its stream, the object
// and where in the object it goes. An audio object, once whole, is a run of codec blocks of `block_align`
// bytes. AsfAudioBlocks puts the pieces together and hands out the blocks in order.
//
// A video stream's properties hold the picture's size and a BITMAPINFOHEADER: its codec's four letters and
// codec bytes. Each video object is one compressed frame; its payloads say when it shows (in milliseconds,
// after the file's preroll) and whether it is a key frame. AsfObjects puts them together whole.
//
// Audio spread over several packets to survive losses (a "span" above 1) is not read.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::audio
{

    struct AsfAudioFormat
    {
        std::uint16_t codec = 0; ///< WAVEFORMATEX format tag
        std::uint16_t channels = 0;
        std::uint32_t sample_rate = 0;
        std::uint32_t bytes_per_second = 0;
        std::uint16_t block_align = 0;
        std::uint16_t bits_per_sample = 0;
        std::vector<std::byte> extra; ///< Codec bytes after the WAVEFORMATEX
    };

    struct AsfVideoFormat
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t codec = 0;      ///< BITMAPINFOHEADER compression: four letters, the first in the low byte
        std::vector<std::byte> extra; ///< Codec bytes after the BITMAPINFOHEADER
    };

    struct AsfHeader
    {
        AsfAudioFormat audio;                ///< The first audio stream
        std::uint8_t stream = 0;             ///< Its number
        std::optional<AsfVideoFormat> video; ///< The first video stream, if any
        std::uint8_t video_stream = 0;       ///< Its number
        std::uint32_t preroll = 0;           ///< Milliseconds that object times run ahead of the playing time
        std::uint32_t packet_size = 0;       ///< Bytes in each data packet
        std::uint64_t packet_count = 0;      ///< 0 when the file did not say
        std::uint64_t first_packet = 0;      ///< Where the first data packet starts in the file
        double seconds = 0;                  ///< How long the file plays
    };

    /// Reads the header from the start of a file; `head` must hold the whole header object and the data
    /// object's own header. asf_header_size() says how much that is.
    [[nodiscard]] std::expected<AsfHeader, std::string> read_asf_header(std::span<const std::byte> head);

    /// The bytes to read for read_asf_header, from the first 30 bytes of the file.
    [[nodiscard]] std::expected<std::size_t, std::string> asf_header_size(std::span<const std::byte> start);

    /// One piece of a media object.
    struct AsfPayload
    {
        std::uint8_t stream = 0;
        std::uint32_t object = 0;      ///< Media object number
        std::uint32_t offset = 0;      ///< Where the piece goes in the object
        std::uint32_t object_size = 0; ///< Bytes in the whole object
        std::span<const std::byte> data;
        std::uint32_t time = 0; ///< When the object plays, in milliseconds with the preroll
        bool key = false;       ///< The object is a key frame
    };

    /// The payloads of one data packet, in order. They point into `packet`.
    [[nodiscard]] std::expected<std::vector<AsfPayload>, std::string> read_asf_packet(std::span<const std::byte> packet);

    /// Puts one stream's media objects together and splits them into codec blocks.
    class AsfAudioBlocks
    {
    public:
        AsfAudioBlocks(std::uint8_t stream, std::uint16_t block_align);

        /// Payloads of other streams are ignored. An object left unfinished when another begins is dropped.
        void add(const AsfPayload& payload);
        [[nodiscard]] bool empty() const
        {
            return blocks_.empty();
        }
        /// The oldest block; empty() must be false.
        [[nodiscard]] std::vector<std::byte> take();
        /// Forgets everything, to read from another place in the file.
        void clear();

    private:
        std::uint8_t stream_ = 0;
        std::uint16_t block_align_ = 0;
        std::uint32_t object_ = 0;
        std::vector<std::byte> pieces_;
        std::size_t filled_ = 0;
        bool open_ = false;
        std::deque<std::vector<std::byte>> blocks_;
    };

    /// One stream's media objects, whole: for video, its frames.
    struct AsfObject
    {
        std::uint32_t time = 0; ///< In milliseconds with the preroll
        bool key = false;
        std::vector<std::byte> data;
    };

    class AsfObjects
    {
    public:
        explicit AsfObjects(std::uint8_t stream);

        /// Payloads of other streams are ignored. An object left unfinished when another begins is dropped.
        void add(const AsfPayload& payload);
        [[nodiscard]] bool empty() const
        {
            return done_.empty();
        }
        /// The oldest object; empty() must be false.
        [[nodiscard]] AsfObject take();

    private:
        std::uint8_t stream_ = 0;
        std::uint32_t number_ = 0;
        AsfObject open_object_;
        std::size_t filled_ = 0;
        bool open_ = false;
        std::deque<AsfObject> done_;
    };

} // namespace rct3::audio
