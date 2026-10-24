// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/flexi_colours.hpp"

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
        constexpr std::array<HexColour, kFlexiColourCount> kColours { {
            hex("#324040"), // 0 charcoal
            hex("#829191"), // 1 grey
            hex("#cdd4d4"), // 2 pale grey
            hex("#50508c"), // 3 indigo
            hex("#8787be"), // 4 lavender
            hex("#9664c3"), // 5 purple
            hex("#1e50c8"), // 6 royal blue
            hex("#5aa0e1"), // 7 sky blue
            hex("#aae1f5"), // 8 pale blue
            hex("#46a5a0"), // 9 teal
            hex("#a5e1e1"), // 10 aqua
            hex("#288c0a"), // 11 green
            hex("#5f9673"), // 12 sage
            hex("#739641"), // 13 olive
            hex("#5abe3c"), // 14 bright green
            hex("#9bb46e"), // 15 pale olive
            hex("#969650"), // 16 khaki
            hex("#f0e137"), // 17 yellow
            hex("#f0c81e"), // 18 gold
            hex("#a56e0a"), // 19 ochre
            hex("#e66e14"), // 20 orange
            hex("#af4b0f"), // 21 rust
            hex("#b99655"), // 22 tan
            hex("#8c5f28"), // 23 brown
            hex("#8c7d69"), // 24 taupe
            hex("#d29673"), // 25 peach
            hex("#be5050"), // 26 rose
            hex("#a50505"), // 27 deep red
            hex("#e10a05"), // 28 bright red
            hex("#9b1455"), // 29 crimson
            hex("#eb5aaa"), // 30 pink
            hex("#eb7869"), // 31 coral
        } };

        /// How pale the lightest shade is: the share of white mixed into the colour there.
        constexpr float kPalest = 0.85f;
        /// How dark the darkest shade is: the share of the colour left there.
        constexpr float kDarkest = 0.15f;

        std::uint8_t channel(float v)
        {
            return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
        }

    } // namespace

    Rgb flexi_colour(std::size_t index)
    {
        const HexColour c = kColours[index % kFlexiColourCount];
        return { c.r, c.g, c.b };
    }

    Rgb flexi_shade(Rgb c, std::size_t shade)
    {
        shade = std::min(shade, kFlexiShades - 1);
        float keep = 1;  // Share of the colour
        float white = 0; // Share of white added
        if (shade < kFlexiFullShade)
        {
            white = kPalest * static_cast<float>(kFlexiFullShade - shade) / static_cast<float>(kFlexiFullShade);
            keep = 1 - white;
        }
        else
        {
            const float t = static_cast<float>(shade - kFlexiFullShade) / static_cast<float>(kFlexiShades - 1 - kFlexiFullShade);
            keep = 1 - (1 - kDarkest) * t;
        }
        const auto mix = [&](std::uint8_t v) { return channel(static_cast<float>(v) * keep + 255 * white); };
        return { mix(c.r), mix(c.g), mix(c.b) };
    }

    FlexiColours flexi_colours(const std::array<std::int32_t, kFlexiSlotCount>& stored)
    {
        FlexiColours out { };
        for (std::size_t k = 0; k < kFlexiSlotCount; ++k)
            out[k] = flexi_colour(stored[k] >= 0 && static_cast<std::size_t>(stored[k]) < kFlexiColourCount ? static_cast<std::size_t>(stored[k]) : 0);
        return out;
    }

    std::array<std::int32_t, kFlexiSlotCount> name_colours(std::string_view name)
    {
        // Our own schemes, as indices of kColours: main, accent, trim.
        constexpr std::array<std::array<std::int32_t, kFlexiSlotCount>, 12> kSchemes { {
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
        std::uint32_t hash = 2166136261u; // FNV-1a
        for (const char c : name)
            hash = (hash ^ static_cast<std::uint8_t>(strings::ascii_lower(c))) * 16777619u;
        return kSchemes[hash % kSchemes.size()];
    }

    std::optional<PaletteImage> palette_image(const ovl::FlexiFrame& frame, std::uint32_t slots)
    {
        const std::size_t count = static_cast<std::size_t>(frame.width) * frame.height;
        if (count == 0 || frame.width > ovl::kMaxImageSide || frame.height > ovl::kMaxImageSide || frame.palette.size() < ovl::kFlexiPaletteSize || frame.pixels.size() < count || (frame.alpha && frame.alpha->size() < count))
            return std::nullopt;
        PaletteImage image;
        image.width = frame.width;
        image.height = frame.height;
        image.slots = slots & 7u;
        for (std::size_t i = 0; i < image.palette.size(); ++i)
        {
            // Stored blue, green, red, unused.
            const auto at = [&](std::size_t k) { return std::to_integer<std::uint8_t>(frame.palette[i * 4 + k]); };
            image.palette[i] = { at(2), at(1), at(0) };
        }
        image.indices.resize(count);
        std::ranges::transform(frame.pixels.first(count), image.indices.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
        if (frame.alpha)
        {
            image.alpha.resize(count);
            std::ranges::transform(frame.alpha->first(count), image.alpha.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
        }
        return image;
    }

    FlexiColours used_colours(std::uint32_t slots, FlexiColours colours)
    {
        for (std::size_t k = 0; k < kFlexiSlotCount; ++k)
        {
            if ((slots & (1u << k)) == 0)
                colours[k] = { };
        }
        return colours;
    }

    ovl::RgbaImage paint(const PaletteImage& image, FlexiColours colours)
    {
        std::array<Rgb, 256> palette = image.palette;
        for (std::size_t k = 0; k < kFlexiSlotCount; ++k)
        {
            if ((image.slots & (1u << k)) == 0)
                continue;
            for (std::size_t s = 0; s < kFlexiShades; ++s)
                palette[1 + k * kFlexiShades + s] = flexi_shade(colours[k], s);
        }
        ovl::RgbaImage out;
        out.width = image.width;
        out.height = image.height;
        out.pixels.resize(image.indices.size() * 4);
        for (std::size_t i = 0; i < image.indices.size(); ++i)
        {
            const Rgb c = palette[image.indices[i]];
            out.pixels[i * 4 + 0] = c.r;
            out.pixels[i * 4 + 1] = c.g;
            out.pixels[i * 4 + 2] = c.b;
            out.pixels[i * 4 + 3] = image.alpha.empty() ? 255 : image.alpha[i];
        }
        return out;
    }

    std::string painted_name(std::string_view texture, std::uint32_t slots, FlexiColours colours)
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
