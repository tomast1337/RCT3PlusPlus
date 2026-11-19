// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/person_parts.hpp"

#include "core/types.hpp"
#include "ovl/item_record.hpp"

#include <bit>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSectionTag = "ppg";
        constexpr std::string_view kPieceTag = "prt";
        constexpr std::string_view kHeadTag = "phd";
        constexpr std::string_view kTextureTag = "tex";
        constexpr std::string_view kMeshTag = "mms";
        constexpr std::string_view kBonesTag = "sal";
        constexpr std::string_view kSkinTag = "ssk";

        constexpr usize kSectionSize = 84;
        constexpr usize kPieceSize = 20;
        constexpr usize kHeadSize = 192;

        // Within a section.
        constexpr usize kAgeGroup = 12;
        constexpr usize kSex = 13;
        constexpr usize kKind = 14;
        constexpr usize kOptionalCount = 15;
        constexpr usize kChances = 20;
        constexpr usize kLevels = 28;
        constexpr usize kLevelSize = 8; ///< Fixed count, optional count, two spare bytes, list pointer
        constexpr usize kHead = 52;
        constexpr usize kSwimLevels = 56;
        constexpr usize kSwimHead = 80;

        // Within a head.
        constexpr usize kHeadTexture = 136;
        constexpr usize kHeadBones = 140;
        constexpr usize kHeadSkins = 144;

        u8 byte_at(const ItemRecord& record, usize offset)
        {
            const auto bytes = record.bytes();
            return offset < bytes.size() ? std::to_integer<u8>(bytes[offset]) : 0;
        }

        /// Reads the piece list at `offset` of `record`. A level that lists nothing may hold any pointer, so it
        /// is never followed.
        std::optional<SectionLevel> read_level(const AddressSpace& space, const ReferenceIndex& refs, const ItemRecord& record, usize offset)
        {
            SectionLevel level;
            const u32 fixed = byte_at(record, offset);
            const u32 optional = byte_at(record, offset + 1);
            const u32 count = fixed + optional;
            if (count == 0)
                return level;
            const auto list = ItemRecord::open(space, refs, record.read_u32(offset + 4), usize { count } * 4);
            if (!list)
                return std::nullopt;
            for (u32 i = 0; i < count; ++i)
                (i < fixed ? level.fixed : level.optional).push_back(list->ref(usize { i } * 4, kPieceTag));
            return level;
        }

        std::optional<BodySection> read_section(const AddressSpace& space, const ReferenceIndex& refs, u32 address)
        {
            const auto record = ItemRecord::open(space, refs, address, kSectionSize);
            if (!record)
                return std::nullopt;
            BodySection section;
            section.age_group = byte_at(*record, kAgeGroup);
            section.sex = byte_at(*record, kSex);
            section.kind = byte_at(*record, kKind);
            if (const u32 chances = byte_at(*record, kOptionalCount); chances != 0)
            {
                const auto raw = space.bytes_at(record->read_u32(kChances), usize { chances } * 4);
                if (!raw)
                    return std::nullopt;
                for (u32 i = 0; i < chances; ++i)
                {
                    u32 bits = 0;
                    for (usize b = 0; b < 4; ++b)
                        bits |= std::to_integer<u32>((*raw)[usize { i } * 4 + b]) << (8 * b);
                    section.optional_chances.push_back(std::bit_cast<f32>(bits));
                }
            }
            for (usize i = 0; i < kSectionLevels; ++i)
            {
                auto level = read_level(space, refs, *record, kLevels + i * kLevelSize);
                auto swim = read_level(space, refs, *record, kSwimLevels + i * kLevelSize);
                if (!level || !swim)
                    return std::nullopt;
                section.levels[i] = std::move(*level);
                section.swim_levels[i] = std::move(*swim);
            }
            section.head = record->ref(kHead, kHeadTag);
            section.swim_head = record->ref(kSwimHead, kHeadTag);
            return section;
        }

        std::optional<BodyPiece> read_piece(const AddressSpace& space, const ReferenceIndex& refs, u32 address)
        {
            const auto record = ItemRecord::open(space, refs, address, kPieceSize);
            if (!record)
                return std::nullopt;
            return BodyPiece { .texture = record->ref(8, kTextureTag), .mesh = record->ref(12, kMeshTag), .channel = record->read_u32(16) };
        }

        std::optional<HeadRig> read_head(const AddressSpace& space, const ReferenceIndex& refs, u32 address)
        {
            const auto record = ItemRecord::open(space, refs, address, kHeadSize);
            if (!record)
                return std::nullopt;
            HeadRig head;
            head.texture = record->ref(kHeadTexture, kTextureTag);
            head.bones = record->ref(kHeadBones, kBonesTag);
            for (usize i = 0; i < head.skins.size(); ++i)
                head.skins[i] = record->ref(kHeadSkins + i * 4, kSkinTag);
            return head;
        }

        template<typename Item, typename Read> std::vector<Item> read_all(const Overlay& overlay, const Part& part, std::string_view tag, Read read)
        {
            const AddressSpace space = overlay.space();
            const ReferenceIndex refs(part, space);
            std::vector<Item> items;
            for (const auto& symbol : part.symbols)
            {
                if (!symbol.is_pointer)
                    continue;
                const SymbolName parts = split_symbol_name(symbol.name);
                if (parts.tag == tag)
                    items.push_back({ parts.name, read(space, refs, symbol.data) });
            }
            return items;
        }

    } // namespace

    std::vector<BodySectionItem> read_body_sections(const Overlay& overlay, const Part& part)
    {
        return read_all<BodySectionItem>(overlay, part, kSectionTag, read_section);
    }

    std::vector<BodyPieceItem> read_body_pieces(const Overlay& overlay, const Part& part)
    {
        return read_all<BodyPieceItem>(overlay, part, kPieceTag, read_piece);
    }

    std::vector<HeadRigItem> read_head_rigs(const Overlay& overlay, const Part& part)
    {
        return read_all<HeadRigItem>(overlay, part, kHeadTag, read_head);
    }

} // namespace rct3::ovl
