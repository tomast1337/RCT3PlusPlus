// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/frame_mesh.hpp"

#include "core/types.hpp"

#include <bit>
#include <cstdint>
#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kMeshTag = "mms";
        constexpr usize kHeaderSize = 40;
        constexpr usize kCornerSize = 12;
        constexpr usize kAnimationSize = 64;
        constexpr u32 kMaxElements = 1u << 20;
        constexpr u32 kMaxAnimations = 4096;

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        f32 f32_at(std::span<const std::byte> b, usize at)
        {
            return std::bit_cast<f32>(u32_at(b, at));
        }

        constexpr usize kSocketKeySize = 16;

        /// The point a key packs into `word`, eleven bits across, ten up and eleven along from the top bit down.
        std::array<f32, 3> unpack_point(const FrameAnimation& animation, u32 word)
        {
            const std::array<u32, 3> steps { word >> 21, (word >> 11) & 0x3FF, word & 0x7FF };
            std::array<f32, 3> point { };
            for (usize c = 0; c < 3; ++c)
                point[c] = animation.offset[c] + static_cast<f32>(steps[c]) * animation.scale[c];
            return point;
        }

        std::optional<FrameAnimation> read_animation(const AddressSpace& space, std::span<const std::byte> raw, usize key_size, bool socket)
        {
            FrameAnimation animation;
            for (usize c = 0; c < 3; ++c)
            {
                animation.offset[c] = f32_at(raw, c * 4);
                animation.scale[c] = f32_at(raw, 16 + c * 4);
            }
            if (const auto name = space.cstring_at(u32_at(raw, 32)))
                animation.name = *name;
            const u32 keys = u32_at(raw, 36);
            if (keys > kMaxElements)
                return std::nullopt;
            if (keys == 0)
                return animation;
            const auto times = space.bytes_at(u32_at(raw, 40), usize { keys } * 4);
            const auto data = space.bytes_at(u32_at(raw, 44), keys * key_size);
            if (!times || !data)
                return std::nullopt;
            animation.times.resize(keys);
            std::memcpy(animation.times.data(), times->data(), times->size());
            animation.keys.assign(data->begin(), data->end());
            // A socket without its track still marks a place, by its triangle.
            if (const auto track = socket ? space.bytes_at(u32_at(raw, 48), keys * kSocketKeySize) : std::nullopt)
            {
                for (usize k = 0; k < keys; ++k)
                {
                    const usize at = k * kSocketKeySize;
                    animation.sockets.push_back({ { f32_at(*track, at), f32_at(*track, at + 4), f32_at(*track, at + 8) }, unpack_point(animation, u32_at(*track, at + 12)) });
                }
            }
            return animation;
        }

        std::optional<FrameMesh> read_mesh(const AddressSpace& space, u32 address)
        {
            const auto header = space.bytes_at(address, kHeaderSize);
            if (!header)
                return std::nullopt;
            FrameMesh mesh;
            const u32 corners = u32_at(*header, 0);
            const u32 indices = u32_at(*header, 4);
            mesh.points = u32_at(*header, 8);
            mesh.normals = u32_at(*header, 12);
            mesh.tracks = u32_at(*header, 16);
            const u32 animations = u32_at(*header, 20);
            if (corners > kMaxElements || indices > kMaxElements || mesh.points > kMaxElements || mesh.normals > kMaxElements || animations > kMaxAnimations)
                return std::nullopt;

            if (corners != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 24), usize { corners } * kCornerSize);
                if (!raw)
                    return std::nullopt;
                mesh.corners.resize(corners);
                for (u32 i = 0; i < corners; ++i)
                {
                    const usize at = usize { i } * kCornerSize;
                    const u32 pair = u32_at(*raw, at);
                    mesh.corners[i] = { static_cast<u16>(pair & 0xFFFF), static_cast<u16>(pair >> 16), { f32_at(*raw, at + 4), f32_at(*raw, at + 8) } };
                }
            }
            if (indices != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 28), usize { indices } * 2);
                if (!raw)
                    return std::nullopt;
                mesh.indices.resize(indices);
                std::memcpy(mesh.indices.data(), raw->data(), raw->size());
            }
            if (animations != 0)
            {
                const auto raw = space.bytes_at(u32_at(*header, 36), usize { animations } * kAnimationSize);
                if (!raw)
                    return std::nullopt;
                for (u32 i = 0; i < animations; ++i)
                {
                    auto animation = read_animation(space, raw->subspan(usize { i } * kAnimationSize, kAnimationSize), mesh.key_size(), mesh.tracks != 0);
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

    FrameKey decode_key(const FrameMesh& mesh, const FrameAnimation& animation, usize key)
    {
        FrameKey out;
        const usize size = mesh.key_size();
        if (key >= animation.times.size() || (key + 1) * size > animation.keys.size())
            return out;
        const std::span<const std::byte> data = std::span(animation.keys).subspan(key * size, size);
        out.points.reserve(mesh.points);
        for (u32 i = 0; i < mesh.points; ++i)
        {
            out.points.push_back(unpack_point(animation, u32_at(data, usize { i } * 4)));
        }
        out.normals.reserve(mesh.normals);
        const usize normals = usize { mesh.points } * 4;
        for (u32 i = 0; i < mesh.normals; ++i)
        {
            std::array<f32, 3> normal { };
            for (usize c = 0; c < 3; ++c)
                normal[c] = (static_cast<f32>(std::to_integer<i32>(data[normals + usize { i } * 3 + c])) - 127.5f) / 127.5f;
            out.normals.push_back(normal);
        }
        return out;
    }

} // namespace rct3::ovl
