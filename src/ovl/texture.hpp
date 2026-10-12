// SPDX-License-Identifier: GPL-3.0-or-later
//
// Textures: a small record that binds a texture style to a few parameters. It holds no pixels, images are
// flexi textures.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// A three-word record some textures point at.
    struct TextureNode
    {
        /// Where the node lives. A flic item has the same address: the node is the start of that flic's
        /// record, which is how a texture finds its pixels.
        std::uint32_t address = 0;
        std::uint32_t first = 0;  ///< 0 in all files seen
        std::uint32_t second = 0; ///< 1 for nearly all textures, up to 32 for a few
        float scale = 0;          ///< 1 for nearly all textures, 30 for one
    };

    struct Texture
    {
        /// Sixteen 16-bit values at the start of the record, 7 in every file seen. Purpose unknown.
        std::array<std::uint16_t, 16> table { };
        /// Three words after the table: {0 or 1 or 2, 4 or 8 or 12, 0 or 13 or 16}. Purpose unknown.
        std::array<std::uint32_t, 3> parameters { };
        std::string_view style;          ///< Texture style name without the tag. Empty when not referenced.
        std::uint16_t first = 0;         ///< 0, 1 or 2
        std::uint16_t count = 0;         ///< 0, 1 or 2. The engine treats values above 1 specially.
        std::optional<TextureNode> node; ///< Absent in the few textures whose `parameters[1]` is 4
    };

    struct TextureItem
    {
        std::string_view name;          ///< Symbol name without the tag
        std::optional<Texture> texture; ///< Nothing when any part of the structure is unreadable
    };

    /// Textures of one part of an overlay: pointer symbols tagged "tex". Views in the result point into the
    /// overlay's bytes. Two old-format items in one file use a different record and read as unreadable.
    [[nodiscard]] std::vector<TextureItem> read_textures(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
