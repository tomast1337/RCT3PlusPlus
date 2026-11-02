// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame meshes: meshes that move by storing every point again at each key of each animation, with no
// bones. People's clothes and bodies are built from them.
//
// The mesh keeps one list of corners. Each corner names a point and a normal by index and carries its own
// texture coordinates; the points and normals themselves change from key to key. A key stores every point
// in one 32-bit word, eleven bits across, ten bits up and eleven bits along (from the top bit down), each a
// step count from the animation's offset in steps of the animation's scale. Then every normal in three
// bytes, each mapping 0..255 onto -1..1, with the key padded to whole words.
//
// A few meshes carry tracks instead of a shape: sockets where a hat or a held item would sit. Their own
// triangle is only a placeholder. Each key of a socket's track is sixteen bytes: a turn as three floats, a
// vector along its axis as long as its angle in radians, then where the socket is, packed as a point is.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct FrameCorner
    {
        std::uint16_t point = 0;
        std::uint16_t normal = 0;
        std::array<float, 2> uv { };
    };

    /// Where a socket is at one key, and how it is turned.
    struct SocketKey
    {
        std::array<float, 3> turn { }; ///< Along the turn's axis, as long as its angle in radians
        std::array<float, 3> at { };
    };

    struct FrameAnimation
    {
        std::string_view name;
        std::array<float, 3> offset { };
        std::array<float, 3> scale { };
        /// The time each key after the first is reached; the first key is at time 0 and the last time is
        /// the animation's length, where a repeating animation comes back to its first key.
        std::vector<float> times;
        std::vector<std::byte> keys; ///< `times.size()` keys, back to back, copied out of the file
        /// For a socket, its first track's key at each of `times`; empty for other meshes.
        std::vector<SocketKey> sockets;
    };

    struct FrameMesh
    {
        std::vector<FrameCorner> corners;
        std::vector<std::uint16_t> indices; ///< Three corners per triangle
        std::uint32_t points = 0;
        std::uint32_t normals = 0;
        std::uint32_t tracks = 0; ///< Above 0 for sockets, which are not drawn
        std::vector<FrameAnimation> animations;

        /// Bytes of one key.
        [[nodiscard]] std::size_t key_size() const noexcept
        {
            return std::size_t { points } * 4 + ((std::size_t { normals } + 1) * 3 & ~std::size_t { 3 });
        }
    };

    struct FrameMeshItem
    {
        std::string_view name;
        std::optional<FrameMesh> mesh;
    };

    /// Frame meshes of one part of an overlay: pointer symbols tagged "mms". Names are views into the overlay.
    [[nodiscard]] std::vector<FrameMeshItem> read_frame_meshes(const Overlay& overlay, const Part& part);

    /// The points and normals of key `key` of `animation`. Empty when the key is out of range.
    struct FrameKey
    {
        std::vector<std::array<float, 3>> points;
        std::vector<std::array<float, 3>> normals;
    };
    [[nodiscard]] FrameKey decode_key(const FrameMesh& mesh, const FrameAnimation& animation, std::size_t key);

} // namespace rct3::ovl
