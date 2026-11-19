// SPDX-License-Identifier: GPL-3.0-or-later
//
// LED signs: boards of lamps that scroll words along, as over ride queues and on park entrances.
//
// The sign over a ride's queue: an arch of the ride's theme standing over the queue tile where guests join the
// line, its board showing the ride's name while it is open, else what it is doing ("Closed", "Testing"),
// scrolling along. Parks do not keep the signs; they are put up again from the queues.
//
// A line's head is the tile of the line no other tile of it leads into. The sign stands on the head's start
// edge, where guests come in, placed and turned as a railing on that edge would be.
//
// The board's text is a loop: the words and a gap, as many times over as it takes to fill the board. The board
// shows a window of the loop that moves on a dot at a time as time passes, drawn as a grid of lamps: a lamp is
// lit where the letters cover its middle, in the board's colour, and glows a little round its dot.
#pragma once

#include "core/types.hpp"
#include "ovl/scenery_item.hpp"
#include "ovl/texture/image.hpp"
#include "park/turns.hpp"
#include "save/model/world.hpp"
#include "units/units.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// Where a line's sign stands.
    struct QueueSignSpot
    {
        u64 line = 0;
        i32 col = 0;
        i32 row = 0;
        f32 height = 0; ///< Of the head's start edge, in world units
        ModelTurn turn; ///< Of the sign: as a railing on the head's start edge
        friend bool operator==(const QueueSignSpot&, const QueueSignSpot&) = default;
    };

    /// The sign spot of each line `paths` hold, by line id order; lines whose tiles all lead into each other
    /// have none.
    [[nodiscard]] std::vector<QueueSignSpot> queue_sign_spots(std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid);

    /// Of a theme's items, the sign it puts over queues: a sign whose settings name it a queue sign. Empty when
    /// the theme has none.
    [[nodiscard]] std::string queue_sign_item(std::span<const ovl::SceneryItemEntry> items);

    /// A lamp colour, red, green and blue.
    using LampColour = std::array<u8, 3>;

    /// The board showing the `width` columns of `loop` from column `shift` on, wrapping round, in lamps of
    /// `colour`.
    [[nodiscard]] ovl::texture::RgbaImage sign_window(const ovl::texture::RgbaImage& loop, u32 shift, u32 width, LampColour colour);

    /// How far the text has moved after `seconds`, in pixels of a loop `length` wide: whole lamps.
    [[nodiscard]] u32 sign_shift(f32 seconds, u32 length);

    /// The board's look.
    inline constexpr u32 kSignTextHeight = 40; ///< Pixels, room for the large interface font
    inline constexpr u32 kSignLamp = 3;        ///< Pixels a lamp takes each way
    inline constexpr units::PixelPace kSignScroll { 24 };
    inline constexpr u8 kSignDark = 6;        ///< Of the board between the lamps, grey
    inline constexpr u8 kSignLit = 96;        ///< Cover of a lamp's middle that lights it
    inline constexpr f32 kSignLampOff = 0.1f; ///< An unlit lamp's share of the colour
    inline constexpr f32 kSignGlow = 0.25f;   ///< A lit lamp's glow round its dot, share of the colour
    inline constexpr u32 kSignGap = 2;        ///< After the words, in heights
    inline constexpr LampColour kSignWhite { 255, 255, 255 };

} // namespace rct3::park
