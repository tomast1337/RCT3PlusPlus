// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/flexi_colours.hpp"

#include "core/hash.hpp"
#include "core/types.hpp"
#include "math/hex_colour.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::park
{

    namespace
    {

        // Picks, one per index, named for what the index shows in the game.
        constexpr std::array<math::HexColour, kFlexiColourCount> kColours { {
            math::hex("#324040"), // 0 charcoal
            math::hex("#829191"), // 1 grey
            math::hex("#cdd4d4"), // 2 pale grey
            math::hex("#50508c"), // 3 indigo
            math::hex("#8787be"), // 4 lavender
            math::hex("#9664c3"), // 5 purple
            math::hex("#1e50c8"), // 6 royal blue
            math::hex("#5aa0e1"), // 7 sky blue
            math::hex("#aae1f5"), // 8 pale blue
            math::hex("#46a5a0"), // 9 teal
            math::hex("#a5e1e1"), // 10 aqua
            math::hex("#288c0a"), // 11 green
            math::hex("#5f9673"), // 12 sage
            math::hex("#739641"), // 13 olive
            math::hex("#5abe3c"), // 14 bright green
            math::hex("#9bb46e"), // 15 pale olive
            math::hex("#969650"), // 16 khaki
            math::hex("#f0e137"), // 17 yellow
            math::hex("#f0c81e"), // 18 gold
            math::hex("#a56e0a"), // 19 ochre
            math::hex("#e66e14"), // 20 orange
            math::hex("#af4b0f"), // 21 rust
            math::hex("#b99655"), // 22 tan
            math::hex("#8c5f28"), // 23 brown
            math::hex("#8c7d69"), // 24 taupe
            math::hex("#d29673"), // 25 peach
            math::hex("#be5050"), // 26 rose
            math::hex("#a50505"), // 27 deep red
            math::hex("#e10a05"), // 28 bright red
            math::hex("#9b1455"), // 29 crimson
            math::hex("#eb5aaa"), // 30 pink
            math::hex("#eb7869"), // 31 coral
        } };

        /// How pale the lightest shade is: the share of white mixed into the colour there.
        constexpr f32 kPalest = 0.85f;
        /// How dark the darkest shade is: the share of the colour left there.
        constexpr f32 kDarkest = 0.15f;

        u8 channel(f32 v)
        {
            return static_cast<u8>(std::clamp(std::lround(v), 0L, 255L));
        }

    } // namespace

    Rgb flexi_colour(usize index)
    {
        const math::HexColour c = kColours[index % kFlexiColourCount];
        return { c.r, c.g, c.b };
    }

    Rgb flexi_shade(Rgb c, usize shade)
    {
        shade = std::min(shade, kFlexiShades - 1);
        f32 keep = 1;  // Share of the colour
        f32 white = 0; // Share of white added
        if (shade < kFlexiFullShade)
        {
            white = kPalest * static_cast<f32>(kFlexiFullShade - shade) / static_cast<f32>(kFlexiFullShade);
            keep = 1 - white;
        }
        else
        {
            const f32 t = static_cast<f32>(shade - kFlexiFullShade) / static_cast<f32>(kFlexiShades - 1 - kFlexiFullShade);
            keep = 1 - (1 - kDarkest) * t;
        }
        const auto mix = [&](u8 v) { return channel(static_cast<f32>(v) * keep + 255 * white); };
        return { mix(c.r), mix(c.g), mix(c.b) };
    }

    FlexiColours flexi_colours(const std::array<i32, kFlexiSlotCount>& stored)
    {
        FlexiColours out { };
        for (usize k = 0; k < kFlexiSlotCount; ++k)
            out[k] = flexi_colour(stored[k] >= 0 && static_cast<usize>(stored[k]) < kFlexiColourCount ? static_cast<usize>(stored[k]) : 0);
        return out;
    }

    std::array<i32, kFlexiSlotCount> name_colours(std::string_view name)
    {
        // Our own schemes, as indices of kColours: main, accent, trim.
        constexpr std::array<std::array<i32, kFlexiSlotCount>, 12> kSchemes { {
            { 28, 17, 1 },  // bright red, yellow, grey
            { 6, 18, 2 },   // royal blue, gold, pale grey
            { 11, 17, 0 },  // green, yellow, charcoal
            { 5, 30, 2 },   // purple, pink, pale grey
            { 20, 0, 2 },   // orange, charcoal, pale grey
            { 9, 31, 2 },   // teal, coral, pale grey
            { 27, 18, 0 },  // deep red, gold, charcoal
            { 7, 28, 2 },   // sky blue, bright red, pale grey
            { 0, 28, 1 },   // charcoal, bright red, grey
            { 23, 22, 16 }, // brown, tan, khaki
            { 14, 3, 2 },   // bright green, indigo, pale grey
            { 29, 8, 0 },   // crimson, pale blue, charcoal
        } };
        u32 hash = core::kFnv32Offset;
        for (const char c : name)
            hash = (hash ^ static_cast<u8>(strings::ascii_lower(c))) * core::kFnv32Prime;
        return kSchemes[hash % kSchemes.size()];
    }

    std::optional<PaletteImage> palette_image(const ovl::FlexiFrame& frame, u32 slots)
    {
        const usize count = static_cast<usize>(frame.width) * frame.height;
        if (count == 0 || frame.width > ovl::texture::kMaxImageSide || frame.height > ovl::texture::kMaxImageSide || frame.palette.size() < ovl::kFlexiPaletteSize || frame.pixels.size() < count || (frame.alpha && frame.alpha->size() < count))
            return std::nullopt;
        PaletteImage image;
        image.width = frame.width;
        image.height = frame.height;
        image.slots = slots & 7u;
        for (usize i = 0; i < image.palette.size(); ++i)
        {
            // Stored blue, green, red, unused.
            const auto at = [&](usize k) { return std::to_integer<u8>(frame.palette[i * 4 + k]); };
            image.palette[i] = { at(2), at(1), at(0) };
        }
        image.indices.resize(count);
        std::ranges::transform(frame.pixels.first(count), image.indices.begin(), [](std::byte b) { return std::to_integer<u8>(b); });
        if (frame.alpha)
        {
            image.alpha.resize(count);
            std::ranges::transform(frame.alpha->first(count), image.alpha.begin(), [](std::byte b) { return std::to_integer<u8>(b); });
        }
        return image;
    }

    FlexiColours used_colours(u32 slots, FlexiColours colours)
    {
        for (usize k = 0; k < kFlexiSlotCount; ++k)
        {
            if ((slots & (1u << k)) == 0)
                colours[k] = { };
        }
        return colours;
    }

    ovl::texture::RgbaImage paint(const PaletteImage& image, FlexiColours colours)
    {
        std::array<Rgb, 256> palette = image.palette;
        for (usize k = 0; k < kFlexiSlotCount; ++k)
        {
            if ((image.slots & (1u << k)) == 0)
                continue;
            for (usize s = 0; s < kFlexiShades; ++s)
                palette[1 + k * kFlexiShades + s] = flexi_shade(colours[k], s);
        }
        ovl::texture::RgbaImage out;
        out.width = image.width;
        out.height = image.height;
        out.pixels.resize(image.indices.size() * 4);
        for (usize i = 0; i < image.indices.size(); ++i)
        {
            const Rgb c = palette[image.indices[i]];
            out.pixels[i * 4 + 0] = c.r;
            out.pixels[i * 4 + 1] = c.g;
            out.pixels[i * 4 + 2] = c.b;
            out.pixels[i * 4 + 3] = image.alpha.empty() ? 255 : image.alpha[i];
        }
        return out;
    }

    std::string painted_name(std::string_view texture, u32 slots, FlexiColours colours)
    {
        std::string name(texture);
        if ((slots & 7u) == 0)
            return name;
        const FlexiColours used = used_colours(slots, colours);
        for (const Rgb& c : used)
            name += std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
        return name;
    }

} // namespace rct3::park
