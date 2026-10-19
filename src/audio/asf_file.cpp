// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/asf_file.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace rct3::audio
{

    namespace
    {

        using Guid = std::array<std::uint8_t, 16>;

        // As stored: the first three fields little-endian.
        constexpr Guid kHeaderObject = { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
        constexpr Guid kDataObject = { 0x36, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
        constexpr Guid kFileProperties = { 0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
        constexpr Guid kStreamProperties = { 0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11, 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 };
        constexpr Guid kAudioMedia = { 0x40, 0x9E, 0x69, 0xF8, 0x4D, 0x5B, 0xCF, 0x11, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B };
        constexpr Guid kVideoMedia = { 0xC0, 0xEF, 0x19, 0xBC, 0x4D, 0x5B, 0xCF, 0x11, 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B };
        constexpr Guid kAudioSpread = { 0x50, 0xCD, 0xC3, 0xBF, 0x8F, 0x61, 0xCF, 0x11, 0x8B, 0xB2, 0x00, 0xAA, 0x00, 0xB4, 0xE2, 0x20 };

        constexpr std::size_t kObjectHeader = 24;     ///< GUID and size
        constexpr std::size_t kDataObjectHeader = 50; ///< GUID, size, file id, packet count, reserved
        constexpr std::size_t kMaxHeader = 16u << 20;
        // Far more than an audio object needs; bounds what a bad object length makes us hold.
        constexpr std::uint32_t kMaxObject = 1u << 20;
        constexpr std::size_t kBitmapHeader = 40; ///< BITMAPINFOHEADER without the codec bytes
        constexpr std::uint32_t kMaxPicture = 4096;

        /// Reads little-endian values in order; past the end everything fails and reads as zero.
        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> bytes)
                : bytes_(bytes)
            {
            }

            [[nodiscard]] bool ok() const
            {
                return ok_;
            }
            [[nodiscard]] std::size_t position() const
            {
                return at_;
            }
            [[nodiscard]] std::size_t left() const
            {
                return ok_ ? bytes_.size() - at_ : 0;
            }

            std::span<const std::byte> bytes(std::size_t n)
            {
                if (!ok_ || n > bytes_.size() - at_)
                {
                    ok_ = false;
                    return { };
                }
                const auto out = bytes_.subspan(at_, n);
                at_ += n;
                return out;
            }
            void skip(std::size_t n)
            {
                (void)bytes(n);
            }
            std::uint64_t value(std::size_t n)
            {
                std::uint64_t v = 0;
                const auto b = bytes(n);
                for (std::size_t i = b.size(); i-- > 0;)
                    v = (v << 8) | std::to_integer<std::uint64_t>(b[i]);
                return v;
            }
            std::uint8_t u8()
            {
                return static_cast<std::uint8_t>(value(1));
            }
            std::uint16_t u16()
            {
                return static_cast<std::uint16_t>(value(2));
            }
            std::uint32_t u32()
            {
                return static_cast<std::uint32_t>(value(4));
            }
            std::uint64_t u64()
            {
                return value(8);
            }
            Guid guid()
            {
                Guid g { };
                const auto b = bytes(16);
                if (!b.empty())
                    std::memcpy(g.data(), b.data(), g.size());
                return g;
            }
            /// A value whose size is given by a 2-bit length type: none, 1, 2 or 4 bytes.
            std::uint32_t sized(unsigned type)
            {
                static constexpr std::array<std::size_t, 4> kSizes = { 0, 1, 2, 4 };
                return static_cast<std::uint32_t>(value(kSizes.at(type & 3)));
            }

        private:
            std::span<const std::byte> bytes_;
            std::size_t at_ = 0;
            bool ok_ = true;
        };

        struct StreamInfo
        {
            std::uint8_t number = 0;
            AsfAudioFormat format;
            std::optional<AsfVideoFormat> video; ///< In place of `format`, for a video stream
        };

        std::expected<AsfVideoFormat, std::string> read_video(Reader w)
        {
            AsfVideoFormat f;
            f.width = w.u32();
            f.height = w.u32();
            w.skip(1); // Reserved
            const std::uint16_t format_size = w.u16();
            Reader b(w.bytes(format_size));
            const std::uint32_t header_size = b.u32();
            b.skip(4 + 4 + 2 + 2); // Width, height, planes, bits per pixel
            f.codec = b.u32();
            b.skip(4 + 4 + 4 + 4 + 4); // Image size, pixels per metre twice, colours used and important
            if (!w.ok() || !b.ok() || header_size < kBitmapHeader || header_size > format_size)
                return std::unexpected("cut short video format");
            if (f.width == 0 || f.height == 0 || f.width > kMaxPicture || f.height > kMaxPicture)
                return std::unexpected("bad video picture size");
            const auto extra = b.bytes(header_size - kBitmapHeader);
            f.extra.assign(extra.begin(), extra.end());
            return f;
        }

        std::expected<std::optional<StreamInfo>, std::string> read_stream(Reader r)
        {
            const Guid type = r.guid();
            const Guid correction = r.guid();
            r.skip(8); // Time offset
            const std::uint32_t specific_size = r.u32();
            const std::uint32_t correction_size = r.u32();
            const std::uint16_t flags = r.u16();
            r.skip(4);
            if (!r.ok())
                return std::unexpected("cut short stream properties");
            if (type == kVideoMedia)
            {
                auto video = read_video(Reader(r.bytes(specific_size)));
                if (!r.ok())
                    return std::unexpected("cut short video stream properties");
                if (!video)
                    return std::unexpected(video.error());
                StreamInfo s;
                s.number = static_cast<std::uint8_t>(flags & 0x7F);
                s.video = std::move(*video);
                return s;
            }
            if (type != kAudioMedia)
                return std::nullopt;
            Reader w(r.bytes(specific_size));
            const auto correction_data = r.bytes(correction_size);
            if (!r.ok())
                return std::unexpected("cut short audio stream properties");
            StreamInfo s;
            s.number = static_cast<std::uint8_t>(flags & 0x7F);
            AsfAudioFormat& f = s.format;
            f.codec = w.u16();
            f.channels = w.u16();
            f.sample_rate = w.u32();
            f.bytes_per_second = w.u32();
            f.block_align = w.u16();
            f.bits_per_sample = w.u16();
            if (!w.ok())
                return std::unexpected("cut short audio format");
            if (w.left() >= 2)
            {
                const std::uint16_t extra = w.u16();
                const auto bytes = w.bytes(extra);
                if (!w.ok())
                    return std::unexpected("cut short codec bytes");
                f.extra.assign(bytes.begin(), bytes.end());
            }
            if (correction == kAudioSpread && correction_data.size() >= 1 && std::to_integer<std::uint8_t>(correction_data[0]) > 1)
                return std::unexpected("audio spread over several packets is not read");
            return s;
        }

    } // namespace

    std::expected<std::size_t, std::string> asf_header_size(std::span<const std::byte> start)
    {
        Reader r(start);
        if (r.guid() != kHeaderObject || !r.ok())
            return std::unexpected("not an ASF file");
        const std::uint64_t size = r.u64();
        if (size < 30 || size > kMaxHeader)
            return std::unexpected("bad ASF header size");
        return static_cast<std::size_t>(size) + kDataObjectHeader;
    }

    std::expected<AsfHeader, std::string> read_asf_header(std::span<const std::byte> head)
    {
        const auto size = asf_header_size(head);
        if (!size)
            return std::unexpected(size.error());
        if (head.size() < *size)
            return std::unexpected("cut short ASF header");
        Reader r(head);
        r.skip(16);
        const std::uint64_t header_end = r.u64();
        const std::uint32_t objects = r.u32();
        r.skip(2);

        AsfHeader h;
        bool have_file = false;
        bool have_audio = false;
        for (std::uint32_t i = 0; i < objects && r.ok() && r.position() < header_end; ++i)
        {
            const std::size_t start = r.position();
            const Guid id = r.guid();
            const std::uint64_t object_size = r.u64();
            if (!r.ok() || object_size < kObjectHeader || object_size > header_end - start)
                return std::unexpected("bad object in the ASF header");
            Reader body(r.bytes(static_cast<std::size_t>(object_size - kObjectHeader)));
            if (id == kFileProperties)
            {
                body.skip(16 + 8 + 8); // File id, file size, creation date
                h.packet_count = body.u64();
                const std::uint64_t play = body.u64();    // In 100 ns
                body.skip(8);                             // Send duration
                const std::uint64_t preroll = body.u64(); // In ms
                body.skip(4);                             // Flags
                const std::uint32_t min_packet = body.u32();
                const std::uint32_t max_packet = body.u32();
                if (!body.ok())
                    return std::unexpected("cut short file properties");
                if (min_packet != max_packet || min_packet == 0)
                    return std::unexpected("ASF packets of different sizes are not read");
                h.packet_size = min_packet;
                h.preroll = static_cast<std::uint32_t>(std::min<std::uint64_t>(preroll, 0xFFFFFFFF));
                h.seconds = std::max(0.0, static_cast<double>(play) / 1e7 - static_cast<double>(preroll) / 1e3);
                have_file = true;
            }
            else if (id == kStreamProperties)
            {
                auto stream = read_stream(body);
                if (!stream)
                    return std::unexpected(stream.error());
                if (*stream && (*stream)->video)
                {
                    if (!h.video)
                    {
                        h.video_stream = (*stream)->number;
                        h.video = std::move((*stream)->video);
                    }
                }
                else if (*stream && !have_audio)
                {
                    h.stream = (*stream)->number;
                    h.audio = std::move((*stream)->format);
                    have_audio = true;
                }
            }
        }
        if (!have_file)
            return std::unexpected("no file properties in the ASF header");
        if (!have_audio)
            return std::unexpected("no audio stream in the ASF file");
        if (h.audio.block_align == 0)
            return std::unexpected("audio stream without a block size");

        Reader data(head.subspan(static_cast<std::size_t>(header_end)));
        if (data.guid() != kDataObject)
            return std::unexpected("no data object after the ASF header");
        h.first_packet = header_end + kDataObjectHeader;
        return h;
    }

    std::expected<std::vector<AsfPayload>, std::string> read_asf_packet(std::span<const std::byte> packet)
    {
        Reader r(packet);
        std::uint8_t flags = r.u8();
        if (flags & 0x80) // Error correction data first
        {
            if (flags & 0x10)
                return std::unexpected("unknown ASF error correction");
            r.skip(flags & 0x0F);
            flags = r.u8();
        }
        const std::uint8_t properties = r.u8();
        const bool multiple = flags & 0x01;
        std::uint32_t length = r.sized(flags >> 5);
        r.sized(flags >> 1); // Sequence
        const std::uint32_t padding = r.sized(flags >> 3);
        r.skip(4 + 2); // Send time, duration
        if (!r.ok())
            return std::unexpected("cut short ASF packet");
        if (length == 0 || length > packet.size())
            length = static_cast<std::uint32_t>(packet.size());
        if (padding > length || r.position() > length - padding)
            return std::unexpected("bad ASF packet padding");
        const std::size_t end = length - padding;

        std::uint32_t count = 1;
        unsigned length_type = 0;
        if (multiple)
        {
            const std::uint8_t payload_flags = r.u8();
            count = payload_flags & 0x3F;
            length_type = payload_flags >> 6;
        }
        std::vector<AsfPayload> out;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            AsfPayload p;
            const std::uint8_t stream = r.u8();
            p.stream = stream & 0x7F;
            p.key = (stream & 0x80) != 0;
            p.object = r.sized(properties >> 4);
            p.offset = r.sized(properties >> 2);
            const std::uint32_t replicated = r.sized(properties);
            if (replicated == 1)
                return std::unexpected("compressed ASF payloads are not read");
            const auto extra = r.bytes(replicated);
            if (replicated >= 8)
            {
                Reader e(extra);
                p.object_size = e.u32();
                p.time = e.u32();
            }
            const std::uint32_t size = multiple ? r.sized(length_type) : static_cast<std::uint32_t>(r.position() <= end ? end - r.position() : 0);
            if (!r.ok() || r.position() > end || size > end - r.position())
                return std::unexpected("ASF payload past the end of its packet");
            p.data = r.bytes(size);
            if (p.object_size == 0)
                p.object_size = p.offset + size;
            out.push_back(p);
        }
        return out;
    }

    AsfAudioBlocks::AsfAudioBlocks(std::uint8_t stream, std::uint16_t block_align)
        : stream_(stream)
        , block_align_(block_align)
    {
    }

    void AsfAudioBlocks::add(const AsfPayload& payload)
    {
        if (payload.stream != stream_ || block_align_ == 0)
            return;
        if (!open_ || payload.object != object_)
        {
            open_ = payload.offset == 0 && payload.object_size <= kMaxObject;
            if (!open_)
                return; // The middle of an object whose start was missed, or one too big
            object_ = payload.object;
            pieces_.assign(payload.object_size, std::byte { 0 });
            filled_ = 0;
        }
        if (payload.offset != filled_ || payload.data.size() > pieces_.size() - filled_)
        {
            open_ = false; // A piece out of order: the object is lost
            return;
        }
        std::ranges::copy(payload.data, pieces_.begin() + static_cast<std::ptrdiff_t>(filled_));
        filled_ += payload.data.size();
        if (filled_ < pieces_.size())
            return;
        for (std::size_t at = 0; at + block_align_ <= pieces_.size(); at += block_align_)
            blocks_.emplace_back(pieces_.begin() + static_cast<std::ptrdiff_t>(at), pieces_.begin() + static_cast<std::ptrdiff_t>(at + block_align_));
        open_ = false;
    }

    std::vector<std::byte> AsfAudioBlocks::take()
    {
        auto out = std::move(blocks_.front());
        blocks_.pop_front();
        return out;
    }

    AsfObjects::AsfObjects(std::uint8_t stream)
        : stream_(stream)
    {
    }

    void AsfObjects::add(const AsfPayload& payload)
    {
        if (payload.stream != stream_)
            return;
        if (!open_ || payload.object != number_)
        {
            open_ = payload.offset == 0 && payload.object_size > 0 && payload.object_size <= kMaxObject;
            if (!open_)
                return; // The middle of an object whose start was missed, or one too big
            number_ = payload.object;
            open_object_ = { payload.time, payload.key, std::vector<std::byte>(payload.object_size) };
            filled_ = 0;
        }
        std::vector<std::byte>& pieces = open_object_.data;
        if (payload.offset != filled_ || payload.data.size() > pieces.size() - filled_)
        {
            open_ = false; // A piece out of order: the object is lost
            return;
        }
        std::ranges::copy(payload.data, pieces.begin() + static_cast<std::ptrdiff_t>(filled_));
        filled_ += payload.data.size();
        if (filled_ < pieces.size())
            return;
        done_.push_back(std::move(open_object_));
        open_ = false;
    }

    AsfObject AsfObjects::take()
    {
        AsfObject out = std::move(done_.front());
        done_.pop_front();
        return out;
    }

    void AsfAudioBlocks::clear()
    {
        blocks_.clear();
        pieces_.clear();
        filled_ = 0;
        open_ = false;
    }

} // namespace rct3::audio
