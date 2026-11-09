// SPDX-License-Identifier: GPL-3.0-or-later
//
// The WMA decoder as FFmpeg's, built in (third_party/ffmpeg). It starts two frames late (its windows overlap):
// delay() says how many samples that is, and WmaStream drops them.
#include "audio/wma/decoder.hpp"

#include <algorithm>
#include <ffmpeg/rct3_wma.h>

namespace rct3::audio::wma
{

    struct Decoder::State
    {
        struct Close
        {
            void operator()(Rct3WmaDecoder* d) const
            {
                rct3_wma_close(d);
            }
        };
        std::unique_ptr<Rct3WmaDecoder, Close> decoder;
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
        const Rct3WmaFormat f {
            .version = format.version,
            .sample_rate = static_cast<int>(format.sample_rate),
            .channels = format.channels,
            .bit_rate = static_cast<int>(format.bit_rate),
            .block_align = format.block_align,
            .extra = reinterpret_cast<const std::uint8_t*>(format.extra.data()),
            .extra_size = static_cast<int>(format.extra.size()),
        };
        auto s = std::make_unique<State>();
        s->decoder.reset(rct3_wma_open(&f));
        if (!s->decoder)
            return std::nullopt;
        return Decoder(std::move(s));
    }

    int Decoder::decode(std::span<const std::byte> packet, const float* const*& planes)
    {
        const int n = rct3_wma_decode(state_->decoder.get(), packet.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(packet.data()), static_cast<int>(packet.size()), &planes);
        return std::max(n, 0);
    }

    int Decoder::delay() const
    {
        return rct3_wma_delay(state_->decoder.get());
    }

    void Decoder::reset()
    {
        rct3_wma_reset(state_->decoder.get());
    }

} // namespace rct3::audio::wma
