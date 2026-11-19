// SPDX-License-Identifier: GPL-3.0-or-later
//
// Textures: a small record that binds a texture style to a few parameters. It holds no pixels, images are
// flexi textures.
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl::texture
{

    /// A three-word record some textures point at.
    struct TextureNode
    {
        /// Where the node lives. A flic item has the same address: the node is the start of that flic's
        /// record, which is how a texture finds its pixels.
        u32 address = 0;
        u32 first = 0;  ///< 0 in all files seen
        u32 second = 0; ///< 1 for nearly all textures, up to 32 for a few
        f32 scale = 0;  ///< 1 for nearly all textures, 30 for one
    };

    struct Texture
    {
        /// Sixteen 16-bit values at the start of the record, 7 in every file seen. Purpose not confirmed.
        std::array<u16, 16> table { };
        /// Three words after the table: {0 or 1 or 2, 4 or 8 or 12, 0 or 13 or 16}. The first equals `first`
        /// and the second is 4 + 4 * `first` in every file seen (so they hold no extra information). The
        /// third is 0 for plain textures, 13 for water and cube-map textures and 16 for most others; its use
        /// is not confirmed.
        std::array<u32, 3> parameters { };
        std::string_view style;          ///< Texture style name without the tag. Empty when not referenced.
        u16 first = 0;                   ///< 0, 1 or 2. Nonzero exactly when the texture has a `node`
        u16 count = 0;                   ///< 0, 1 or 2 in shipped files
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

} // namespace rct3::ovl::texture