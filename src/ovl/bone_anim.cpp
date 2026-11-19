// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/bone_anim.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kAnimTag = "ban";
        constexpr usize kHeaderSize = 12;   // count, pointer, duration
        constexpr usize kBoneSize = 20;     // name, position count + pointer, rotation count + pointer
        constexpr usize kKeyframeSize = 16; // time and three values
        constexpr u32 kMaxCount = 1u << 20;

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

        bool read_track(const AddressSpace& space, u32 count, u32 address, std::vector<BoneKeyframe>& out)
        {
            if (count == 0)
                return true;
            if (count > kMaxCount)
                return false;
            const auto raw = space.bytes_at(address, usize { count } * kKeyframeSize);
            if (!raw)
                return false;
            out.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                const usize at = usize { i } * kKeyframeSize;
                BoneKeyframe key;
                key.time = f32_at(*raw, at);
                for (usize k = 0; k < 3; ++k)
                    key.value[k] = f32_at(*raw, at + 4 + k * 4);
                out.push_back(key);
            }
            return true;
        }

        std::optional<BoneAnim> read_anim(const AddressSpace& space, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            BoneAnim anim;
            const u32 bone_count = u32_at(*header, 0);
            const u32 bone_array = u32_at(*header, 4);
            anim.duration = f32_at(*header, 8);
            if (bone_count > kMaxCount)
                return std::nullopt;

            if (bone_count != 0)
            {
                const auto bones = space.bytes_at(bone_array, usize { bone_count } * kBoneSize);
                if (!bones)
                    return std::nullopt;
                anim.bones.reserve(bone_count);
                for (u32 i = 0; i < bone_count; ++i)
                {
                    const auto raw = bones->subspan(usize { i } * kBoneSize, kBoneSize);
                    BoneTrack bone;
                    if (const auto name = space.cstring_at(u32_at(raw, 0)))
                        bone.name = *name;
                    if (!read_track(space, u32_at(raw, 4), u32_at(raw, 8), bone.positions))
                        return std::nullopt;
                    if (!read_track(space, u32_at(raw, 12), u32_at(raw, 16), bone.rotations))
                        return std::nullopt;
                    anim.bones.push_back(std::move(bone));
                }
            }
            return anim;
        }

    } // namespace

    std::vector<BoneAnimItem> read_bone_anims(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<BoneAnimItem> items;
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
