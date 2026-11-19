// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/column_table.hpp"

#include "core/types.hpp"
#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using namespace rct3::ui::widgets;

namespace
{
    TableRow row(std::u16string name, std::optional<f64> number, usize id)
    {
        return { { std::move(name), u"" }, { std::nullopt, number }, id };
    }

    std::vector<usize> ids(const std::vector<TableRow>& rows)
    {
        std::vector<usize> out;
        for (const TableRow& r : rows)
            out.push_back(r.id);
        return out;
    }

    ScrollBarStyle bar_style()
    {
        ScrollBarStyle s;
        s.thickness = 10;
        return s;
    }

    // Headings 20 tall, then rows 20 tall; no padding.
    ColumnTableStyle table_style()
    {
        ColumnTableStyle s;
        s.heading_height = 20;
        s.row_height = 20;
        s.padding = { };
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        ColumnTable& table = desktop.root().add<ColumnTable>("table", table_style(), bar_style());

        Fixture()
        {
            table.placement = top_left(0, 0, 110, 100);
            table.columns = { { u"Name", 0, Align::Start, true }, { u"Cost", 40, Align::End, true }, { u"Note", 20, Align::Start, false } };
            for (usize i = 0; i < 6; ++i)
                table.rows.push_back(row(u"Ride", static_cast<f64>(i), i));
            desktop.layout({ 0, 0, 300, 300 });
            desktop.update({ { 200, 200 }, false, true });
        }

        void click(f32 x, f32 y)
        {
            desktop.update({ { x, y }, true, true });
            desktop.update({ { x, y }, false, true });
        }
    };
} // namespace

TEST_CASE("rows sort by their numbers, then by their text")
{
    std::vector<TableRow> rows { row(u"Carousel", 30, 0), row(u"Bumper cars", std::nullopt, 1), row(u"Arcade", 5, 2), row(u"Ferris wheel", std::nullopt, 3) };
    SUBCASE("lowest first")
    {
        sort_table_rows(rows, 1, false);
        CHECK(ids(rows) == std::vector<usize> { 2, 0, 1, 3 });
    }
    SUBCASE("highest first keeps rows without a number last, in their order")
    {
        sort_table_rows(rows, 1, true);
        CHECK(ids(rows) == std::vector<usize> { 0, 2, 1, 3 });
    }
    SUBCASE("by text")
    {
        sort_table_rows(rows, 0, false);
        CHECK(ids(rows) == std::vector<usize> { 2, 1, 0, 3 });
    }
}

TEST_CASE("ties keep their order")
{
    std::vector<TableRow> rows { row(u"B", 1, 0), row(u"A", 1, 1), row(u"C", 1, 2) };
    sort_table_rows(rows, 1, true);
    CHECK(ids(rows) == std::vector<usize> { 0, 1, 2 });
}

TEST_CASE("the shared column takes what the fixed ones leave")
{
    Fixture f;
    const auto edges = f.table.column_edges();
    REQUIRE(edges.size() == 3);
    // 110 wide less the 10 unit bar leaves 100: 40 for the name.
    CHECK(edges[0].right == doctest::Approx(40));
    CHECK(edges[1].left == doctest::Approx(40));
    CHECK(edges[2].right == doctest::Approx(100));
}

TEST_CASE("pressing a heading asks to sort by it, unless it cannot sort")
{
    Fixture f;
    f.click(60, 5);
    CHECK(f.table.take_sort() == std::optional<usize>(1));
    CHECK_FALSE(f.table.take_sort());
    f.click(90, 5);
    CHECK_FALSE(f.table.take_sort());
}

TEST_CASE("pressing a row selects it and pressing it again activates it")
{
    Fixture f;
    f.click(20, 45); // Second row
    CHECK(f.table.selected() == std::optional<usize>(1));
    CHECK_FALSE(f.table.take_activate());
    f.click(20, 45);
    CHECK(f.table.take_activate());
    CHECK(f.table.row_at({ 20, 10 }) == std::nullopt); // The headings are not a row
}

TEST_CASE("the pointer is a hand over rows and the headings that sort")
{
    Fixture f;
    CHECK(f.table.cursor({ 20, 45 }) == CursorKind::Hand);
    CHECK(f.table.cursor({ 60, 5 }) == CursorKind::Hand);
    CHECK(f.table.cursor({ 90, 5 }) == CursorKind::Arrow); // Cannot sort
}
