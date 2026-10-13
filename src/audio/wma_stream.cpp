// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/wma_stream.hpp"

#include "audio/asf_file.hpp"

#include <algorithm>
#include <ffmpeg_wma/rct3_wma.h>
#include <format>
#include <fstream>
#include <vector>

namespace rct3::audio
{

    namespace
    {

        constexpr std::uint16_t kWma1 = 0x160;
        constexpr std::uint16_t kWma2 = 0x161;

        struct CloseDecoder
        {
            void operator()(Rct3WmaDecoder* d) const
            {
                rct3_wma_close(d);
            }
        };

        // At most `size` bytes from `at`: fewer where the file ends, so a size the file claims never makes us
        // hold more than the file has.
        std::expected<std::vector<std::byte>, std::string> read_at(std::ifstream& file, std::uint64_t file_size, std::uint64_t at, std::size_t size)
        {
            std::vector<std::byte> out(at < file_size ? static_cast<std::size_t>(std::min<std::uint64_t>(size, file_size - at)) : 0);
            file.clear();
            file.seekg(static_cast<std::streamoff>(at));
            file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            out.resize(static_cast<std::size_t>(std::max<std::streamsize>(file.gcount(), 0)));
            if (file.bad())
                return std::unexpected("cannot read the file");
            return out;
        }

    } // namespace

    struct WmaStream::State
    {
        std::ifstream file;
        std::uint64_t file_size = 0;
        AsfHeader header;
        std::unique_ptr<Rct3WmaDecoder, CloseDecoder> decoder;
        AsfAudioBlocks blocks { 0, 0 };
        std::uint64_t next_packet = 0;
        bool flushed = false;
        int skip = 0;             ///< Samples per channel still to drop at the start
        std::vector<float> ready; ///< Decoded frames, interleaved
        std::size_t ready_at = 0; ///< In samples

        std::expected<void, std::string> start()
        {
            rct3_wma_reset(decoder.get());
            blocks.clear();
            next_packet = 0;
            flushed = false;
            skip = rct3_wma_delay(decoder.get());
            ready.clear();
            ready_at = 0;
            return { };
        }

        void take_samples(int count, const float* const* planes)
        {
            const int channels = header.audio.channels;
            const int from = std::min(skip, count);
            skip -= from;
            for (int i = from; i < count; ++i)
            {
                for (int ch = 0; ch < channels; ++ch)
                    ready.push_back(planes[ch][i]);
            }
        }

        /// Decodes more; false at the end of the sound.
        std::expected<bool, std::string> decode_more()
        {
            const float* const* planes = nullptr;
            if (!blocks.empty())
            {
                const auto block = blocks.take();
                const int n = rct3_wma_decode(decoder.get(), reinterpret_cast<const std::uint8_t*>(block.data()), static_cast<int>(block.size()), &planes);
                if (n > 0)
                    take_samples(n, planes);
                return true;
            }
            const bool more_packets = header.packet_count == 0 || next_packet < header.packet_count;
            if (more_packets)
            {
                const std::uint64_t at = header.first_packet + next_packet * header.packet_size;
                auto packet = read_at(file, file_size, at, header.packet_size);
                if (!packet)
                    return std::unexpected(packet.error());
                if (packet->size() == header.packet_size)
                {
                    ++next_packet;
                    if (const auto payloads = read_asf_packet(*packet))
                    {
                        for (const auto& p : *payloads)
                            blocks.add(p);
                    }
                    return true;
                }
                header.packet_count = next_packet; // The file ends early
            }
            if (flushed)
                return false;
            flushed = true;
            const int n = rct3_wma_decode(decoder.get(), nullptr, 0, &planes);
            if (n > 0)
                take_samples(n, planes);
            return true;
        }
    };

    WmaStream::WmaStream(std::unique_ptr<State> state)
        : state_(std::move(state))
    {
    }
    WmaStream::WmaStream(WmaStream&&) noexcept = default;
    WmaStream& WmaStream::operator=(WmaStream&&) noexcept = default;
    WmaStream::~WmaStream() = default;

    std::expected<WmaStream, std::string> WmaStream::open(const std::filesystem::path& path)
    {
        auto s = std::make_unique<State>();
        s->file.open(path, std::ios::binary);
        if (!s->file)
            return std::unexpected(std::format("{}: cannot open", path.string()));
        const auto fail = [&](const std::string& why) { return std::unexpected(std::format("{}: {}", path.string(), why)); };
        std::error_code error;
        s->file_size = std::filesystem::file_size(path, error);
        if (error)
            return fail(error.message());

        auto start = read_at(s->file, s->file_size, 0, 30);
        if (!start)
            return fail(start.error());
        const auto size = asf_header_size(*start);
        if (!size)
            return fail(size.error());
        auto head = read_at(s->file, s->file_size, 0, *size);
        if (!head)
            return fail(head.error());
        auto header = read_asf_header(*head);
        if (!header)
            return fail(header.error());
        s->header = std::move(*header);

        const AsfAudioFormat& f = s->header.audio;
        if (f.codec != kWma1 && f.codec != kWma2)
            return fail(std::format("not Windows Media Audio 1 or 2 (codec {:#x})", f.codec));
        const Rct3WmaFormat format {
            .version = f.codec == kWma1 ? 1 : 2,
            .sample_rate = static_cast<int>(f.sample_rate),
            .channels = f.channels,
            .bit_rate = static_cast<int>(std::min<std::uint64_t>(f.bytes_per_second * 8ull, 0x7FFFFFFF)),
            .block_align = f.block_align,
            .extra = reinterpret_cast<const std::uint8_t*>(f.extra.data()),
            .extra_size = static_cast<int>(f.extra.size()),
        };
        s->decoder.reset(rct3_wma_open(&format));
        if (!s->decoder)
            return fail(std::format("the decoder cannot take this stream ({} Hz, {} channels, {} bytes a second)", f.sample_rate, f.channels, f.bytes_per_second));
        s->blocks = AsfAudioBlocks(s->header.stream, f.block_align);
        if (auto ok = s->start(); !ok)
            return fail(ok.error());
        return WmaStream(std::move(s));
    }

    std::uint32_t WmaStream::sample_rate() const
    {
        return state_->header.audio.sample_rate;
    }

    std::uint16_t WmaStream::channels() const
    {
        return state_->header.audio.channels;
    }

    double WmaStream::seconds() const
    {
        return state_->header.seconds;
    }

    std::expected<std::size_t, std::string> WmaStream::read(std::span<float> out)
    {
        State& s = *state_;
        const std::size_t channels = s.header.audio.channels;
        const std::size_t want = out.size() / channels * channels;
        std::size_t written = 0;
        while (written < want)
        {
            if (s.ready_at < s.ready.size())
            {
                const std::size_t n = std::min(want - written, s.ready.size() - s.ready_at);
                std::copy_n(s.ready.begin() + static_cast<std::ptrdiff_t>(s.ready_at), n, out.begin() + static_cast<std::ptrdiff_t>(written));
                s.ready_at += n;
                written += n;
                continue;
            }
            s.ready.clear();
            s.ready_at = 0;
            auto more = s.decode_more();
            if (!more)
                return std::unexpected(more.error());
            if (!*more)
                break;
        }
        return written / channels;
    }

    std::expected<void, std::string> WmaStream::rewind()
    {
        return state_->start();
    }

} // namespace rct3::audio
