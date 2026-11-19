// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/model_anim.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>
#include <unordered_map>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kAnimTag = "modelanim";
        constexpr usize kHeaderSize = 48; // see read_one
        constexpr u32 kMaxFrames = 1u << 20;
        constexpr u32 kMaxBones = 4096;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        // Splits `count` NUL-terminated strings off the front of `text`. False when the text runs out.
        bool take_names(std::string_view& text, u32 count, std::vector<std::string_view>& out)
        {
            out.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                const usize end = text.find('\0');
                if (end == std::string_view::npos)
                    return false;
                out.push_back(text.substr(0, end));
                text.remove_prefix(end + 1);
            }
            return true;
        }

        std::optional<ModelAnim> read_one(const AddressSpace& space, const Part& part, usize row)
        {
            if (row >= part.loaders.extra.size() || part.loaders.extra[row].size() != 1)
                return std::nullopt;
            const Chunk& chunk = part.loaders.extra[row][0];
            if (chunk.file_offset > part.bytes.size() || part.bytes.size() - chunk.file_offset < chunk.size)
            {
                return std::nullopt;
            }

            // Header: f32 duration, u32 frame count, ptr and zero-filled hash arrays for the name lists, counts of
            // the lists (position bones, rotation bones, plain list, pair list), then the two sample pointers.
            const auto header = space.bytes_at(part.loaders.entries[row].data, kHeaderSize);
            if (!header)
                return std::nullopt;

            ModelAnim anim;
            std::memcpy(&anim.duration, header->data(), sizeof anim.duration);
            anim.frame_count = u32_at(*header, 4);
            const u32 position_count = u32_at(*header, 0x18);
            const u32 rotation_count = u32_at(*header, 0x1c);
            const u32 plain_count = u32_at(*header, 0x20);
            const u32 pair_count = u32_at(*header, 0x24);
            if (anim.frame_count > kMaxFrames || position_count > kMaxBones || rotation_count > kMaxBones)
            {
                return std::nullopt;
            }
            if (plain_count != 0 || pair_count != 0)
                return std::nullopt;

            std::string_view text(reinterpret_cast<const char*>(part.bytes.data()) + chunk.file_offset, chunk.size);
            if (!take_names(text, position_count, anim.position_bones))
                return std::nullopt;
            if (!take_names(text, rotation_count, anim.rotation_bones))
                return std::nullopt;

            const usize position_total = usize { anim.frame_count } * position_count;
            const usize rotation_total = usize { anim.frame_count } * rotation_count;
            if (position_total != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 0x28), position_total * 12);
                if (!raw)
                    return std::nullopt;
                anim.positions.resize(position_total);
                std::memcpy(anim.positions.data(), raw->data(), raw->size());
            }
            if (rotation_total != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 0x2c), rotation_total * 16);
                if (!raw)
                    return std::nullopt;
                anim.rotations.resize(rotation_total);
                std::memcpy(anim.rotations.data(), raw->data(), raw->size());
            }
            return anim;
        }

    } // namespace

    std::vector<ModelAnimItem> read_model_anims(const Overlay& overlay, const Part& part)
    {
        const auto& loaders = part.file.header.loaders;
        const AddressSpace space = overlay.space();

        std::unordered_map<u32, std::string_view> names;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kAnimTag)
                names.emplace(symbol.data, parts.name);
        }

        std::vector<ModelAnimItem> items;
        for (usize row = 0; row < part.loaders.entries.size(); ++row)
        {
            const LoaderEntry& entry = part.loaders.entries[row];
            if (entry.loader_type >= loaders.size() || loaders[entry.loader_type].tag != kAnimTag)
                continue;
            ModelAnimItem item;
            if (const auto it = names.find(entry.data); it != names.end())
                item.name = it->second;
            item.anim = read_one(space, part, row);
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
