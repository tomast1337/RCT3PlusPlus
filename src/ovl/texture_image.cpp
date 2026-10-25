// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/texture_image.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::size_t kFlicHeaderSize = 32;      // eight header words, the first level's pixels follow
        constexpr std::size_t kFlicLevelHeaderSize = 16; // width, height, row bytes and row count of a later level

        std::uint8_t u8(std::span<const std::byte> b, std::size_t at)
        {
            return static_cast<std::uint8_t>(b[at]);
        }

        std::uint16_t u16_at(std::span<const std::byte> b, std::size_t at)
        {
            return static_cast<std::uint16_t>(u8(b, at) | (u8(b, at + 1) << 8));
        }

        bool side_ok(std::uint32_t side)
        {
            return side != 0 && side <= kMaxImageSide;
        }

        RgbaImage blank(std::uint32_t width, std::uint32_t height)
        {
            RgbaImage image;
            image.width = width;
            image.height = height;
            image.pixels.assign(std::size_t { width } * height * 4, 0);
            return image;
        }

        // 5-6-5 colour to 8 bits per channel, replicating the high bits into the low ones.
        std::array<std::uint8_t, 3> expand565(std::uint16_t c)
        {
            const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            return { static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 2) | (g >> 4)), static_cast<std::uint8_t>((b << 3) | (b >> 2)) };
        }

        // Decodes the 8-byte colour part of a block into 16 pixels (alpha 255 or, for the 1-bit alpha mode,
        // 0 for the transparent colour). `one_bit_alpha` allows the transparent entry of the 3-colour mode.
        void decode_colour_block(std::span<const std::byte> block, bool one_bit_alpha, std::uint8_t out[16][4])
        {
            const std::uint16_t c0 = u16_at(block, 0), c1 = u16_at(block, 2);
            const auto e0 = expand565(c0), e1 = expand565(c1);
            std::uint8_t palette[4][4];
            for (int i = 0; i < 3; ++i)
            {
                palette[0][i] = e0[i];
                palette[1][i] = e1[i];
            }
            palette[0][3] = palette[1][3] = palette[2][3] = palette[3][3] = 255;
            if (c0 > c1 || !one_bit_alpha)
            {
                for (int i = 0; i < 3; ++i)
                {
                    palette[2][i] = static_cast<std::uint8_t>((2 * e0[i] + e1[i]) / 3);
                    palette[3][i] = static_cast<std::uint8_t>((e0[i] + 2 * e1[i]) / 3);
                }
            }
            else
            {
                for (int i = 0; i < 3; ++i)
                {
                    palette[2][i] = static_cast<std::uint8_t>((e0[i] + e1[i]) / 2);
                    palette[3][i] = 0;
                }
                palette[3][3] = 0;
            }
            for (int row = 0; row < 4; ++row)
            {
                const unsigned bits = u8(block, 4 + row);
                for (int col = 0; col < 4; ++col)
                    std::memcpy(out[row * 4 + col], palette[(bits >> (2 * col)) & 3], 4);
            }
        }

        std::optional<RgbaImage> decode_raw(std::uint32_t width, std::uint32_t height, std::span<const std::byte> data)
        {
            RgbaImage image = blank(width, height);
            const std::size_t count = std::size_t { width } * height;
            for (std::size_t i = 0; i < count; ++i)
            {
                image.pixels[i * 4 + 0] = u8(data, i * 4 + 2);
                image.pixels[i * 4 + 1] = u8(data, i * 4 + 1);
                image.pixels[i * 4 + 2] = u8(data, i * 4 + 0);
                image.pixels[i * 4 + 3] = u8(data, i * 4 + 3);
            }
            return image;
        }

        std::optional<RgbaImage> decode_blocks(std::uint32_t width, std::uint32_t height, std::span<const std::byte> data, bool explicit_alpha)
        {
            RgbaImage image = blank(width, height);
            const std::size_t block_bytes = explicit_alpha ? 16 : 8;
            const std::size_t blocks_wide = (std::size_t { width } + 3) / 4;
            const std::size_t blocks_high = (std::size_t { height } + 3) / 4;
            for (std::size_t by = 0; by < blocks_high; ++by)
            {
                for (std::size_t bx = 0; bx < blocks_wide; ++bx)
                {
                    const auto block = data.subspan((by * blocks_wide + bx) * block_bytes, block_bytes);
                    std::uint8_t px[16][4];
                    decode_colour_block(explicit_alpha ? block.subspan(8) : block, !explicit_alpha, px);
                    for (std::size_t i = 0; i < 16; ++i)
                    {
                        const std::size_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                        if (x >= width || y >= height)
                            continue;
                        std::uint8_t* dst = &image.pixels[(y * width + x) * 4];
                        std::memcpy(dst, px[i], 4);
                        if (explicit_alpha)
                        {
                            const unsigned nibble = (u8(block, i / 2) >> ((i % 2) * 4)) & 15;
                            dst[3] = static_cast<std::uint8_t>(nibble * 17);
                        }
                    }
                }
            }
            return image;
        }

    } // namespace

    std::optional<RgbaImage> decode_flexi_frame(const FlexiFrame& frame)
    {
        if (!side_ok(frame.width) || !side_ok(frame.height))
            return std::nullopt;
        const std::size_t count = std::size_t { frame.width } * frame.height;
        if (frame.palette.size() < kFlexiPaletteSize || frame.pixels.size() < count)
            return std::nullopt;
        if (frame.alpha && frame.alpha->size() < count)
            return std::nullopt;

        RgbaImage image = blank(frame.width, frame.height);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t entry = std::size_t { u8(frame.pixels, i) } * 4;
            image.pixels[i * 4 + 0] = u8(frame.palette, entry + 2);
            image.pixels[i * 4 + 1] = u8(frame.palette, entry + 1);
            image.pixels[i * 4 + 2] = u8(frame.palette, entry + 0);
            image.pixels[i * 4 + 3] = frame.alpha ? u8(*frame.alpha, i) : 255;
        }
        return image;
    }

    std::optional<RgbaImage> decode_pixels(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::span<const std::byte> data)
    {
        if (!side_ok(width) || !side_ok(height))
            return std::nullopt;
        const auto size = bitmap_data_size(format, width, height, 1);
        if (!size || data.size() < *size)
            return std::nullopt;
        switch (*bitmap_layout(format))
        {
            case BitmapLayout::Pixels32:
                return decode_raw(width, height, data);
            case BitmapLayout::Blocks8:
                return decode_blocks(width, height, data, false);
            case BitmapLayout::Blocks16:
                return decode_blocks(width, height, data, true);
        }
        return std::nullopt;
    }

    namespace
    {

        std::vector<RgbaImage> decode_chain(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t levels, std::span<const std::byte> data)
        {
            std::vector<RgbaImage> out;
            if (levels == 0 || levels > 16 || width == 0 || height == 0)
                return out;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const std::uint32_t w = std::max<std::uint32_t>(1, width >> level);
                const std::uint32_t h = std::max<std::uint32_t>(1, height >> level);
                const auto size = bitmap_data_size(format, w, h, 1);
                if (!size || data.size() < *size)
                    return { };
                auto image = decode_pixels(format, w, h, data);
                if (!image)
                    return { };
                out.push_back(std::move(*image));
                data = data.subspan(static_cast<std::size_t>(*size));
            }
            return out;
        }

    } // namespace

    std::vector<RgbaImage> decode_bitmap(const Bitmap& bitmap)
    {
        return decode_chain(bitmap.format, bitmap.width, bitmap.height, bitmap.levels, bitmap.data);
    }

    std::vector<RgbaImage> decode_flic_image(const Flic& flic)
    {
        if (!flic.image || flic.data.size() < kFlicHeaderSize)
            return { };
        const FlicImageHeader& h = *flic.image;
        const auto layout = bitmap_layout(h.format);
        if (!layout || h.width == 0 || h.height == 0 || h.level_count == 0 || h.level_count > 16)
            return { };

        // The first level follows the 32-byte header directly. Every further level is preceded by a 16-byte
        // record repeating its size and row geometry. Block formats stop at the last level that still holds a
        // whole block in each direction, smaller levels are not stored. The remaining bytes are padding.
        std::vector<RgbaImage> out;
        std::span<const std::byte> rest = flic.data.subspan(kFlicHeaderSize);
        for (std::uint32_t level = 0; level < h.level_count; ++level)
        {
            const std::uint32_t w = std::max<std::uint32_t>(1, h.width >> level);
            const std::uint32_t ht = std::max<std::uint32_t>(1, h.height >> level);
            if (*layout != BitmapLayout::Pixels32 && (w < 4 || ht < 4))
                break;
            if (level != 0)
            {
                if (rest.size() < kFlicLevelHeaderSize)
                    return { };
                rest = rest.subspan(kFlicLevelHeaderSize);
            }
            const auto size = bitmap_data_size(h.format, w, ht, 1);
            if (!size || rest.size() < *size)
                return { };
            auto image = decode_pixels(h.format, w, ht, rest);
            if (!image)
                return { };
            out.push_back(std::move(*image));
            rest = rest.subspan(static_cast<std::size_t>(*size));
        }
        return out;
    }

    std::vector<std::byte> encode_tga(const RgbaImage& image)
    {
        if (image.width == 0 || image.height == 0 || image.width > 0xffff || image.height > 0xffff)
            return { };
        if (image.pixels.size() != std::size_t { image.width } * image.height * 4)
            return { };

        std::vector<std::byte> out;
        out.reserve(18 + image.pixels.size());
        auto put = [&out](unsigned v) { out.push_back(static_cast<std::byte>(v)); };
        // Header: no id, no colour map, uncompressed true colour, then origin, size, 32 bits per pixel and the
        // descriptor with 8 alpha bits and a top-left origin.
        for (int i = 0; i < 18; ++i)
            put(0);
        out[2] = std::byte { 2 };
        out[12] = static_cast<std::byte>(image.width & 0xff);
        out[13] = static_cast<std::byte>(image.width >> 8);
        out[14] = static_cast<std::byte>(image.height & 0xff);
        out[15] = static_cast<std::byte>(image.height >> 8);
        out[16] = std::byte { 32 };
        out[17] = std::byte { 0x28 };
        for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        {
            put(image.pixels[i + 2]);
            put(image.pixels[i + 1]);
            put(image.pixels[i + 0]);
            put(image.pixels[i + 3]);
        }
        return out;
    }

    int texture_halvings(float detail, std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0)
            return 0;
        const int doublings = std::bit_width(std::max(width, height)) - 1;
        const int kept = static_cast<int>(std::ceil(static_cast<float>(doublings) * std::clamp(detail, 0.0f, 1.0f)));
        return std::min(doublings - kept, static_cast<int>(std::bit_width(std::min(width, height))) - 1);
    }

    RgbaImage halved(RgbaImage image, int times)
    {
        for (int t = 0; t < times && image.width > 1 && image.height > 1 && image.pixels.size() == std::size_t { image.width } * image.height * 4; ++t)
        {
            RgbaImage half { .width = image.width / 2, .height = image.height / 2, .pixels = std::vector<std::uint8_t>(std::size_t { image.width / 2 } * (image.height / 2) * 4) };
            for (std::uint32_t y = 0; y < half.height; ++y)
            {
                for (std::uint32_t x = 0; x < half.width; ++x)
                {
                    std::array<std::uint32_t, 4> sum { };
                    for (const std::uint32_t sy : { 2 * y, std::min(2 * y + 1, image.height - 1) })
                    {
                        for (const std::uint32_t sx : { 2 * x, std::min(2 * x + 1, image.width - 1) })
                        {
                            const std::uint8_t* p = &image.pixels[(std::size_t { sy } * image.width + sx) * 4];
                            for (std::size_t c = 0; c < 3; ++c)
                                sum[c] += p[c] * p[3];
                            sum[3] += p[3];
                        }
                    }
                    std::uint8_t* out = &half.pixels[(std::size_t { y } * half.width + x) * 4];
                    for (std::size_t c = 0; c < 3; ++c)
                        out[c] = static_cast<std::uint8_t>(sum[3] > 0 ? (sum[c] + sum[3] / 2) / sum[3] : 0);
                    out[3] = static_cast<std::uint8_t>((sum[3] + 2) / 4);
                }
            }
            image = std::move(half);
        }
        return image;
    }

} // namespace rct3::ovl
