// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/button.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    SkinRef skin(TextureId texture)
    {
        SkinRef s;
        s.layout.xs = { 0, 8 };
        s.layout.ys = { 0, 8 };
        s.layout.x_count = 2;
        s.layout.y_count = 2;
        s.texture = texture;
        s.texture_width = 16;
        s.texture_height = 16;
        return s;
    }

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }

    struct Fixture
    {
        Desktop desktop;
        Button& button = desktop.root().add<Button>("b", "Btn", "Icon");
        Fixture()
        {
            button.placement = top_left(0, 0, 20, 20);
            desktop.layout({ 0, 0, 100, 100 });
        }
        void click(float x = 5)
        {
            desktop.update(at(5, 5, true));
            desktop.update(at(x, 5, false));
        }
    };
} // namespace

TEST_CASE("a click is a press and release over the button")
{
    Fixture f;
    CHECK_FALSE(f.button.take_click());
    f.click();
    CHECK(f.button.take_click());
    CHECK_FALSE(f.button.take_click());

    f.click(60); // Released outside
    CHECK_FALSE(f.button.take_click());
}

TEST_CASE("the state follows the pointer")
{
    Fixture f;
    CHECK(f.button.state() == SkinState::Up);
    f.desktop.update(at(5, 5));
    CHECK(f.button.state() == SkinState::UpOver);
    f.desktop.update(at(5, 5, true));
    CHECK(f.button.state() == SkinState::Pressed);
    f.desktop.update(at(5, 5, false));
    f.button.enabled = false;
    CHECK(f.button.state() == SkinState::Disabled);
}

TEST_CASE("a toggle stays down until clicked again")
{
    Fixture f;
    f.button.toggle = true;
    f.click();
    CHECK(f.button.on);
    CHECK(f.button.state() == SkinState::DownOver);
    f.desktop.update(at(60, 60));
    CHECK(f.button.state() == SkinState::Down);
    f.click();
    CHECK_FALSE(f.button.on);
}

TEST_CASE("buttons draw the family member for their state, then the icon")
{
    Fixture f;
    SkinLibrary skins;
    skins.add("BtnUp", skin(1));
    skins.add("BtnUpOver", skin(2));
    skins.add("Icon", skin(7));
    DrawList list;
    const FontSet fonts;
    DrawContext context { list, skins, fonts };

    f.desktop.update(at(5, 5));
    f.desktop.draw(context);
    REQUIRE(list.batches().size() == 2);
    CHECK(list.batches()[0].texture == 2);
    CHECK(list.batches()[1].texture == 7);
    // The icon keeps its size and is centred.
    const Vertex& icon = list.vertices()[4];
    CHECK(icon.x == 6);
    CHECK(icon.y == 6);

    SUBCASE("a missing pressed member tints the resting one")
    {
        list.clear();
        f.desktop.update(at(5, 5, true));
        f.desktop.draw(context);
        CHECK(list.batches()[0].texture == 1);
        CHECK(list.vertices()[0].color[0] < 255);
    }
}

TEST_CASE("a toggle whose face has one picture shows lit while on and dimmed while off")
{
    Fixture f;
    f.button.toggle = true;
    SkinLibrary skins;
    skins.add("Btn", skin(1));
    DrawList list;
    const FontSet fonts;
    DrawContext context { list, skins, fonts };
    f.desktop.draw(context);
    CHECK(list.vertices()[0].color[0] < 255);
    f.button.on = true;
    list.clear();
    f.desktop.draw(context);
    CHECK(list.vertices()[0].color[0] == 255);
}

TEST_CASE("text is centred in the button, grey when disabled and cut to the button")
{
    rct3::ovl::Font f;
    f.metrics = { 0.1f, 1.0f, 0.2f };
    f.has_code_table = true;
    f.glyphs.push_back({ u'x', 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
    const std::array<Font::Page, 1> pages = { { { 9, 100, 100 } } };
    FontSet fonts;
    fonts.set(FontRole::Small, Font::from(f, pages));
    const SkinLibrary skins;

    Desktop desktop;
    Button& b = desktop.root().add<Button>("b", "Btn");
    b.text = u"xx";
    b.placement = top_left(10, 10, 40, 20);
    desktop.layout({ 0, 0, 100, 100 });

    const auto draw = [&] {
        DrawList list;
        DrawContext context { list, skins, fonts };
        desktop.draw(context);
        CHECK(!list.clip());
        return list;
    };
    DrawList list = draw();
    REQUIRE(list.vertices().size() == 8);
    CHECK(list.vertices()[0].x == 20); // 20 wide text in a 40 wide button at 10
    CHECK(list.vertices()[0].color == std::array<std::uint8_t, 4> { 0, 0, 0, 255 });

    b.enabled = false;
    list = draw();
    REQUIRE(list.vertices().size() == 8);
    CHECK(list.vertices()[0].color == std::array<std::uint8_t, 4> { 127, 127, 127, 255 });

    b.text_inset = { 10, 0, 0, 0 };
    list = draw();
    CHECK(list.vertices()[0].x == 25);

    b.text = u"xxxxxxxxxxxx";
    list = draw();
    for (const Vertex& v : list.vertices())
        CHECK((v.x >= 10 && v.x <= 50));
}

TEST_CASE("space and enter click a focused button")
{
    Button b("b", "Skin");
    CHECK(b.takes_focus());
    CHECK(b.on_key({ Key::Space }));
    CHECK(b.on_key({ Key::Enter }));
    CHECK_FALSE(b.on_key({ Key::Escape }));
    CHECK(b.take_click());
    CHECK(b.take_click());
    CHECK_FALSE(b.take_click());

    b.toggle = true;
    CHECK(b.on_key({ Key::Space }));
    CHECK(b.on);
}

TEST_CASE("a swelling button grows its face while the pointer is over it")
{
    Fixture f;
    f.button.swell = 4;
    f.button.swell_time = 0.1f;
    UpdateContext half;
    half.elapsed = 0.05f;

    f.desktop.update(at(5, 5), half);
    CHECK(f.button.face() == Rect { -2, -2, 22, 22 }); // Half way, eased
    f.desktop.update(at(5, 5), half);
    CHECK(f.button.face() == Rect { -4, -4, 24, 24 });
    f.desktop.update(at(5, 5), half);
    CHECK(f.button.face() == Rect { -4, -4, 24, 24 });
    // Only the face grows: the pointer just outside still misses it.
    CHECK(!f.desktop.hit({ 21, 5 }));

    f.desktop.update(at(90, 90), half);
    CHECK(f.button.face() == Rect { -2, -2, 22, 22 });
    f.desktop.update(at(90, 90), half);
    CHECK(f.button.face() == f.button.rect());
}

TEST_CASE("buttons without a swell keep their face")
{
    Fixture f;
    UpdateContext later;
    later.elapsed = 1;
    f.desktop.update(at(5, 5), later);
    CHECK(f.button.face() == f.button.rect());
}

TEST_CASE("an inert button shows the pointer it does nothing")
{
    Button b("b", "skin");
    CHECK(b.cursor({ }) == CursorKind::Hand);
    b.inert = true;
    CHECK(b.cursor({ }) == CursorKind::NotAllowed);
}
