// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/flexi_texture.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kFlexiTag = "ftx";
        constexpr std::size_t kHeaderSize = 36; // nine 32-bit words
        constexpr std::size_t kFrameSize = 28;  // seven 32-bit words

        std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<FlexiFrame> read_frame(const AddressSpace& space, std::span<const std::byte> raw)
        {
            FlexiFrame frame;
            frame.log2_size = u32_at(raw, 0);
            frame.width = u32_at(raw, 4);
            frame.height = u32_at(raw, 8);
            frame.flags = u32_at(raw, 12);

            const auto palette = space.bytes_at(u32_at(raw, 16), kFlexiPaletteSize);
            if (!palette)
                return std::nullopt;
            frame.palette = *palette;

            const std::uint64_t plane = static_cast<std::uint64_t>(frame.width) * frame.height;
            if (plane > UINT32_MAX)
                return std::nullopt;

            const auto pixels = space.bytes_at(u32_at(raw, 20), static_cast<std::size_t>(plane));
            if (!pixels)
                return std::nullopt;
            frame.pixels = *pixels;

            if (const std::uint32_t alpha_address = u32_at(raw, 24); alpha_address != 0)
            {
                const auto alpha = space.bytes_at(alpha_address, static_cast<std::size_t>(plane));
                if (!alpha)
                    return std::nullopt;
                frame.alpha = *alpha;
            }
            return frame;
        }

        std::optional<FlexiTexture> read_texture(const AddressSpace& space, std::uint32_t address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            FlexiTexture texture;
            texture.log2_size = u32_at(*header, 0);
            texture.width = u32_at(*header, 4);
            texture.height = u32_at(*header, 8);
            texture.animation = u32_at(*header, 12);
            texture.flags = u32_at(*header, 16);
            const std::uint32_t sequence_count = u32_at(*header, 20);
            const std::uint32_t sequence_address = u32_at(*header, 24);
            const std::uint32_t frame_count = u32_at(*header, 28);
            const std::uint32_t frame_address = u32_at(*header, 32);

            // Counts are checked through the size of the block that must hold the array.
            if (sequence_count > UINT32_MAX / 4 || frame_count > UINT32_MAX / kFrameSize)
                return std::nullopt;

            if (sequence_count != 0)
            {
                const auto raw = space.bytes_at(sequence_address, std::size_t { sequence_count } * 4);
                if (!raw)
                    return std::nullopt;
                texture.sequence.reserve(sequence_count);
                for (std::uint32_t i = 0; i < sequence_count; ++i)
                {
                    const std::uint32_t index = u32_at(*raw, std::size_t { i } * 4);
                    if (index >= frame_count)
                        return std::nullopt;
                    texture.sequence.push_back(index);
                }
            }

            if (frame_count != 0)
            {
                const auto raw = space.bytes_at(frame_address, std::size_t { frame_count } * kFrameSize);
                if (!raw)
                    return std::nullopt;
                texture.frames.reserve(frame_count);
                for (std::uint32_t i = 0; i < frame_count; ++i)
                {
                    auto frame = read_frame(space, raw->subspan(std::size_t { i } * kFrameSize, kFrameSize));
                    if (!frame)
                        return std::nullopt;
                    texture.frames.push_back(*frame);
                }
            }
            return texture;
        }

    } // namespace

    std::vector<FlexiTextureItem> read_flexi_textures(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<FlexiTextureItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kFlexiTag)
                continue;
            items.push_back({ parts.name, read_texture(space, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
