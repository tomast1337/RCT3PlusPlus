// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finding the pixels behind a texture. A texture's node is the record of a flic. A flic of revision 3
// lists frames as indices into the bitmaps of a bitmap table: the nearest table stored before the flic in
// the same part. A flic of revision 2 carries its image itself.
#pragma once

#include "ovl/bitmap_table.hpp"
#include "ovl/flic.hpp"
#include "ovl/texture.hpp"
#include "ovl/texture_image.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rct3::ovl
{

    /// The flics and bitmap tables of one overlay, indexed by address. Views point into the overlay, which
    /// must outlive this.
    class TexturePixels
    {
    public:
        [[nodiscard]] static TexturePixels index(const Overlay& overlay);

        /// The largest mip level of the texture's first frame, rows as stored in the file. Nothing when the
        /// texture has no node, the node is not a flic, or the pixels cannot be decoded.
        [[nodiscard]] std::optional<RgbaImage> first_frame(const Texture& texture) const
        {
            return frame(texture, 0);
        }

        /// Frames the texture's flic lists (1 for a flic that carries its own image, 0 without a flic).
        [[nodiscard]] std::size_t frame_count(const Texture& texture) const;
        /// The largest mip level of frame `index`, rows as stored. Nothing when the frame is out of range or
        /// cannot be decoded. Font pages are the frames of the font's texture.
        [[nodiscard]] std::optional<RgbaImage> frame(const Texture& texture, std::size_t index) const;

        [[nodiscard]] std::size_t flic_count() const
        {
            return flics_.size();
        }
        [[nodiscard]] std::size_t table_count() const
        {
            return tables_.size();
        }

    private:
        struct Table
        {
            std::uint32_t address = 0;
            BitmapTable table;
        };

        std::unordered_map<std::uint32_t, Flic> flics_;
        std::vector<Table> tables_; ///< By address
    };

    /// The image with its rows in the opposite order.
    [[nodiscard]] RgbaImage flipped_vertically(RgbaImage image);

} // namespace rct3::ovl
