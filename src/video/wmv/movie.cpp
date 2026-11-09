// SPDX-License-Identifier: GPL-3.0-or-later
#include "video/wmv/movie.hpp"

#include "audio/asf_file.hpp"
#include "video/wmv/decoder.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>

namespace rct3::video::wmv
{

    namespace
    {
        constexpr std::uint32_t kWmv3 = 0x33564D57; ///< "WMV3", the first letter in the low byte
        constexpr std::uintmax_t kMaxFile = 256u << 20;

        std::uint8_t clamp_byte(int value)
        {
            return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        }
    } // namespace

    void yuv_to_rgba(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> y, std::size_t y_stride, std::span<const std::uint8_t> u, std::size_t u_stride, std::span<const std::uint8_t> v, std::size_t v_stride, std::span<std::uint8_t> rgba)
    {
        // In 1/256ths: the usual BT.601 limited-range matrix.
        for (std::uint32_t row = 0; row < height; ++row)
            for (std::uint32_t col = 0; col < width; ++col)
            {
                const int luma = 298 * (y[row * y_stride + col] - 16);
                const int cb = u[row / 2 * u_stride + col / 2] - 128;
                const int cr = v[row / 2 * v_stride + col / 2] - 128;
                std::uint8_t* out = &rgba[(static_cast<std::size_t>(row) * width + col) * 4];
                out[0] = clamp_byte((luma + 409 * cr + 128) >> 8);
                out[1] = clamp_byte((luma - 100 * cb - 208 * cr + 128) >> 8);
                out[2] = clamp_byte((luma + 516 * cb + 128) >> 8);
                out[3] = 255;
            }
    }

    struct WmvMovie::State
    {
        std::vector<std::byte> file;
        audio::AsfHeader header;
        std::optional<Decoder> decoder;
        audio::AsfObjects frames { 0 };
        std::size_t next_packet = 0; ///< In the file
        std::optional<audio::AsfObject> due;
        bool drained = false;
        std::vector<std::uint8_t> picture;

        /// The next frame's data, reading packets as needed; none at the end.
        std::optional<audio::AsfObject> next()
        {
            while (frames.empty())
            {
                const std::size_t at = header.first_packet + next_packet * header.packet_size;
                if (at + header.packet_size > file.size() || (header.packet_count > 0 && next_packet >= header.packet_count))
                    return std::nullopt;
                ++next_packet;
                const auto payloads = audio::read_asf_packet(std::span(file).subspan(at, header.packet_size));
                if (!payloads)
                    continue; // A packet we cannot read: its frames are lost
                for (const audio::AsfPayload& p : *payloads)
                    frames.add(p);
            }
            return frames.take();
        }

        bool show(std::span<const std::byte> frame)
        {
            const auto p = decoder->decode(frame);
            if (!p)
                return false;
            const std::uint32_t w = std::min(p->width, header.video->width);
            const std::uint32_t h = std::min(p->height, header.video->height);
            yuv_to_rgba(w, h, p->planes[0], p->strides[0], p->planes[1], p->strides[1], p->planes[2], p->strides[2], picture);
            return true;
        }
    };

    WmvMovie::WmvMovie(std::unique_ptr<State> state)
        : state_(std::move(state))
    {
    }
    WmvMovie::WmvMovie(WmvMovie&&) noexcept = default;
    WmvMovie& WmvMovie::operator=(WmvMovie&&) noexcept = default;
    WmvMovie::~WmvMovie() = default;

    std::expected<WmvMovie, std::string> WmvMovie::open(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::unexpected(std::format("{}: {}", path.string(), error.message()));
        if (size > kMaxFile)
            return std::unexpected(std::format("{}: too big for a movie", path.string()));
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream in(path, std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return std::unexpected(std::format("{}: cannot read", path.string()));
        auto movie = open(std::move(bytes));
        if (!movie)
            return std::unexpected(std::format("{}: {}", path.string(), movie.error()));
        return movie;
    }

    std::expected<WmvMovie, std::string> WmvMovie::open(std::vector<std::byte> file)
    {
        auto s = std::make_unique<State>();
        s->file = std::move(file);
        const auto size = audio::asf_header_size(std::span(s->file).first(std::min<std::size_t>(s->file.size(), 30)));
        if (!size)
            return std::unexpected(size.error());
        if (*size > s->file.size())
            return std::unexpected("cut short ASF header");
        auto header = audio::read_asf_header(std::span(s->file).first(*size));
        if (!header)
            return std::unexpected(header.error());
        s->header = std::move(*header);
        if (!s->header.video)
            return std::unexpected("no video stream");
        const audio::AsfVideoFormat& f = *s->header.video;
        if (f.codec != kWmv3)
            return std::unexpected(std::format("not Windows Media Video 9 (codec {:#x})", f.codec));
        s->decoder = Decoder::open({ .width = f.width, .height = f.height, .extra = f.extra });
        if (!s->decoder)
            return std::unexpected(std::format("the decoder cannot take this stream ({} by {})", f.width, f.height));
        s->frames = audio::AsfObjects(s->header.video_stream);
        s->picture.assign(static_cast<std::size_t>(f.width) * f.height * 4, 0);
        s->due = s->next();
        return WmvMovie(std::move(s));
    }

    std::uint32_t WmvMovie::width() const
    {
        return state_->header.video->width;
    }

    std::uint32_t WmvMovie::height() const
    {
        return state_->header.video->height;
    }

    double WmvMovie::seconds() const
    {
        return state_->header.seconds;
    }

    bool WmvMovie::advance(double seconds)
    {
        State& s = *state_;
        bool changed = false;
        while (s.due && (static_cast<double>(s.due->time) - s.header.preroll) / 1000 <= seconds)
        {
            changed = s.show(s.due->data) || changed;
            s.due = s.next();
        }
        // The decoder may hold the last picture back; it shows once the frames run out.
        if (!s.due && !s.drained)
        {
            s.drained = true;
            changed = s.show({ }) || changed;
        }
        return changed;
    }

    std::span<const std::uint8_t> WmvMovie::picture() const
    {
        return state_->picture;
    }

    bool WmvMovie::finished() const
    {
        return !state_->due && state_->drained;
    }

} // namespace rct3::video::wmv
