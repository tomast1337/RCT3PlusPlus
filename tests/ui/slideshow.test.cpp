// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/slideshow.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

using namespace rct3::ui;
namespace fs = std::filesystem;

namespace
{
    std::vector<fs::path> names(std::initializer_list<const char*> list)
    {
        std::vector<fs::path> out;
        for (const char* n : list)
            out.emplace_back(n);
        return out;
    }
} // namespace

TEST_CASE("each picture fades in over the last, then stays")
{
    Slideshow show(names({ "a.jpg", "b.jpg", "c.jpg" }));
    auto s = show.shown();
    CHECK(s.under == 0);
    CHECK(s.over == 1);
    CHECK(s.over_alpha == 0);
    show.update(0.5f);
    CHECK(show.shown().over_alpha == doctest::Approx(0.5f));
    show.update(2);
    CHECK(show.shown().over_alpha == 1);
    show.update(1.5f); // Four seconds in: the next one starts
    s = show.shown();
    CHECK(s.under == 1);
    CHECK(s.over == 2);
    CHECK(s.over_alpha == doctest::Approx(0));
    show.update(8); // Two more pictures, round to the first
    s = show.shown();
    CHECK(s.under == 0);
    CHECK(s.over == 1);
    show.update(4);
    CHECK(show.shown().over == 2);
}

TEST_CASE("one picture fades in over itself, and none shows nothing")
{
    Slideshow one(names({ "a.jpg" }));
    CHECK(one.shown().under == 0);
    CHECK(one.shown().over == 0);
    one.update(10);
    CHECK(one.shown().over == 0);

    Slideshow none;
    none.update(10);
    CHECK_FALSE(none.shown().under);
    CHECK_FALSE(none.shown().over);
}

TEST_CASE("a new list starts at its beginning")
{
    Slideshow show(names({ "a.jpg", "b.jpg", "c.jpg" }));
    show.update(4.5f);
    show.set_pictures(names({ "x.jpg", "y.jpg" }));
    const auto s = show.shown();
    CHECK(s.under == 0);
    CHECK(s.over == 1);
    CHECK(s.over_alpha == doctest::Approx(0.5f)); // The fade carries on
}

TEST_CASE("the pictures fill the top right beyond the artwork")
{
    const Rect r = slideshow_rect(1024, 768);
    CHECK(r.x0 == doctest::Approx(74));
    CHECK(r.y0 == 0);
    CHECK(r.x1 == 1024);
    CHECK(r.y1 == doctest::Approx(768 - 55.5f));
    const Rect wide = slideshow_rect(1920, 1080);
    CHECK(wide.x0 == doctest::Approx(138.75f));
    CHECK(wide.y1 == doctest::Approx(1080 - 74 * 1080 / 768.0 * 1080 / 1920));
    const Rect empty = slideshow_rect(0, 0);
    CHECK(empty.width() == 0);
}

TEST_CASE("the list mixes the folders' pictures")
{
    const fs::path dir = fs::temp_directory_path() / "rct3pp_slideshow_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "game");
    fs::create_directories(dir / "user");
    for (const char* n : { "game/1.jpg", "game/2.jpg", "game/3.JPG", "game/notes.txt", "user/Shot0001.bmp", "user/Shot0002.png" })
        std::ofstream(dir / n) << "x";
    const std::vector<fs::path> folders { dir / "game", dir / "user", dir / "missing" };
    auto list = slideshow_pictures(folders, 7);
    CHECK(list == slideshow_pictures(folders, 7)); // The same seed, the same order
    std::ranges::sort(list);
    CHECK(list == std::vector { dir / "game/1.jpg", dir / "game/2.jpg", dir / "game/3.JPG", dir / "user/Shot0001.bmp", dir / "user/Shot0002.png" });
    fs::remove_all(dir);
}
