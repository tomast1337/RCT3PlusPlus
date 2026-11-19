// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/sound.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSoundTag = "snd";

        std::uint16_t u16_at(std::span<const std::byte> b, std::size_t at)
        {
            std::uint16_t v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        float f32_at(std::span<const std::byte> b, std::size_t at)
        {
            float v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

    } // namespace

    std::vector<SoundItem> read_sounds(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<SoundItem> items;

        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kSoundTag)
                continue;

            SoundItem item { parts.name, std::nullopt };
            if (const auto raw = space.bytes_at(symbol.data, kSoundDescriptorSize))
            {
                const auto b = *raw;
                SoundInfo info;
                info.format_tag = u16_at(b, 0);
                info.channels = u16_at(b, 2);
                info.sample_rate = u32_at(b, 4);
                info.bytes_per_second = u32_at(b, 8);
                info.block_align = u16_at(b, 12);
                info.bits_per_sample = u16_at(b, 14);
                info.fade_in_curve = u32_at(b, 20);
                info.fade_in_seconds = f32_at(b, 24);
                info.rise_seconds = f32_at(b, 28);
                info.peak_level = f32_at(b, 32);
                info.decay_curve = u32_at(b, 36);
                info.hold_seconds = f32_at(b, 40);
                info.tail_curve = u32_at(b, 44);
                info.fade_out_seconds = f32_at(b, 48);
                info.near_distance = f32_at(b, 52);
                info.far_distance = f32_at(b, 56);
                info.loops = u32_at(b, 60) != 0;
                info.data_address = u32_at(b, 64);
                info.data_size = u32_at(b, 68);
                info.samples = space.bytes_at(info.data_address, info.data_size);
                item.info = info;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
