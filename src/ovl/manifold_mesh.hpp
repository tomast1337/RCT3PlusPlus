// SPDX-License-Identifier: GPL-3.0-or-later
//
// Manifold meshes: simple triangle meshes with positions only, used as collision proxies.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    struct ManifoldMesh
    {
        /// Bounding box. Equals the extent of the vertices in every item seen.
        std::array<float, 3> min { };
        std::array<float, 3> max { };
        std::vector<std::array<float, 3>> vertices;
        std::vector<std::uint16_t> indices; ///< Three per triangle
    };

    struct ManifoldMeshItem
    {
        std::string_view name;            ///< Symbol name without the tag
        std::optional<ManifoldMesh> mesh; ///< Nothing when any part of the structure is unreadable
    };

    /// Manifold meshes of one part of an overlay: pointer symbols tagged "mam". Vertices are stored as four
    /// floats with a zero fourth component, which is dropped here.
    [[nodiscard]] std::vector<ManifoldMeshItem> read_manifold_meshes(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
