// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/spline.hpp"

#include <cstring>
#include <span>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSplineTag = "spl";
        constexpr std::size_t kHeaderSize = 32; // count, nodes, flag, length, 1/length, lengths, trailer, height
        constexpr std::size_t kNodeSize = 36;   // nine floats
        constexpr std::uint32_t kMaxCount = 1u << 20;

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

        std::optional<Spline> read_spline(const AddressSpace& space, std::uint32_t address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            const std::uint32_t count = u32_at(*header, 0);
            if (count > kMaxCount)
                return std::nullopt;

            Spline spline;
            spline.closed = u32_at(*header, 8) != 0;
            spline.length = f32_at(*header, 12);
            spline.max_height = f32_at(*header, 28);

            const auto nodes = space.bytes_at(u32_at(*header, 4), std::size_t { count } * kNodeSize);
            if (!nodes)
                return std::nullopt;
            spline.nodes.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::size_t at = std::size_t { i } * kNodeSize;
                SplineNode node;
                for (std::size_t k = 0; k < 3; ++k)
                {
                    node.position[k] = f32_at(*nodes, at + k * 4);
                    node.tangent_in[k] = f32_at(*nodes, at + 12 + k * 4);
                    node.tangent_out[k] = f32_at(*nodes, at + 24 + k * 4);
                }
                spline.nodes.push_back(node);
            }

            const std::uint32_t segments = count == 0 ? 0 : (spline.closed ? count : count - 1);
            if (segments != 0)
            {
                const auto lengths = space.bytes_at(u32_at(*header, 20), std::size_t { segments } * 4);
                if (!lengths)
                    return std::nullopt;
                spline.segment_lengths.reserve(segments);
                for (std::uint32_t i = 0; i < segments; ++i)
                    spline.segment_lengths.push_back(f32_at(*lengths, i * 4));
            }
            return spline;
        }

    } // namespace

    std::vector<SplineItem> read_splines(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<SplineItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kSplineTag)
                continue;
            items.push_back({ parts.name, read_spline(space, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
