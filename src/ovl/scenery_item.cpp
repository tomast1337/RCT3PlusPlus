// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/scenery_item.hpp"

#include "core/types.hpp"
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
        constexpr usize kCompactSize = 164;
        constexpr usize kBaseSize = 216;
        constexpr usize kFirstAddonSize = 232;
        constexpr usize kSecondAddonSize = 240;

        constexpr usize kSquareSize = 20; // flags, min, max, mask pointer, mask count
        constexpr usize kNodeSize = 88;
        constexpr usize kSoundSize = 16; // sound count, sound references, script count, script pointers
        constexpr usize kResearchSize = 12;

        // Counts above these are treated as corruption. Real files stay far below them (largest seen: 374
        // squares, 169 mask words, 16 nodes, 16 sounds, 4 visuals).
        constexpr u32 kMaxSquares = 1u << 12;
        constexpr u32 kMaxMasks = 1u << 14;
        constexpr u32 kMaxNodes = 1u << 10;
        constexpr u32 kMaxParameters = 1u << 8;
        constexpr u32 kMaxSounds = 1u << 8;
        constexpr u32 kMaxSoundNames = 1u << 12;
        constexpr usize kMaxSoundSteps = 1u << 10; // per script, far past any real one
        constexpr u32 kMaxVisuals = 1u << 10;
        constexpr u32 kMaxResearch = 1u << 8;
        constexpr usize kMaxRun = 1u << 12; // items stored back to back

        // Where each field of the structure sits. The compact form is the base form with the bounding box,
        // node array and group name removed, so most fields are the same words at lower offsets.
        struct Layout
        {
            usize cost, removal_cost, unknown_44, type, group, visual_count, visuals, icon, group_icon;
            usize group_text; // 0 when absent
            usize source_path, parameter_count, parameters, sound_count, sounds, name_text;
            usize research_count, research, unknown_84, colours, late;
            usize late_words; // how many words of `late` exist
        };

        constexpr Layout kFullLayout { 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84, 0x8c, 0x98, 15 };
        constexpr Layout kCompactLayout { 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x70, 0x7c, 10 };

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        f32 f32_at(std::span<const std::byte> b, usize at)
        {
            f32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::string_view string_at(const AddressSpace& space, u32 address)
        {
            const auto s = space.cstring_at(address);
            return s ? *s : std::string_view { };
        }

        bool read_squares(const AddressSpace& space, const PointerFields& pointers, u32 address, u32 count, std::vector<SceneryItemSquare>& out)
        {
            const auto raw = space.bytes_at(address, usize { count } * kSquareSize);
            if (!raw)
                return false;
            u32 total_masks = 0;
            out.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                const usize at = usize { i } * kSquareSize;
                SceneryItemSquare sq;
                sq.flags = u32_at(*raw, at);
                sq.min_height = static_cast<i32>(u32_at(*raw, at + 4));
                sq.max_height = static_cast<i32>(u32_at(*raw, at + 8));
                const u32 pointer = u32_at(*raw, at + 12);
                const u32 mask_count = u32_at(*raw, at + 16);
                sq.support_kind = mask_count;
                if (mask_count > kMaxMasks - total_masks)
                    return false;
                total_masks += mask_count;
                // A square without collision has a count but no array: the pointer is null, not relocated.
                const bool null_pointer = pointer == 0 && !pointers.contains(address + static_cast<u32>(at) + 12);
                if (mask_count != 0 && !null_pointer)
                {
                    // Each word is looked up on its own: a short array at the very end of the common file's data
                    // continues into the first block of the unique file, which follows it in the address space.
                    sq.height_masks.reserve(mask_count);
                    for (u32 k = 0; k < mask_count; ++k)
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

    std::optional<SceneryItem> SceneryItemReader::read(u32 address, bool allow_compact) const
    {
        const AddressSpace& space = space_;
        const ReferenceNames& refs = refs_;
        const auto head = space.bytes_at(address, 12);
        if (!head)
            return std::nullopt;

        const u32 version = u32_at(*head, 8) >> 16;
        usize size = 0;
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
        item.positioning = static_cast<u16>(u32_at(*raw, 8) & 0xFFFF);
        item.height_squares = u32_at(*raw, 12);
        item.x_squares = u32_at(*raw, 16);
        item.z_squares = u32_at(*raw, 20);

        const u64 square_count = u64 { item.x_squares } * item.z_squares;
        if (square_count > kMaxSquares)
            return std::nullopt;
        if (square_count != 0 && !read_squares(space, pointers_, u32_at(*raw, 24), static_cast<u32>(square_count), item.squares))
        {
            return std::nullopt;
        }

        if (!compact)
        {
            for (usize i = 0; i < 3; ++i)
            {
                item.bounds_min[i] = f32_at(*raw, 0x1c + i * 4);
                item.bounds_max[i] = f32_at(*raw, 0x28 + i * 4);
            }
            const u32 node_count = u32_at(*raw, 0x34);
            if (node_count > kMaxNodes)
                return std::nullopt;
            if (node_count != 0)
            {
                const auto nodes = space.bytes_at(u32_at(*raw, 0x38), usize { node_count } * kNodeSize);
                if (!nodes)
                    return std::nullopt;
                item.nodes.resize(node_count);
                for (u32 i = 0; i < node_count; ++i)
                {
                    for (usize w = 0; w < 22; ++w)
                        item.nodes[i].words[w] = u32_at(*nodes, usize { i } * kNodeSize + w * 4);
                }
            }
            item.group_text = refs.find(address + l.group_text, kTextTag);
        }

        item.cost = static_cast<i32>(u32_at(*raw, l.cost));
        item.removal_cost = static_cast<i32>(u32_at(*raw, l.removal_cost));
        item.unknown_44 = u32_at(*raw, l.unknown_44);
        item.type = u32_at(*raw, l.type);
        item.group = string_at(space, u32_at(*raw, l.group));
        item.source_path = string_at(space, u32_at(*raw, l.source_path));
        item.icon = refs.find(address + l.icon, kIconTag);
        item.group_icon = refs.find(address + l.group_icon, kIconTag);
        item.name_text = refs.find(address + l.name_text, kTextTag);

        // Visual references: an array of reference fields, which lie right after the structure for the
        // first one and may continue past it.
        const u32 visual_count = u32_at(*raw, l.visual_count);
        if (visual_count > kMaxVisuals)
            return std::nullopt;
        if (visual_count != 0)
        {
            const u32 slots = u32_at(*raw, l.visuals);
            if (!space.bytes_at(slots, usize { visual_count } * 4))
                return std::nullopt;
            item.visuals.reserve(visual_count);
            for (u32 i = 0; i < visual_count; ++i)
                item.visuals.push_back(refs.find(slots + i * 4, kVisualTag));
        }

        const u32 parameter_count = u32_at(*raw, l.parameter_count);
        if (parameter_count > kMaxParameters)
            return std::nullopt;
        if (parameter_count != 0)
        {
            const auto rows = space.bytes_at(u32_at(*raw, l.parameters), usize { parameter_count } * 8);
            if (!rows)
                return std::nullopt;
            for (u32 i = 0; i < parameter_count; ++i)
            {
                const auto name = space.cstring_at(u32_at(*rows, usize { i } * 8));
                const auto value = space.cstring_at(u32_at(*rows, usize { i } * 8 + 4));
                if (!name || !value)
                    return std::nullopt;
                item.parameters.push_back({ *name, *value });
            }
        }

        const u32 sound_count = u32_at(*raw, l.sound_count);
        if (sound_count > kMaxSounds)
            return std::nullopt;
        if (sound_count != 0)
        {
            const auto rows = space.bytes_at(u32_at(*raw, l.sounds), usize { sound_count } * kSoundSize);
            if (!rows)
                return std::nullopt;
            u32 total_names = 0;
            for (u32 i = 0; i < sound_count; ++i)
            {
                const usize at = usize { i } * kSoundSize;
                const u32 names = u32_at(*rows, at);
                const u32 slots = u32_at(*rows, at + 4);
                const u32 scripts = u32_at(*rows, at + 8);
                if (names > kMaxSoundNames - total_names)
                    return std::nullopt;
                total_names += names;
                if (names != 0 && !space.bytes_at(slots, usize { names } * 4))
                    return std::nullopt;
                if (scripts > kMaxSoundNames)
                    return std::nullopt;
                const auto script_slots = space.bytes_at(u32_at(*rows, at + 12), usize { scripts } * 4);
                if (scripts != 0 && !script_slots)
                    return std::nullopt;
                SceneryItemSound sound;
                sound.scripts.resize(scripts);
                // A script is steps back to back, each a time and a cue, Volume and Pitch then a value and a
                // time, up to a cue 0. A null slot (zero and not relocated) is a silent animation.
                for (u32 k = 0; k < scripts; ++k)
                {
                    u32 step = u32_at(*script_slots, usize { k } * 4);
                    if (step == 0 && !pointers_.contains(u32_at(*rows, at + 12) + k * 4))
                        continue;
                    for (usize n = 0;; ++n)
                    {
                        const auto head = space.bytes_at(step, 8);
                        if (!head || n == kMaxSoundSteps)
                            return std::nullopt;
                        const u32 cue = u32_at(*head, 4);
                        // A cue past the known ones ends the script rather than lose the item over its sound.
                        if (cue == 0 || cue > static_cast<u32>(SoundCue::Pitch))
                            break;
                        SoundStep s { .at = f32_at(*head, 0), .cue = static_cast<SoundCue>(cue) };
                        step += 8;
                        if (s.cue == SoundCue::Volume || s.cue == SoundCue::Pitch)
                        {
                            const auto change = space.bytes_at(step, 8);
                            if (!change)
                                return std::nullopt;
                            s.value = f32_at(*change, 0);
                            s.until = f32_at(*change, 4);
                            step += 8;
                        }
                        sound.scripts[k].push_back(s);
                    }
                }
                sound.sounds.reserve(names);
                for (u32 k = 0; k < names; ++k)
                    sound.sounds.push_back(refs.find(slots + k * 4, kSoundTag));
                item.sounds.push_back(std::move(sound));
            }
        }

        const u32 research_count = u32_at(*raw, l.research_count);
        if (!read_research(u32_at(*raw, l.research), research_count, item.research))
            return std::nullopt;

        item.unknown_84 = { u32_at(*raw, l.unknown_84), u32_at(*raw, l.unknown_84 + 4) };
        for (usize i = 0; i < 3 && !compact; ++i)
            item.default_colours[i] = u32_at(*raw, l.colours + i * 4);
        for (usize i = 0; i < l.late_words; ++i)
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
            item.unknown_int = static_cast<i32>(u32_at(*raw, 0xe8));
        }
        return item;
    }

    bool SceneryItemReader::read_research(u32 address, u32 count, std::vector<SceneryItemResearch>& out) const
    {
        if (count > kMaxResearch)
            return false;
        if (count == 0)
            return true;
        if (!space_.bytes_at(address, usize { count } * kResearchSize))
            return false;
        for (u32 i = 0; i < count; ++i)
        {
            const auto row = space_.bytes_at(address + i * kResearchSize, kResearchSize);
            if (!row)
                return false;
            SceneryItemResearch r;
            r.category = refs_.find(address + i * kResearchSize, kResearchTag);
            r.unknown = static_cast<i32>(u32_at(*row, 4));
            r.level = f32_at(*row, 8);
            out.push_back(r);
        }
        return true;
    }

    usize SceneryItemReader::stride_of(const SceneryItem& item) noexcept
    {
        usize size = kBaseSize;
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

    std::optional<u32> SceneryItemReader::block_end(u32 address) const noexcept
    {
        const auto where = part_->file.locate(address, part_->base);
        if (!where)
            return std::nullopt;
        const auto& block = part_->file.block_types[where->type].blocks[where->index];
        return address - where->offset + block.size;
    }

    std::optional<std::vector<SceneryItem>> SceneryItemReader::read_run(u32 address) const
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
            const usize stride = stride_of(*item);
            items.push_back(std::move(*item));
            if (stride > *end - address)
                return std::nullopt; // the data does not end with the block
            address += static_cast<u32>(stride);
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
