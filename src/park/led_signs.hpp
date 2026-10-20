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

#include "ovl/scenery_item.hpp"
#include "ovl/texture_image.hpp"
#include "save/model/world.hpp"
#include "ui/font.hpp"

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
        std::uint64_t line = 0;
        int col = 0;
        int row = 0;
        float height = 0;       ///< Of the head's start edge, in world units
        std::int32_t turns = 0; ///< Quarter turns of the sign: as a railing on the head's start edge
        friend bool operator==(const QueueSignSpot&, const QueueSignSpot&) = default;
    };

    /// The sign spot of each line `paths` hold, by line id order; lines whose tiles all lead into each other
    /// have none.
    [[nodiscard]] std::vector<QueueSignSpot> queue_sign_spots(std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// Of a theme's items, the sign it puts over queues: a sign whose settings name it a queue sign. Empty when
    /// the theme has none.
    [[nodiscard]] std::string queue_sign_item(std::span<const ovl::SceneryItemEntry> items);

    /// The text loop for `text`, `height` pixels tall, in `font` at its own size: how much the letters cover
    /// each pixel, 0 to 255 in every channel, as wide as a whole number of lamps. The glyphs' pages are
    /// `pages`, by the index the glyphs give, with coverage in alpha.
    [[nodiscard]] ovl::RgbaImage sign_text_loop(const ui::Font& font, std::span<const ovl::RgbaImage> pages, std::u16string_view text, std::uint32_t height);

    /// A lamp colour, red, green and blue.
    using LampColour = std::array<std::uint8_t, 3>;

    /// The board showing the `width` columns of `loop` from column `shift` on, wrapping round, in lamps of
    /// `colour`.
    [[nodiscard]] ovl::RgbaImage sign_window(const ovl::RgbaImage& loop, std::uint32_t shift, std::uint32_t width, LampColour colour);

    /// How far the text has moved after `seconds`, in pixels of a loop `length` wide: whole lamps.
    [[nodiscard]] std::uint32_t sign_shift(float seconds, std::uint32_t length);

    /// The board's look, our own values.
    inline constexpr std::uint32_t kSignTextHeight = 40; ///< Pixels, room for the large interface font
    inline constexpr std::uint32_t kSignLamp = 3;        ///< Pixels a lamp takes each way
    inline constexpr float kSignScroll = 24;             ///< Pixels a second
    inline constexpr std::uint8_t kSignDark = 6;         ///< Of the board between the lamps, grey
    inline constexpr std::uint8_t kSignLit = 96;         ///< Cover of a lamp's middle that lights it
    inline constexpr float kSignLampOff = 0.1f;          ///< An unlit lamp's share of the colour
    inline constexpr float kSignGlow = 0.25f;            ///< A lit lamp's glow round its dot, share of the colour
    inline constexpr std::uint32_t kSignGap = 2;         ///< After the words, in heights
    inline constexpr LampColour kSignWhite { 255, 255, 255 };

} // namespace rct3::park
