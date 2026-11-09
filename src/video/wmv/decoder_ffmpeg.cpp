// SPDX-License-Identifier: GPL-3.0-or-later
//
// The WMV3 decoder as FFmpeg's, built in (third_party/ffmpeg). It may hold the last picture back until it is
// given an empty frame.
#include "video/wmv/decoder.hpp"

#include <ffmpeg/rct3_wmv.h>

namespace rct3::video::wmv
{

    struct Decoder::State
    {
        struct Close
        {
            void operator()(Rct3WmvDecoder* d) const
            {
                rct3_wmv_close(d);
            }
        };
        std::unique_ptr<Rct3WmvDecoder, Close> decoder;
    };

    Decoder::Decoder(std::unique_ptr<State> state)
        : state_(std::move(state))
    {
    }
    Decoder::Decoder(Decoder&&) noexcept = default;
    Decoder& Decoder::operator=(Decoder&&) noexcept = default;
    Decoder::~Decoder() = default;

    std::optional<Decoder> Decoder::open(const Format& format)
    {
        const Rct3WmvFormat f {
            .width = static_cast<int>(format.width),
            .height = static_cast<int>(format.height),
            .extra = reinterpret_cast<const std::uint8_t*>(format.extra.data()),
            .extra_size = static_cast<int>(format.extra.size()),
        };
        auto s = std::make_unique<State>();
        s->decoder.reset(rct3_wmv_open(&f));
        if (!s->decoder)
            return std::nullopt;
        return Decoder(std::move(s));
    }

    std::optional<Picture> Decoder::decode(std::span<const std::byte> frame)
    {
        Rct3WmvPicture p { };
        if (rct3_wmv_decode(state_->decoder.get(), frame.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(frame.data()), static_cast<int>(frame.size()), &p) != 1 || p.width <= 0 || p.height <= 0)
            return std::nullopt;
        Picture out { .width = static_cast<std::uint32_t>(p.width), .height = static_cast<std::uint32_t>(p.height) };
        for (int i = 0; i < 3; ++i)
        {
            const auto rows = static_cast<std::size_t>(i == 0 ? p.height : (p.height + 1) / 2);
            out.strides[i] = static_cast<std::size_t>(p.strides[i]);
            out.planes[i] = std::span(p.planes[i], out.strides[i] * rows);
        }
        return out;
    }

} // namespace rct3::video::wmv
