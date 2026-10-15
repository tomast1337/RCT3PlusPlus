// SPDX-License-Identifier: GPL-3.0-or-later
//
// Simple skins: small triangle meshes in rigid parts, used for people's heads. Each part is carried by one
// bone of the skeleton that animates the head (see skeleton.hpp): part k by bone k, its vertices and normals
// given in that bone's own space. A triangle may join corners of two parts, which stretches it as they move.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct SimpleSkinVertex
    {
        std::array<float, 3> position { };
        std::array<float, 3> normal { }; ///< Unit length in files seen
        std::array<float, 2> uv { };
    };

    struct SimpleSkin
    {
        /// One number per part, the same in every skin of a head. They name the part's bone but are not
        /// needed to find it: parts follow the skeleton's bone order.
        std::vector<std::uint32_t> part_ids;
        /// Vertices of each part. The sum is the vertex count in every item seen.
        std::vector<std::uint32_t> part_vertex_counts;
        std::vector<SimpleSkinVertex> vertices;
        std::vector<std::uint16_t> indices; ///< Three per triangle
    };

    struct SimpleSkinItem
    {
        std::string_view name;          ///< Symbol name without the tag
        std::optional<SimpleSkin> skin; ///< Nothing when any part of the structure is unreadable
    };

    /// Simple skins of one part of an overlay: pointer symbols tagged "ssk".
    [[nodiscard]] std::vector<SimpleSkinItem> read_simple_skins(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
