// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/frame_mesh.hpp"

#include <bit>
#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kMeshTag = "mms";
        constexpr std::size_t kHeaderSize = 40;
        constexpr std::size_t kCornerSize = 12;
        constexpr std::size_t kAnimationSize = 64;
        constexpr std::uint32_t kMaxElements = 1u << 20;
        constexpr std::uint32_t kMaxAnimations = 4096;

        std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        float f32_at(std::span<const std::byte> b, std::size_t at)
        {
            return std::bit_cast<float>(u32_at(b, at));
        }

        std::optional<FrameAnimation> read_animation(const AddressSpace& space, std::span<const std::byte> raw, std::size_t key_size)
        {
            FrameAnimation animation;
            for (std::size_t c = 0; c < 3; ++c)
            {
                animation.offset[c] = f32_at(raw, c * 4);
                animation.scale[c] = f32_at(raw, 16 + c * 4);
            }
            if (const auto name = space.cstring_at(u32_at(raw, 32)))
                animation.name = *name;
            const std::uint32_t keys = u32_at(raw, 36);
            if (keys > kMaxElements)
                return std::nullopt;
            if (keys == 0)
                return animation;
            const auto times = space.bytes_at(u32_at(raw, 40), std::size_t { keys } * 4);
            const auto data = space.bytes_at(u32_at(raw, 44), keys * key_size);
            if (!times || !data)
                return std::nullopt;
            animation.times.resize(keys);
            std::memcpy(animation.times.data(), times->data(), times->size());
            animation.keys.assign(data->begin(), data->end());
            return animation;
        }

        std::optional<FrameMesh> read_mesh(const AddressSpace& space, std::uint32_t address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;
            FrameMesh mesh;
            const std::uint32_t corners = u32_at(*header, 0);
            const std::uint32_t indices = u32_at(*header, 4);
            mesh.points = u32_at(*header, 8);
            mesh.normals = u32_at(*header, 12);
            mesh.tracks = u32_at(*header, 16);
            const std::uint32_t animations = u32_at(*header, 20);
            if (corners > kMaxElements || indices > kMaxElements || mesh.points > kMaxElements || mesh.normals > kMaxElements || animations > kMaxAnimations)
                return std::nullopt;

            if (corners != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 24), std::size_t { corners } * kCornerSize);
                if (!raw)
                    return std::nullopt;
                mesh.corners.resize(corners);
                for (std::uint32_t i = 0; i < corners; ++i)
                {
                    const std::size_t at = std::size_t { i } * kCornerSize;
                    const std::uint32_t pair = u32_at(*raw, at);
                    mesh.corners[i] = { static_cast<std::uint16_t>(pair & 0xFFFF), static_cast<std::uint16_t>(pair >> 16), { f32_at(*raw, at + 4), f32_at(*raw, at + 8) } };
                }
            }
            if (indices != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 28), std::size_t { indices } * 2);
                if (!raw)
                    return std::nullopt;
                mesh.indices.resize(indices);
                std::memcpy(mesh.indices.data(), raw->data(), raw->size());
            }
            if (animations != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 36), std::size_t { animations } * kAnimationSize);
                if (!raw)
                    return std::nullopt;
                for (std::uint32_t i = 0; i < animations; ++i)
                {
                    auto animation = read_animation(space, raw->subspan(std::size_t { i } * kAnimationSize, kAnimationSize), mesh.key_size());
                    if (!animation)
                        return std::nullopt;
                    mesh.animations.push_back(std::move(*animation));
                }
            }
            return mesh;
        }

    } // namespace

    std::vector<FrameMeshItem> read_frame_meshes(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<FrameMeshItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kMeshTag)
                items.push_back({ parts.name, read_mesh(space, symbol.data) });
        }
        return items;
    }

    FrameKey decode_key(const FrameMesh& mesh, const FrameAnimation& animation, std::size_t key)
    {
        FrameKey out;
        const std::size_t size = mesh.key_size();
        if (key >= animation.times.size() || (key + 1) * size > animation.keys.size())
            return out;
        const std::span<const std::byte> data = std::span(animation.keys).subspan(key * size, size);
        out.points.reserve(mesh.points);
        for (std::uint32_t i = 0; i < mesh.points; ++i)
        {
            const std::uint32_t word = u32_at(data, std::size_t { i } * 4);
            const std::array<std::uint32_t, 3> steps { word >> 21, (word >> 11) & 0x3FF, word & 0x7FF };
            std::array<float, 3> point { };
            for (std::size_t c = 0; c < 3; ++c)
                point[c] = animation.offset[c] + static_cast<float>(steps[c]) * animation.scale[c];
            out.points.push_back(point);
        }
        out.normals.reserve(mesh.normals);
        const std::size_t normals = std::size_t { mesh.points } * 4;
        for (std::uint32_t i = 0; i < mesh.normals; ++i)
        {
            std::array<float, 3> normal { };
            for (std::size_t c = 0; c < 3; ++c)
                normal[c] = (static_cast<float>(std::to_integer<int>(data[normals + std::size_t { i } * 3 + c])) - 127.5f) / 127.5f;
            out.normals.push_back(normal);
        }
        return out;
    }

} // namespace rct3::ovl
