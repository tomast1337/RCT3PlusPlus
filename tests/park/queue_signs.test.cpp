// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_signs.hpp"

#include "park/footpath_builder.hpp"
#include "park/item_category.hpp"

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
    std::vector<save::PathTile> lay_line(const save::TerrainGrid& g, PathSpot from, std::uint8_t heading, int count, std::uint64_t line)
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
    CHECK(spots[0].heading == 0);

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

TEST_CASE("the board's text loops and scrolls round")
{
    ovl::RgbaImage page;
    page.width = page.height = 4;
    page.pixels.assign(4 * 4 * 4, 255);
    const std::vector<ovl::RgbaImage> pages { page };
    const std::array glyphs { ui::CodedGlyph { u'A', { { 0, -4, 2, 0 }, { 0, 1, 0.5f, 0 }, 0, 3 } } };
    const ui::Font font = ui::Font::from_glyphs(glyphs, 8, 0);
    const ovl::RgbaImage loop = sign_text_loop(font, pages, u"AA?", 8);
    // Two glyphs of 3 and a missing one, then the gap.
    CHECK(loop.width == 6 + kSignGap * 8);
    CHECK(loop.height == 8);
    const auto grey = [&](const ovl::RgbaImage& image, std::uint32_t x, std::uint32_t y) { return image.pixels[(y * image.width + x) * 4]; };
    // The glyph stands on the baseline, the line's middle on the board's.
    CHECK(grey(loop, 0, 5) == kSignInk);
    CHECK(grey(loop, 2, 5) == kSignDark);
    CHECK(grey(loop, 5, 5) == kSignDark);
    CHECK(grey(loop, 0, 1) == kSignDark);

    const ovl::RgbaImage window = sign_window(loop, loop.width - 1, 2);
    CHECK(window.width == 2);
    CHECK(grey(window, 0, 5) == kSignDark);
    CHECK(grey(window, 1, 5) == kSignInk);

    CHECK(sign_shift(0, 10) == 0);
    CHECK(sign_shift(1, 1000) == static_cast<std::uint32_t>(kSignScroll));
    CHECK(sign_shift(1, 10) < 10);
}
