// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/simple_anim.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kAnimTag = "san";
        constexpr usize kHeaderSize = 16; // key count, channel count, times, samples
        constexpr usize kSampleSize = 24;
        constexpr u32 kMaxKeys = 1u << 20;
        constexpr u32 kMaxChannels = 64;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<SimpleAnim> read_anim(const AddressSpace& space, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            const u32 keys = u32_at(*header, 0);
            SimpleAnim anim;
            anim.channels = u32_at(*header, 4);
            if (keys > kMaxKeys || anim.channels > kMaxChannels)
                return std::nullopt;

            if (keys != 0)
            {
                const auto times = space.bytes_at(u32_at(*header, 8), usize { keys } * 4);
                if (!times)
                    return std::nullopt;
                anim.times.resize(keys);
                std::memcpy(anim.times.data(), times->data(), times->size());
            }

            const usize sample_count = usize { keys } * anim.channels;
            if (sample_count != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 12), sample_count * kSampleSize);
                if (!raw)
                    return std::nullopt;
                anim.samples.resize(sample_count);
                for (usize i = 0; i < sample_count; ++i)
                {
                    const std::byte* p = raw->data() + i * kSampleSize;
                    std::memcpy(anim.samples[i].vector.data(), p, 12);
                    std::memcpy(anim.samples[i].packed.data(), p + 12, 12);
                }
            }
            return anim;
        }

    } // namespace

    std::vector<SimpleAnimItem> read_simple_anims(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<SimpleAnimItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kAnimTag)
                continue;
            items.push_back({ parts.name, read_anim(space, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
