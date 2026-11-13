// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management/window.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using namespace rct3::ui::widgets;
using namespace rct3::ui::screens;
using namespace rct3::ui::screens::management;

namespace
{
    struct Fixture
    {
        Desktop desktop;
        ManagementWindow* window = nullptr;

        Fixture()
        {
            std::vector<ManagementTab> tabs { { "IconA", u"Lists" }, { "IconB", u"Sums" } };
            window = &desktop.root().add<ManagementWindow>("window", game_skins::management_window_style(kGamePalette), 400, 300, std::move(tabs), 0);
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }

        void click(Point at)
        {
            desktop.update({ at, true, true });
            desktop.update({ at, false, true });
        }
    };

    ManagementPage table_page()
    {
        ManagementPage page;
        page.columns = { { u"Name", 0, Align::Start, true }, { u"Cost", 60, Align::End, true } };
        page.rows = { { { u"Arcade", u"5" }, { std::nullopt, 5 }, 10 }, { { u"Carousel", u"30" }, { std::nullopt, 30 }, 20 }, { { u"Bumper cars", u"2" }, { std::nullopt, 2 }, 30 } };
        return page;
    }

    std::vector<std::size_t> shown_ids(const ManagementWindow& w)
    {
        std::vector<std::size_t> out;
        for (const TableRow& r : w.table()->rows)
            out.push_back(r.id);
        return out;
    }
} // namespace

TEST_CASE("picking a tab opens it and reports it once")
{
    Fixture f;
    CHECK(f.window->tab() == 0);
    CHECK(f.window->tab_button(0).on);
    f.window->tab_button(1).click();
    f.desktop.update({ });
    CHECK(f.window->tab() == 1);
    CHECK_FALSE(f.window->tab_button(0).on);
    CHECK(f.window->take_tab() == std::optional<std::size_t>(1));
    CHECK_FALSE(f.window->take_tab());
}

TEST_CASE("a page without columns has no table")
{
    Fixture f;
    ManagementPage page;
    page.lines = { { u"Cash", u"100" } };
    f.window->show_page(page);
    CHECK_FALSE(f.window->table()->visible);
    f.window->show_page(table_page());
    CHECK(f.window->table()->visible);
}

TEST_CASE("the page's own sort holds until the player picks a column, which then outlives new pages")
{
    Fixture f;
    ManagementPage page = table_page();
    page.sort = 1;
    f.window->show_page(page);
    f.desktop.layout({ 0, 0, 1024, 768 });
    CHECK(shown_ids(*f.window) == std::vector<std::size_t> { 30, 10, 20 });

    // Press the name heading: by name, lowest first.
    const Rect& r = f.window->table()->rect();
    f.click({ r.x0 + 10, r.y0 + 5 });
    CHECK(shown_ids(*f.window) == std::vector<std::size_t> { 10, 30, 20 });
    f.window->show_page(page);
    CHECK(shown_ids(*f.window) == std::vector<std::size_t> { 10, 30, 20 });
}

TEST_CASE("the selection follows its row into a new page")
{
    Fixture f;
    f.window->show_page(table_page());
    f.window->table()->select(1); // Carousel
    ManagementPage page = table_page();
    std::swap(page.rows[0], page.rows[1]);
    f.window->show_page(page);
    REQUIRE(f.window->table()->selected());
    CHECK(f.window->table()->rows.at(*f.window->table()->selected()).id == 20);
}

TEST_CASE("an enabled button under the page reports its press; a stub does not")
{
    Fixture f;
    ManagementPage page;
    page.buttons = { { { }, u"Open all", { }, true }, { { }, u"Close all", { }, false } };
    f.window->show_page(page);
    f.desktop.layout({ 0, 0, 1024, 768 });
    auto open = dynamic_cast<Button*>(rct3::core::as_pointer(f.window->find("content/page/lines/button0")));
    auto close = dynamic_cast<Button*>(rct3::core::as_pointer(f.window->find("content/page/lines/button1")));
    REQUIRE(open != nullptr);
    REQUIRE(close != nullptr);
    CHECK_FALSE(close->enabled);
    open->click();
    f.desktop.update({ });
    CHECK(f.window->take_button() == std::optional<std::size_t>(0));
}

