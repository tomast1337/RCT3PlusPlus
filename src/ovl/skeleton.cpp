// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/skeleton.hpp"

#include "ovl/item_record.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSkeletonTag = "sal";
        constexpr std::string_view kAnimationTag = "san";
        constexpr std::size_t kSkeletonSize = 40;
        constexpr std::uint32_t kMaxBones = 256;
        constexpr std::uint32_t kMaxAnimations = 4096;

        std::optional<Skeleton> read_skeleton(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address)
        {
            const auto record = ItemRecord::open(space, refs, address, kSkeletonSize);
            if (!record)
                return std::nullopt;
            const std::uint32_t bones = record->u32(0);
            const std::uint32_t animations = record->u32(28);
            if (bones > kMaxBones || animations > kMaxAnimations)
                return std::nullopt;

            Skeleton skeleton;
            if (bones != 0)
            {
                const auto names = ItemRecord::open(space, refs, record->u32(4), std::size_t { bones } * 4);
                const auto parents = ItemRecord::open(space, refs, record->u32(8), std::size_t { bones } * 4);
                if (!names || !parents)
                    return std::nullopt;
                for (std::uint32_t i = 0; i < bones; ++i)
                {
                    skeleton.bones.push_back(names->text(std::size_t { i } * 4));
                    const std::int32_t parent = parents->i32(std::size_t { i } * 4);
                    // A bone only ever hangs from one listed before it; anything else is treated as none.
                    skeleton.parents.push_back(parent >= 0 && static_cast<std::uint32_t>(parent) < i ? parent : -1);
                }
            }
            if (animations != 0)
            {
                const auto names = ItemRecord::open(space, refs, record->u32(32), std::size_t { animations } * 4);
                const auto items = ItemRecord::open(space, refs, record->u32(36), std::size_t { animations } * 4);
                if (!names || !items)
                    return std::nullopt;
                for (std::uint32_t i = 0; i < animations; ++i)
                {
                    skeleton.animation_names.push_back(names->text(std::size_t { i } * 4));
                    skeleton.animations.push_back(items->ref(std::size_t { i } * 4, kAnimationTag));
                }
            }
            return skeleton;
        }

    } // namespace

    std::vector<SkeletonItem> read_skeletons(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        std::vector<SkeletonItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kSkeletonTag)
                items.push_back({ parts.name, read_skeleton(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
