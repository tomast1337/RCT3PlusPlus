// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/particle_skin.hpp"

#include "ovl/reference_names.hpp"

#include <bit>
#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSkinTag = "psi";
        constexpr std::string_view kTextureTag = "tex";
        // Frame count, frame list pointer, texture reference, blended flag, single-frame flag, loop time, unused
        constexpr std::size_t kItemSize = 28;
        constexpr std::uint32_t kMaxFrames = 1024; // Real skins have at most a few dozen

        std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        std::optional<ParticleSkin> read_skin(const AddressSpace& space, const ReferenceNames& refs, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kItemSize);
            if (!raw)
                return std::nullopt;
            const std::uint32_t count = u32_at(*raw, 0);
            if (count > kMaxFrames)
                return std::nullopt;
            const auto list = space.bytes_at(u32_at(*raw, 4), count * 4);
            if (!list)
                return std::nullopt;

            ParticleSkin skin {
                .texture = refs.find(address + 8, kTextureTag),
                .frames = { },
                .blended = u32_at(*raw, 12) != 0,
                .animated = u32_at(*raw, 16) == 0,
                .loop_time = std::bit_cast<float>(u32_at(*raw, 20)),
            };
            skin.frames.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const auto frame = space.bytes_at(u32_at(*list, i * 4), 16);
                if (!frame)
                    return std::nullopt;
                std::array<float, 4> r { };
                std::memcpy(r.data(), frame->data(), sizeof r);
                skin.frames.push_back(r);
            }
            return skin;
        }

    } // namespace

    std::vector<ParticleSkinItem> read_particle_skins(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);

        std::vector<ParticleSkinItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kSkinTag)
                continue;
            items.push_back({ parts.name, read_skin(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