TEST_CASE("a button with choices fans them out and reports the one picked, not its press")
{
    Fixture f;
    ManagementPage page;
    page.buttons = { { "All", { }, { }, true, { { "Yes", { } }, { "No", { } } } } };
    f.window->show_page(page);
    f.desktop.layout({ 0, 0, 1024, 768 });
    auto all = dynamic_cast<Button*>(rct3::core::as_pointer(f.window->find("content/page/lines/All")));
    REQUIRE(all != nullptr);
    CHECK_FALSE(f.window->find("content/page/lines/No"));
    all->click();
    f.desktop.update({ });
    CHECK_FALSE(f.window->take_button());
    f.window->show_page(page); // A page coming again keeps the fan open
    f.desktop.layout({ 0, 0, 1024, 768 });
    auto no = dynamic_cast<Button*>(rct3::core::as_pointer(f.window->find("content/page/lines/No")));
    REQUIRE(no != nullptr);
    no->click();
    f.desktop.update({ });
    CHECK(f.window->take_choice() == std::optional(std::pair<std::size_t, std::size_t>(0, 1)));
    CHECK_FALSE(f.window->find("content/page/lines/No"));
}

TEST_CASE("a graph's span is picked by its ranges or zoomed by the wheel, and reads out the week under the pointer")
{
    Fixture f;
    ManagementPage page;
    std::vector<float> values(64);
    std::vector<std::u16string> readings(64);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = static_cast<float>(i);
        readings[i] = std::u16string(1, static_cast<char16_t>(u'A' + i % 26));
    }
    page.graph = { { .name = u"Cash", .values = values, .key = "cash", .readings = readings } };
    page.graph_slots = 64;
    page.graph_ranges = { { u"Month", 4 }, { u"All", 0 } };
    page.graph_when = { u"Newest", u"Older" };
    for (std::size_t back = 0; back < 64; back += 4)
        page.graph_ticks.push_back({ static_cast<float>(back), u"M" });
    f.window->show_page(page);
    f.desktop.layout({ 0, 0, 1024, 768 });
    auto plot = dynamic_cast<LinePlot*>(rct3::core::as_pointer(f.window->find("content/page/lines/graph")));
    auto month = dynamic_cast<Button*>(rct3::core::as_pointer(f.window->find("content/page/lines/range0")));
    REQUIRE(plot != nullptr);
    REQUIRE(month != nullptr);
    CHECK(plot->span() == 64);
    const auto marks = [&] {
        std::size_t n = 0;
        for (const auto& w : plot->parent()->children())
            n += w->name().starts_with("mark") ? 1 : 0;
        return n;
    };
    CHECK(marks() == 8); // Every other one of the sixteen

    month->click();
    f.desktop.update({ });
    CHECK(plot->span() == 4);
    CHECK(month->kept_down);
    CHECK(marks() == 1);
    // A new page keeps the span picked.
    f.window->show_page(page);
    f.desktop.layout({ 0, 0, 1024, 768 });
    plot = dynamic_cast<LinePlot*>(rct3::core::as_pointer(f.window->find("content/page/lines/graph")));
    REQUIRE(plot != nullptr);
    CHECK(plot->span() == 4);

    // The newest week at the right edge, read out with its time.
    const Rect r = plot->rect();
    f.desktop.update({ .position = { r.x1 - 1, (r.y0 + r.y1) / 2 } });
    f.desktop.update({ .position = { r.x1 - 1, (r.y0 + r.y1) / 2 } });
    CHECK(plot->tooltip == u"Newest\nCash: L"); // values[63]: 'A' + 63 % 26
    f.desktop.update({ .position = { r.x1 - 1, (r.y0 + r.y1) / 2 }, .wheel = { 0, -1 } });
    f.desktop.update({ .position = { r.x1 - 1, (r.y0 + r.y1) / 2 } });
    CHECK(plot->span() == 5);
    CHECK_FALSE(month->kept_down);
}
