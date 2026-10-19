// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sign over a ride's queue: an arch of the ride's theme standing over the queue tile where guests join the
// line, its board showing the ride's name while it is open, else what it is doing ("Closed", "Testing"),
// scrolling along. Parks do not keep the signs; they are put up again from the queues.
//
// A line's head is the tile of the line no other tile of it leads into. The sign faces out of the head's
// start side, the way guests come in.
//
// The board's text is a loop: the words and a gap, drawn light on dark, as many times over as it takes to fill
// the board. The board shows a window of the loop that moves on as time passes.
#pragma once

#include "ovl/scenery_item.hpp"
#include "ovl/texture_image.hpp"
#include "save/model/world.hpp"
#include "ui/font.hpp"

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
        float height = 0;         ///< Of the head's start edge, in world units
        std::uint8_t heading = 0; ///< Out of the head's start side, as the path builder counts headings
        friend bool operator==(const QueueSignSpot&, const QueueSignSpot&) = default;
    };

    /// The sign spot of each line `paths` hold, by line id order; lines whose tiles all lead into each other
    /// have none.
    [[nodiscard]] std::vector<QueueSignSpot> queue_sign_spots(std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// Of a theme's items, the sign it puts over queues: a sign whose settings name it a queue sign. Empty when
    /// the theme has none.
    [[nodiscard]] std::string queue_sign_item(std::span<const ovl::SceneryItemEntry> items);

    /// The text loop for `text`, `height` pixels tall, in `font` at its own size; its glyphs' pages are
    /// `pages`, by the index the glyphs give, with coverage in alpha.
    [[nodiscard]] ovl::RgbaImage sign_text_loop(const ui::Font& font, std::span<const ovl::RgbaImage> pages, std::u16string_view text, std::uint32_t height);

    /// The `width` columns of `loop` from column `shift` on, wrapping round.
    [[nodiscard]] ovl::RgbaImage sign_window(const ovl::RgbaImage& loop, std::uint32_t shift, std::uint32_t width);

    /// How far the text has moved after `seconds`, in pixels of a loop `length` wide.
    [[nodiscard]] std::uint32_t sign_shift(float seconds, std::uint32_t length);

    /// The board's look, our own values.
    inline constexpr std::uint32_t kSignTextHeight = 40; ///< Pixels, room for the large interface font
    inline constexpr float kSignScroll = 24;             ///< Pixels a second
    inline constexpr std::uint8_t kSignInk = 255;        ///< Of the letters, grey
    inline constexpr std::uint8_t kSignDark = 12;        ///< Of the board behind them, grey
    inline constexpr std::uint32_t kSignGap = 2;         ///< After the words, in heights

} // namespace rct3::park
