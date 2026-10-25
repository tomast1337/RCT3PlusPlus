// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/design_browser.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        Desktop desktop;
        DesignBrowser* window = nullptr;

        Fixture()
        {
            std::vector<TableColumn> columns { { u"Name", 0, Align::Start, true }, { u"Excitement", 80, Align::End, true } };
            std::vector<TableRow> rows { { { u"Zephyr", u"3.00" }, { std::nullopt, 3 }, 1 }, { { u"Custom track", u"" }, { }, 0 }, { { u"Avalanche", u"7.00" }, { std::nullopt, 7 }, 2 } };
            window = &desktop.root().add<DesignBrowser>("designs", game_skins::design_browser_style(kGamePalette), u"Designs", "", std::move(columns), std::move(rows), u"Build", u"Cancel");
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }

        void click(Point at)
        {
            desktop.update({ at, true, true });
            desktop.update({ at, false, true });
        }

        [[nodiscard]] std::vector<std::size_t> ids() const
        {
            std::vector<std::size_t> out;
            for (const TableRow& r : window->table().rows)
                out.push_back(r.id);
            return out;
        }
    };
} // namespace

TEST_CASE("the custom track stays the first row however the designs are sorted")
{
    Fixture f;
    CHECK(f.ids() == std::vector<std::size_t> { 0, 2, 1 });
    // Press the excitement heading twice: highest first.
    const Rect& r = f.window->table().rect();
    f.click({ r.x1 - 30, r.y0 + 5 });
    f.click({ r.x1 - 30, r.y0 + 5 });
    CHECK(f.ids() == std::vector<std::size_t> { 0, 2, 1 });
    f.click({ r.x1 - 30, r.y0 + 5 });
    CHECK(f.ids() == std::vector<std::size_t> { 0, 1, 2 });
}

TEST_CASE("the window reports the custom track picked as it opens, then each row picked once")
{
    Fixture f;
    CHECK(f.window->take_picked() == std::optional<std::size_t>(0));
    CHECK_FALSE(f.window->take_picked());
    f.window->table().select(2);
    f.desktop.update({ });
    CHECK(f.window->take_picked() == std::optional<std::size_t>(1));
}

TEST_CASE("a locked design cannot be built")
{
    Fixture f;
    f.window->table().select(1);
    f.desktop.update({ });
    f.window->show_design(0, { { u"Locked", { } } }, false);
    auto build = dynamic_cast<Button*>(rct3::as_pointer(f.window->find("content/build")));
    REQUIRE(build != nullptr);
    build->click();
    f.desktop.update({ });
    CHECK_FALSE(f.window->take_build());
    f.window->show_design(0, { }, true);
    build->click();
    f.desktop.update({ });
    CHECK(f.window->take_build() == std::optional<std::size_t>(2));
}
