// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/scenery_item.hpp"

#include "ovl/pointer_fields.hpp"
#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kItemTag = "sid";
        constexpr std::string_view kVisualTag = "svd";
        constexpr std::string_view kIconTag = "gsi";
        constexpr std::string_view kTextTag = "txt";
        constexpr std::string_view kSoundTag = "snd";
        constexpr std::string_view kResearchTag = "rcg";

        // Sizes of the structure in bytes: compact base form, base form, first and second expansion.
        constexpr std::size_t kCompactSize = 164;
        constexpr std::size_t kBaseSize = 216;
        constexpr std::size_t kFirstAddonSize = 232;
        constexpr std::size_t kSecondAddonSize = 240;

        constexpr std::size_t kSquareSize = 20; // flags, min, max, mask pointer, mask count
        constexpr std::size_t kNodeSize = 88;
        constexpr std::size_t kSoundSize = 16; // sound count, sound references, script count, script pointers
        constexpr std::size_t kResearchSize = 12;

        // Counts above these are treated as corruption. Real files stay far below them (largest seen: 374
        // squares, 169 mask words, 16 nodes, 16 sounds, 4 visuals).
        constexpr std::uint32_t kMaxSquares = 1u << 12;
        constexpr std::uint32_t kMaxMasks = 1u << 14;
        constexpr std::uint32_t kMaxNodes = 1u << 10;
        constexpr std::uint32_t kMaxParameters = 1u << 8;
        constexpr std::uint32_t kMaxSounds = 1u << 8;
        constexpr std::uint32_t kMaxSoundNames = 1u << 12;
        constexpr std::uint32_t kMaxVisuals = 1u << 10;
        constexpr std::uint32_t kMaxResearch = 1u << 8;
        constexpr std::size_t kMaxRun = 1u << 12; // items stored back to back

        // Where each field of the structure sits. The compact form is the base form with the bounding box,
        // node array and group name removed, so most fields are the same words at lower offsets.
        struct Layout
        {
            std::size_t cost, removal_cost, unknown_44, type, group, visual_count, visuals, icon, group_icon;
            std::size_t group_text; // 0 when absent
            std::size_t source_path, parameter_count, parameters, sound_count, sounds, name_text;
            std::size_t research_count, research, unknown_84, colours, late;
            std::size_t late_words; // how many words of `late` exist
        };

        constexpr Layout kFullLayout { 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84, 0x8c, 0x98, 15 };
        constexpr Layout kCompactLayout { 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x70, 0x7c, 10 };

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

        std::string_view string_at(const AddressSpace& space, std::uint32_t address)
        {
            const auto s = space.cstring_at(address);
            return s ? *s : std::string_view { };
        }

        bool read_squares(const AddressSpace& space, const PointerFields& pointers, std::uint32_t address, std::uint32_t count, std::vector<SceneryItemSquare>& out)
        {
            const auto raw = space.bytes_at(address, std::size_t { count } * kSquareSize);
            if (!raw)
                return false;
            std::uint32_t total_masks = 0;
            out.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::size_t at = std::size_t { i } * kSquareSize;
                SceneryItemSquare sq;
                sq.flags = u32_at(*raw, at);
                sq.min_height = static_cast<std::int32_t>(u32_at(*raw, at + 4));
                sq.max_height = static_cast<std::int32_t>(u32_at(*raw, at + 8));
                const std::uint32_t pointer = u32_at(*raw, at + 12);
                const std::uint32_t mask_count = u32_at(*raw, at + 16);
                sq.support_kind = mask_count;
                if (mask_count > kMaxMasks - total_masks)
                    return false;
                total_masks += mask_count;
                // A square without collision has a count but no array: the pointer is null, not relocated.
                const bool null_pointer = pointer == 0 && !pointers.contains(address + static_cast<std::uint32_t>(at) + 12);
                if (mask_count != 0 && !null_pointer)
                {
                    // Each word is looked up on its own: a short array at the very end of the common file's data
                    // continues into the first block of the unique file, which follows it in the address space.
                    sq.height_masks.reserve(mask_count);
                    for (std::uint32_t k = 0; k < mask_count; ++k)
                    {
                        if (k > UINT32_MAX / 4 || pointer > UINT32_MAX - k * 4)
                            return false;
                        const auto word = space.bytes_at(pointer + k * 4, 4);
                        if (!word)
                            return false;
                        sq.height_masks.push_back(u32_at(*word, 0));
                    }
                }
                out.push_back(std::move(sq));
            }
            return true;
        }

    } // namespace

    SceneryItemReader::SceneryItemReader(const Overlay& overlay, const Part& part)
        : part_(&part)
        , space_(overlay.space())
        , refs_(part, space_)
        , pointers_(overlay)
    {
    }

    std::optional<SceneryItem> SceneryItemReader::read(std::uint32_t address, bool allow_compact) const
    {
        const AddressSpace& space = space_;
        const ReferenceNames& refs = refs_;
        const auto head = space.bytes_at(address, 12);
        if (!head)
            return std::nullopt;

        const std::uint32_t version = u32_at(*head, 8) >> 16;
        std::size_t size = 0;
        bool compact = false;
        switch (version)
        {
            case 0:
                // The shorter base form is told apart by its block ending before the longer one would.
                if (space.bytes_at(address, kBaseSize))
                    size = kBaseSize;
                else if (allow_compact)
                    size = kCompactSize, compact = true;
                else
                    return std::nullopt;
                break;
            case 1:
                size = kFirstAddonSize;
                break;
            case 2:
                size = kSecondAddonSize;
                break;
            default:
                return std::nullopt;
        }
        const auto raw = space.bytes_at(address, size);
        if (!raw)
            return std::nullopt;
        const Layout& l = compact ? kCompactLayout : kFullLayout;

        SceneryItem item;
        item.version = version;
        item.compact = compact;
        item.flags = u32_at(*raw, 4);
        item.positioning = static_cast<std::uint16_t>(u32_at(*raw, 8) & 0xFFFF);
        item.height_squares = u32_at(*raw, 12);
        item.x_squares = u32_at(*raw, 16);
        item.z_squares = u32_at(*raw, 20);

        const std::uint64_t square_count = std::uint64_t { item.x_squares } * item.z_squares;
        if (square_count > kMaxSquares)
            return std::nullopt;
        if (square_count != 0 && !read_squares(space, pointers_, u32_at(*raw, 24), static_cast<std::uint32_t>(square_count), item.squares))
        {
            return std::nullopt;
        }

        if (!compact)
        {
            for (std::size_t i = 0; i < 3; ++i)
            {
                item.bounds_min[i] = f32_at(*raw, 0x1c + i * 4);
                item.bounds_max[i] = f32_at(*raw, 0x28 + i * 4);
            }
            const std::uint32_t node_count = u32_at(*raw, 0x34);
            if (node_count > kMaxNodes)
                return std::nullopt;
            if (node_count != 0)
            {
                const auto nodes = space.bytes_at(u32_at(*raw, 0x38), std::size_t { node_count } * kNodeSize);
                if (!nodes)
                    return std::nullopt;
                item.nodes.resize(node_count);
                for (std::uint32_t i = 0; i < node_count; ++i)
                {
                    for (std::size_t w = 0; w < 22; ++w)
                        item.nodes[i].words[w] = u32_at(*nodes, std::size_t { i } * kNodeSize + w * 4);
                }
            }
            item.group_text = refs.find(address + l.group_text, kTextTag);
        }

        item.cost = static_cast<std::int32_t>(u32_at(*raw, l.cost));
        item.removal_cost = static_cast<std::int32_t>(u32_at(*raw, l.removal_cost));
        item.unknown_44 = u32_at(*raw, l.unknown_44);
        item.type = u32_at(*raw, l.type);
        item.group = string_at(space, u32_at(*raw, l.group));
        item.source_path = string_at(space, u32_at(*raw, l.source_path));
        item.icon = refs.find(address + l.icon, kIconTag);
        item.group_icon = refs.find(address + l.group_icon, kIconTag);
        item.name_text = refs.find(address + l.name_text, kTextTag);

        // Visual references: an array of reference fields, which lie right after the structure for the
        // first one and may continue past it.
        const std::uint32_t visual_count = u32_at(*raw, l.visual_count);
        if (visual_count > kMaxVisuals)
            return std::nullopt;
        if (visual_count != 0)
        {
            const std::uint32_t slots = u32_at(*raw, l.visuals);
            if (!space.bytes_at(slots, std::size_t { visual_count } * 4))
                return std::nullopt;
            item.visuals.reserve(visual_count);
            for (std::uint32_t i = 0; i < visual_count; ++i)
                item.visuals.push_back(refs.find(slots + i * 4, kVisualTag));
        }

        const std::uint32_t parameter_count = u32_at(*raw, l.parameter_count);
        if (parameter_count > kMaxParameters)
            return std::nullopt;
        if (parameter_count != 0)
        {
            const auto rows = space.bytes_at(u32_at(*raw, l.parameters), std::size_t { parameter_count } * 8);
            if (!rows)
                return std::nullopt;
            for (std::uint32_t i = 0; i < parameter_count; ++i)
            {
                const auto name = space.cstring_at(u32_at(*rows, std::size_t { i } * 8));
                const auto value = space.cstring_at(u32_at(*rows, std::size_t { i } * 8 + 4));
                if (!name || !value)
                    return std::nullopt;
                item.parameters.push_back({ *name, *value });
            }
        }

        const std::uint32_t sound_count = u32_at(*raw, l.sound_count);
        if (sound_count > kMaxSounds)
            return std::nullopt;
        if (sound_count != 0)
        {
            const auto rows = space.bytes_at(u32_at(*raw, l.sounds), std::size_t { sound_count } * kSoundSize);
            if (!rows)
                return std::nullopt;
            std::uint32_t total_names = 0;
            for (std::uint32_t i = 0; i < sound_count; ++i)
            {
                const std::size_t at = std::size_t { i } * kSoundSize;
                const std::uint32_t names = u32_at(*rows, at);
                const std::uint32_t slots = u32_at(*rows, at + 4);
                const std::uint32_t scripts = u32_at(*rows, at + 8);
                if (names > kMaxSoundNames - total_names)
                    return std::nullopt;
                total_names += names;
                if (names != 0 && !space.bytes_at(slots, std::size_t { names } * 4))
                    return std::nullopt;
                // The script slots are a plain pointer array. Its bounds are checked, its contents are not decoded.
                if (scripts > kMaxSoundNames)
                    return std::nullopt;
                if (scripts != 0 && !space.bytes_at(u32_at(*rows, at + 12), std::size_t { scripts } * 4))
                    return std::nullopt;
                SceneryItemSound sound;
                sound.script_count = scripts;
                sound.sounds.reserve(names);
                for (std::uint32_t k = 0; k < names; ++k)
                    sound.sounds.push_back(refs.find(slots + k * 4, kSoundTag));
                item.sounds.push_back(std::move(sound));
            }
        }

        const std::uint32_t research_count = u32_at(*raw, l.research_count);
        if (!read_research(u32_at(*raw, l.research), research_count, item.research))
            return std::nullopt;

        item.unknown_84 = { u32_at(*raw, l.unknown_84), u32_at(*raw, l.unknown_84 + 4) };
        for (std::size_t i = 0; i < 3 && !compact; ++i)
            item.default_colours[i] = u32_at(*raw, l.colours + i * 4);
        for (std::size_t i = 0; i < l.late_words; ++i)
            item.late_words[i] = u32_at(*raw, l.late + i * 4);

        if (version >= 1)
        {
            item.has_ride_data = u32_at(*raw, 0xd4) != 0;
            item.addon = u32_at(*raw, 0xdc);
            item.addon_extra = u32_at(*raw, 0xe0);
        }
        if (version == 2)
        {
            item.unknown_float = f32_at(*raw, 0xe4);
            item.unknown_int = static_cast<std::int32_t>(u32_at(*raw, 0xe8));
        }
        return item;
    }

    bool SceneryItemReader::read_research(std::uint32_t address, std::uint32_t count, std::vector<SceneryItemResearch>& out) const
    {
        if (count > kMaxResearch)
            return false;
        if (count == 0)
            return true;
        if (!space_.bytes_at(address, std::size_t { count } * kResearchSize))
            return false;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto row = space_.bytes_at(address + i * kResearchSize, kResearchSize);
            if (!row)
                return false;
            SceneryItemResearch r;
            r.category = refs_.find(address + i * kResearchSize, kResearchTag);
            r.unknown = static_cast<std::int32_t>(u32_at(*row, 4));
            r.priority = f32_at(*row, 8);
            out.push_back(r);
        }
        return true;
    }

    std::size_t SceneryItemReader::stride_of(const SceneryItem& item) noexcept
    {
        std::size_t size = kBaseSize;
        if (item.compact)
            size = kCompactSize;
        else if (item.version == 1)
            size = kFirstAddonSize;
        else if (item.version == 2)
            size = kSecondAddonSize;
        // The first visual reference is the structure's last word, further ones follow it.
        if (item.visuals.size() > 1)
            size += (item.visuals.size() - 1) * 4;
        return size;
    }

    std::optional<std::uint32_t> SceneryItemReader::block_end(std::uint32_t address) const noexcept
    {
        const auto where = part_->file.locate(address, part_->base);
        if (!where)
            return std::nullopt;
        const auto& block = part_->file.block_types[where->type].blocks[where->index];
        return address - where->offset + block.size;
    }

    std::optional<std::vector<SceneryItem>> SceneryItemReader::read_run(std::uint32_t address) const
    {
        const auto end = block_end(address);
        if (!end)
            return std::nullopt;
        std::vector<SceneryItem> items;
        while (address < *end)
        {
            if (items.size() >= kMaxRun)
                return std::nullopt;
            auto item = read(address, /*allow_compact=*/false);
            if (!item)
                return std::nullopt;
            const std::size_t stride = stride_of(*item);
            items.push_back(std::move(*item));
            if (stride > *end - address)
                return std::nullopt; // the data does not end with the block
            address += static_cast<std::uint32_t>(stride);
        }
        return items;
    }

    std::vector<SceneryItemEntry> read_scenery_items(const Overlay& overlay, const Part& part)
    {
        const SceneryItemReader reader(overlay, part);

        std::vector<SceneryItemEntry> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kItemTag)
                continue;
            items.push_back({ parts.name, reader.read(symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
