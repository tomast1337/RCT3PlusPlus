// SPDX-License-Identifier: GPL-3.0-or-later
//
// Flics: small image and frame-list items. They have no symbol name. Each one is a loader row whose
// extra data chunk holds its payload.
#pragma once

#include "ovl/overlay.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::ovl
{

    /// Leading fields of a revision 2 flic's chunk, which holds a block-compressed or raw image.
    struct FlicImageHeader
    {
        std::uint32_t format = 0; ///< 2 (raw, 4 bytes per pixel), 18 or 19 (block compressed, 8 or 16 byte blocks)
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t level_count = 0; ///< Levels of the mip chain, 1 for a single image
        std::uint32_t row_bytes = 0;   ///< Bytes of one row of the first level (a row of blocks when compressed)
        std::uint32_t row_count = 0;   ///< Rows of the first level (height, or height / 4 when compressed)
    };

    struct Flic
    {
        std::uint32_t revision = 0;    ///< Format revision of the handler that declares the flic: 2 or 3
        std::uint32_t frame_count = 0; ///< 1 for every revision 2 flic
        float rate = 0;                ///< 1.0 in all files seen but one animation, which has 30.0
        /// The item's extra data chunk. For revision 2 it is the 32-byte image header, then the pixels of the
        /// first mip level, then for each smaller level a 16-byte record (width, height, row bytes, row count)
        /// followed by its pixels, then 16 bytes of padding. Block formats store no level below 4x4.
        std::span<const std::byte> data;

        /// Revision 3: one number per frame, read from `data`. Each is an index into the bitmaps of the bitmap
        /// table in the same file (in files that have several tables the numbering is not resolved). Empty for
        /// other revisions or a chunk whose size is not four bytes per frame.
        std::vector<std::uint32_t> frames;

        /// Revision 2: the image geometry, when `data` starts with a header of 32 bytes that is consistent.
        std::optional<FlicImageHeader> image;
    };

    struct FlicItem
    {
        std::uint32_t address = 0; ///< Address of the item's data, the only identity a flic has
        std::optional<Flic> flic;  ///< Nothing when the item or its chunk is unreadable
    };

    /// Flics of one part of an overlay: loader rows whose handler is tagged "flic". Spans in the result
    /// point into the part's bytes. Only common files carry chunks, so unique files give unreadable items.
    [[nodiscard]] std::vector<FlicItem> read_flics(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
