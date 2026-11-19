// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/flic.hpp"

#include "core/types.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kFlicTag = "flic";
        constexpr usize kItemSize = 12;        // u32, u32, f32
        constexpr usize kImageHeaderSize = 32; // eight 32-bit words
        constexpr u32 kRawFormat = 2;
        constexpr u32 kBlock8Format = 18;
        constexpr u32 kBlock16Format = 19;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<FlicImageHeader> read_image_header(std::span<const std::byte> data)
        {
            if (data.size() < kImageHeaderSize)
                return std::nullopt;

            FlicImageHeader h;
            h.format = u32_at(data, 0);
            h.width = u32_at(data, 4);
            h.height = u32_at(data, 8);
            h.level_count = u32_at(data, 12);
            h.row_bytes = u32_at(data, 24);
            h.row_count = u32_at(data, 28);

            // The size is stored twice; both copies agree in all files seen.
            if (u32_at(data, 16) != h.width || u32_at(data, 20) != h.height)
                return std::nullopt;

            u64 block_bytes = 0;
            if (h.format == kBlock8Format)
                block_bytes = 8;
            else if (h.format == kBlock16Format)
                block_bytes = 16;
            else if (h.format != kRawFormat)
                return std::nullopt;

            // The row geometry must follow from the format, so a damaged header is not taken for an image.
            const u64 blocks_wide = (u64 { h.width } + 3) / 4;
            const u64 blocks_high = (u64 { h.height } + 3) / 4;
            const u64 row_bytes = block_bytes != 0 ? blocks_wide * block_bytes : u64 { h.width } * 4;
            const u64 row_count = block_bytes != 0 ? blocks_high : h.height;
            if (h.row_bytes != row_bytes || h.row_count != row_count)
                return std::nullopt;
            return h;
        }

        std::optional<Flic> read_one(const AddressSpace& space, const Part& part, usize row, u32 revision)
        {
            if (row >= part.loaders.extra.size() || part.loaders.extra[row].size() != 1)
                return std::nullopt;

            const Chunk& chunk = part.loaders.extra[row][0];
            if (chunk.file_offset > part.bytes.size() || part.bytes.size() - chunk.file_offset < chunk.size)
            {
                return std::nullopt;
            }

            const auto item = space.bytes_at(part.loaders.entries[row].data, kItemSize);
            if (!item)
                return std::nullopt;

            Flic flic;
            flic.revision = revision;
            flic.frame_count = u32_at(*item, 4);
            std::memcpy(&flic.rate, item->data() + 8, sizeof flic.rate);
            flic.data = std::span<const std::byte>(part.bytes).subspan(chunk.file_offset, chunk.size);

            if (revision == 3)
            {
                if (flic.data.size() / 4 == flic.frame_count && flic.data.size() % 4 == 0)
                {
                    flic.frames.reserve(flic.frame_count);
                    for (u32 i = 0; i < flic.frame_count; ++i)
                        flic.frames.push_back(u32_at(flic.data, usize { i } * 4));
                }
            }
            else if (revision == 2)
            {
                flic.image = read_image_header(flic.data);
            }
            return flic;
        }

    } // namespace

    std::vector<FlicItem> read_flics(const Overlay& overlay, const Part& part)
    {
        const auto& loaders = part.file.header.loaders;
        const AddressSpace space = overlay.space();

        std::vector<FlicItem> items;
        for (usize row = 0; row < part.loaders.entries.size(); ++row)
        {
            const LoaderEntry& entry = part.loaders.entries[row];
            if (entry.loader_type >= loaders.size() || loaders[entry.loader_type].tag != kFlicTag)
                continue;
            items.push_back({ entry.data, read_one(space, part, row, loaders[entry.loader_type].loader_type) });
        }
        return items;
    }

} // namespace rct3::ovl
