// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/led_signs.hpp"

#include "park/footpath_builder.hpp"
#include "park/item_category.hpp"
#include "park/queue_fences.hpp"

#include <array>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid owned_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
        return g;
    }

    /// `count` ground queue tiles of line `line` laid from `from` heading `heading`: the line flows back
    /// towards the first, as a queue does until it reaches an entrance.
    std::vector<save::PathTile> lay_line(const save::TerrainGrid& g, save::Tile from, std::uint8_t heading, int count, std::uint64_t line)
    {
        FootpathBuilder b;
        b.queue = true;
        b.heading = heading;
        start_at(b, from);
        std::vector<save::PathTile> paths;
        for (int i = 0; i < count; ++i)
            (void)lay_path(b, paths, *next_piece(b, g), "QueueSet", line * 100 + static_cast<std::uint64_t>(i), 0);
        for (save::PathTile& p : paths)
            p.queue->queue_line_ref = line;
        return paths;
    }
} // namespace

TEST_CASE("a line's sign stands on the tile no other leads into, facing the way guests come in")
{
    const save::TerrainGrid g = owned_grid(8, 8);
    std::vector<save::PathTile> paths = lay_line(g, { 2, 1 }, 0, 3, 7);
    const save::PathTile last = paths.back();
    const auto spots = queue_sign_spots(paths, g);
    REQUIRE(spots.size() == 1);
    CHECK(spots[0].line == 7);
    CHECK(spots[0].col == last.col);
    CHECK(spots[0].row == last.row);
    CHECK(spots[0].turns == edge_turns(TileSide::Front)); // Its start side, +row

    // Tiles of no line, and a line of tiles all leading into each other, get none.
    paths.back().queue->queue_line_ref = 0;
    paths.front().queue->queue_line_ref = 0;
    CHECK(queue_sign_spots(paths, g).size() == 1);
    std::vector<save::PathTile> ring(2, paths[1]);
    ring[1].row = static_cast<std::uint8_t>(ring[0].row + 1);
    ring[0].queue->end_direction = 3;
    ring[1].queue->end_direction = 2;
    CHECK(queue_sign_spots(ring, g).empty());
}

TEST_CASE("a theme's queue sign is the sign whose setting says so")
{
    ovl::SceneryItem plain;
    plain.type = std::to_underlying(ItemCategory::Sign);
    plain.parameters = { { "Sign", " EnableNoEntry" } };
    ovl::SceneryItem queue = plain;
    queue.parameters = { { "Sign", " EnableNoEntry queuesign" } };
    ovl::SceneryItem other = queue;
    other.type = std::to_underlying(ItemCategory::RideEntrance);
    const std::vector<ovl::SceneryItemEntry> items { { "Other", other }, { "NoEntry", plain }, { "Arch", queue } };
    CHECK(queue_sign_item(items) == "Arch");
    CHECK(queue_sign_item(std::span(items).first(2)).empty());
}

TEST_CASE("the board's text loops and scrolls round in lamps")
{
    ovl::RgbaImage page;
    page.width = page.height = 4;
    page.pixels.assign(4 * 4 * 4, 255);
    const std::vector<ovl::RgbaImage> pages { page };
    const std::array glyphs { ui::CodedGlyph { u'A', { { 0, -4, 4, 0 }, { 0, 1, 1, 0 }, 0, 5 } } };
    const ui::Font font = ui::Font::from_glyphs(glyphs, 8, 0);
    const ovl::RgbaImage loop = sign_text_loop(font, pages, u"AA?", 8);
    // Two glyphs of 5 and a missing one, then the gap, out to whole lamps.
    REQUIRE(kSignLamp == 3); // The places below count on it
    CHECK(loop.width == 27);
    CHECK(loop.height == 8);
    const auto at = [&](const ovl::RgbaImage& image, std::uint32_t x, std::uint32_t y, std::size_t k = 0) { return image.pixels[(y * image.width + x) * 4 + k]; };
    // The glyph stands on the baseline, the line's middle on the board's.
    CHECK(at(loop, 0, 5) == 255);
    CHECK(at(loop, 4, 5) == 0);
    CHECK(at(loop, 0, 1) == 0);

    // Lamps are 3 by 3, lit where the letters cover their middles (1, 4, 7, ...).
    const LampColour red { 255, 0, 0 };
    const ovl::RgbaImage board = sign_window(loop, 0, 9, red);
    CHECK(board.width == 9);
    CHECK(at(board, 1, 4) == 255); // A lit lamp's middle
    CHECK(at(board, 1, 4, 1) == kSignDark);
    CHECK(at(board, 4, 4) == static_cast<std::uint8_t>(255 * kSignLampOff)); // Between the glyphs: unlit
    CHECK(at(board, 1, 1) == static_cast<std::uint8_t>(255 * kSignLampOff)); // Above them
    CHECK(at(board, 0, 3) == static_cast<std::uint8_t>(255 * kSignGlow));    // Round a lit dot
    const ovl::RgbaImage moved = sign_window(loop, loop.width - kSignLamp, 9, red);
    CHECK(at(moved, 4, 4) == 255); // The first lamp's letters, one lamp on
    CHECK(at(moved, 1, 4) < 255);

    CHECK(sign_shift(0, 100) == 0);
    CHECK(sign_shift(1, 1000) == static_cast<std::uint32_t>(kSignScroll) / kSignLamp * kSignLamp);
    CHECK(sign_shift(1, 1000) % kSignLamp == 0);
    CHECK(sign_shift(1, 12) < 12);
}
