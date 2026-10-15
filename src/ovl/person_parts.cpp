// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/person_parts.hpp"

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

        constexpr std::size_t kSectionSize = 84;
        constexpr std::size_t kPieceSize = 20;
        constexpr std::size_t kHeadSize = 192;

        // Within a section.
        constexpr std::size_t kAgeGroup = 12;
        constexpr std::size_t kSex = 13;
        constexpr std::size_t kKind = 14;
        constexpr std::size_t kOptionalCount = 15;
        constexpr std::size_t kChances = 20;
        constexpr std::size_t kLevels = 28;
        constexpr std::size_t kLevelSize = 8; ///< Fixed count, optional count, two spare bytes, list pointer
        constexpr std::size_t kHead = 52;
        constexpr std::size_t kSwimLevels = 56;
        constexpr std::size_t kSwimHead = 80;

        // Within a head.
        constexpr std::size_t kHeadTexture = 136;
        constexpr std::size_t kHeadBones = 140;
        constexpr std::size_t kHeadSkins = 144;

        std::uint8_t byte_at(const ItemRecord& record, std::size_t offset)
        {
            const auto bytes = record.bytes();
            return offset < bytes.size() ? std::to_integer<std::uint8_t>(bytes[offset]) : 0;
        }

        /// Reads the piece list at `offset` of `record`. A level that lists nothing may hold any pointer, so it
        /// is never followed.
        std::optional<SectionLevel> read_level(const AddressSpace& space, const ReferenceIndex& refs, const ItemRecord& record, std::size_t offset)
        {
            SectionLevel level;
            const std::uint32_t fixed = byte_at(record, offset);
            const std::uint32_t optional = byte_at(record, offset + 1);
            const std::uint32_t count = fixed + optional;
            if (count == 0)
                return level;
            const auto list = ItemRecord::open(space, refs, record.u32(offset + 4), std::size_t { count } * 4);
            if (!list)
                return std::nullopt;
            for (std::uint32_t i = 0; i < count; ++i)
                (i < fixed ? level.fixed : level.optional).push_back(list->ref(std::size_t { i } * 4, kPieceTag));
            return level;
        }

        std::optional<BodySection> read_section(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address)
        {
            const auto record = ItemRecord::open(space, refs, address, kSectionSize);
            if (!record)
                return std::nullopt;
            BodySection section;
            section.age_group = byte_at(*record, kAgeGroup);
            section.sex = byte_at(*record, kSex);
            section.kind = byte_at(*record, kKind);
            if (const std::uint32_t chances = byte_at(*record, kOptionalCount); chances != 0)
            {
                const auto raw = space.bytes_at(record->u32(kChances), std::size_t { chances } * 4);
                if (!raw)
                    return std::nullopt;
                for (std::uint32_t i = 0; i < chances; ++i)
                {
                    std::uint32_t bits = 0;
                    for (std::size_t b = 0; b < 4; ++b)
                        bits |= std::to_integer<std::uint32_t>((*raw)[std::size_t { i } * 4 + b]) << (8 * b);
                    section.optional_chances.push_back(std::bit_cast<float>(bits));
                }
            }
            for (std::size_t i = 0; i < kSectionLevels; ++i)
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

        std::optional<BodyPiece> read_piece(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address)
        {
            const auto record = ItemRecord::open(space, refs, address, kPieceSize);
            if (!record)
                return std::nullopt;
            return BodyPiece { .texture = record->ref(8, kTextureTag), .mesh = record->ref(12, kMeshTag), .channel = record->u32(16) };
        }

        std::optional<HeadRig> read_head(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address)
        {
            const auto record = ItemRecord::open(space, refs, address, kHeadSize);
            if (!record)
                return std::nullopt;
            HeadRig head;
            head.texture = record->ref(kHeadTexture, kTextureTag);
            head.bones = record->ref(kHeadBones, kBonesTag);
            for (std::size_t i = 0; i < head.skins.size(); ++i)
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
