// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recolourable pictures. A flexi texture that can be recoloured keeps palette entry 0 for itself and splits
// the other 255 into three runs of 85 shades, light to dark, one per colour slot: entries 1 to 85 for the
// first slot, 86 to 170 for the second, 171 to 255 for the third. The art paints them in blue, magenta and
// yellow. A placed object picks one of 32 colours for each slot, and the slot's run is replaced by that
// colour's shades. Painting takes any colour, not only those 32, so an object can be given others.
//
// The game's own 32 colours are not stored in any data file. These are ours: a colour for each index,
// chosen to read like the one the game shows there (index 27 a deep red, for instance), and shades made
// by fading from a pale tint down to the colour and on towards black. They are close, not the same.
#pragma once

#include "core/types.hpp"
#include "ovl/flexi_texture.hpp"
#include "ovl/texture/image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    inline constexpr usize kFlexiColourCount = 32;
    inline constexpr usize kFlexiSlotCount = 3;
    inline constexpr usize kFlexiShades = 85;
    /// The shade at which a run shows its colour unchanged; lighter ones before it, darker after.
    inline constexpr usize kFlexiFullShade = 41;

    struct Rgb
    {
        u8 r = 0;
        u8 g = 0;
        u8 b = 0;

        friend bool operator==(const Rgb&, const Rgb&) = default;
    };

    /// The colour each slot is painted in.
    using FlexiColours = std::array<Rgb, kFlexiSlotCount>;

    /// Colour `index` of the 32 (taken modulo the count).
    [[nodiscard]] Rgb flexi_colour(usize index);
    /// Shade `shade` (0 the lightest, kFlexiShades - 1 the darkest) of `colour`.
    [[nodiscard]] Rgb flexi_shade(Rgb colour, usize shade);

    /// The colours an object's stored picks name; a pick out of range is the first colour.
    [[nodiscard]] FlexiColours flexi_colours(const std::array<i32, kFlexiSlotCount>& stored);

    /// The picks a new tracked ride of model `name` starts in: one of a few colour schemes of ours (a main
    /// colour, an accent and a trim), chosen by the name in any case, so rides of one model start alike and
    /// models differ. Our own rule; the game starts them in black.
    [[nodiscard]] std::array<i32, kFlexiSlotCount> name_colours(std::string_view name);

    /// One frame of a flexi texture kept as palette indices, so it can be painted in any colours.
    struct PaletteImage
    {
        u32 width = 0;
        u32 height = 0;
        std::array<Rgb, 256> palette { };
        std::vector<u8> indices { }; ///< width * height
        std::vector<u8> alpha { };   ///< width * height, or empty for an opaque picture
        u32 slots = 0;               ///< Bit k set: slot k is recoloured
    };

    /// `frame` as a palette image whose recoloured slots are `slots`. Nothing when the frame is malformed.
    [[nodiscard]] std::optional<PaletteImage> palette_image(const ovl::FlexiFrame& frame, u32 slots);

    /// `colours` with those of slots not in `slots` made black, so equal paintings compare equal.
    [[nodiscard]] FlexiColours used_colours(u32 slots, FlexiColours colours);

    /// The picture with each recoloured slot's run replaced by the picked colour's shades.
    [[nodiscard]] ovl::texture::RgbaImage paint(const PaletteImage& image, FlexiColours colours);

    /// A name for `texture` painted in `colours`: the texture's own name when it recolours nothing.
    [[nodiscard]] std::string painted_name(std::string_view texture, u32 slots, FlexiColours colours);

} // namespace rct3::park
