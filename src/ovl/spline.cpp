// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/spline.hpp"

#include "core/types.hpp"

#include <cstring>
#include <span>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSplineTag = "spl";
        constexpr usize kHeaderSize = 32; // count, nodes, flag, length, 1/length, lengths, parameters, height
        constexpr usize kNodeSize = 36;   // nine floats
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

        std::optional<Spline> read_spline(const AddressSpace& space, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;

            const u32 count = u32_at(*header, 0);
            if (count > kMaxCount)
                return std::nullopt;

            Spline spline;
            spline.closed = u32_at(*header, 8) != 0;
            spline.length = f32_at(*header, 12);
            spline.max_height = f32_at(*header, 28);

            const auto nodes = space.bytes_at(u32_at(*header, 4), usize { count } * kNodeSize);
            if (!nodes)
                return std::nullopt;
            spline.nodes.reserve(count);
            for (u32 i = 0; i < count; ++i)
            {
                const usize at = usize { i } * kNodeSize;
                SplineNode node;
                for (usize k = 0; k < 3; ++k)
                {
                    node.position[k] = f32_at(*nodes, at + k * 4);
                    node.tangent_in[k] = f32_at(*nodes, at + 12 + k * 4);
                    node.tangent_out[k] = f32_at(*nodes, at + 24 + k * 4);
                }
                spline.nodes.push_back(node);
            }

            const u32 segments = count == 0 ? 0 : (spline.closed ? count : count - 1);
            if (segments != 0)
            {
                const auto lengths = space.bytes_at(u32_at(*header, 20), usize { segments } * 4);
                if (!lengths)
                    return std::nullopt;
                spline.segment_lengths.reserve(segments);
                for (u32 i = 0; i < segments; ++i)
                    spline.segment_lengths.push_back(f32_at(*lengths, i * 4));
                if (const auto marks = space.bytes_at(u32_at(*header, 24), usize { segments } * kLengthMarks))
                {
                    spline.segment_parameters.resize(segments);
                    std::memcpy(spline.segment_parameters.data(), marks->data(), marks->size());
                }
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

    std::array<f32, 3> spline_point(const Spline& spline, usize segment, f32 t)
    {
        const SplineNode& a = spline.nodes[segment];
        const SplineNode& b = spline.nodes[segment + 1];
        const f32 u = 1 - t;
        std::array<f32, 3> out { };
        for (usize k = 0; k < 3; ++k)
            out[k] = u * u * u * a.position[k] + 3 * u * u * t * (a.position[k] + a.tangent_out[k]) + 3 * u * t * t * (b.position[k] + b.tangent_in[k]) + t * t * t * b.position[k];
        return out;
    }

} // namespace rct3::ovl
