// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/scenery_visual.hpp"

#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kVisualTag = "svd";
        constexpr std::string_view kShapeTag = "shs";
        constexpr std::string_view kBoneShapeTag = "bsh";
        constexpr std::string_view kFlexiTag = "ftx";
        constexpr std::string_view kStyleTag = "txs";
        constexpr std::string_view kAnimationTag = "ban";
        constexpr std::size_t kHeaderSize = 28; // seven 32-bit words
        constexpr std::size_t kLodSize = 72;    // eighteen 32-bit words
        constexpr std::uint32_t kMaxCount = 1u << 16;

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

        std::optional<SceneryVisualLod> read_lod(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kLodSize);
            if (!raw)
                return std::nullopt;

            SceneryVisualLod lod;
            lod.mesh_type = u32_at(*raw, 0);
            if (const auto name = space.cstring_at(u32_at(*raw, 4)))
                lod.name = *name;
            lod.static_shape = refs.find(address + 8, kShapeTag);
            lod.bone_shape = refs.find(address + 16, kBoneShapeTag);
            lod.texture = refs.find(address + 24, kFlexiTag);
            lod.style = refs.find(address + 28, kStyleTag);
            lod.width = f32_at(*raw, 32);
            lod.height = f32_at(*raw, 36);
            for (std::size_t i = 0; i < 4; ++i)
                lod.texcoords[i] = f32_at(*raw, 40 + i * 4);
            lod.distance = f32_at(*raw, 56);

            const std::uint32_t animation_count = u32_at(*raw, 60);
            if (animation_count > kMaxCount)
                return std::nullopt;
            if (animation_count != 0)
            {
                // Each entry points at a one-word field that holds the animation reference.
                const auto entries = space.bytes_at(u32_at(*raw, 68), std::size_t { animation_count } * 4);
                if (!entries)
                    return std::nullopt;
                lod.animations.reserve(animation_count);
                for (std::uint32_t i = 0; i < animation_count; ++i)
                {
                    lod.animations.push_back(refs.find(u32_at(*entries, std::size_t { i } * 4), kAnimationTag));
                }
            }
            return lod;
        }

        std::optional<SceneryVisual> read_visual(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            SceneryVisual visual;
            visual.flags = u32_at(*header, 0);
            visual.sway = f32_at(*header, 4);
            visual.brightness = f32_at(*header, 8);
            visual.scale_variation = f32_at(*header, 16);
            const std::uint32_t lod_count = u32_at(*header, 20);
            if (lod_count > kMaxCount)
                return std::nullopt;

            if (lod_count != 0)
            {
                const auto pointers = space.bytes_at(u32_at(*header, 24), std::size_t { lod_count } * 4);
                if (!pointers)
                    return std::nullopt;
                visual.lods.reserve(lod_count);
                for (std::uint32_t i = 0; i < lod_count; ++i)
                {
                    auto lod = read_lod(space, refs, u32_at(*pointers, std::size_t { i } * 4));
                    if (!lod)
                        return std::nullopt;
                    visual.lods.push_back(std::move(*lod));
                }
            }
            return visual;
        }

    } // namespace

    std::vector<SceneryVisualItem> read_scenery_visuals(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);

        std::vector<SceneryVisualItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kVisualTag)
                continue;
            items.push_back({ parts.name, read_visual(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
