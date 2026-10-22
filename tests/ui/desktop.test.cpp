// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/desktop.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <format>
#include <initializer_list>
#include <vector>

using namespace rct3::ui;

namespace
{
    // Takes the pointer and records what happened to it.
    class Probe : public Widget
    {
    public:
        using Widget::Widget;
        bool blocks = true;
        std::vector<std::string> events;
        int updates = 0;

        [[nodiscard]] bool takes_pointer() const override
        {
            return blocks;
        }
        void on_press(Point p) override
        {
            events.push_back(std::format("press {}", static_cast<int>(p.x)));
        }
        void on_drag(Point p) override
        {
            events.push_back(std::format("drag {}", static_cast<int>(p.x)));
        }
        void on_release(Point, bool inside) override
        {
            events.push_back(inside ? "release inside" : "release outside");
        }
        void update(const UpdateContext&) override
        {
            ++updates;
        }
    };

    constexpr Rect kView { 0, 0, 200, 100 };

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }
} // namespace

TEST_CASE("layout places the tree in the view")
{
    Desktop desktop;
    Widget& w = desktop.root().add<Widget>("w");
    w.placement = top_right(10, 10, 50, 20);
    Widget& inner = w.add<Widget>("inner");
    inner.placement = fill(5);
    desktop.layout(kView);
    CHECK(w.rect() == Rect { 140, 10, 190, 30 });
    CHECK(inner.rect() == Rect { 145, 15, 185, 25 });

    desktop.layout({ 0, 0, 400, 300 });
    CHECK(w.rect() == Rect { 340, 10, 390, 30 });
}

TEST_CASE("keep_inside brings a widget back when the view shrinks")
{
    Desktop desktop;
    Widget& w = desktop.root().add<Widget>("w");
    w.placement = top_left(150, 10, 40, 20);
    w.keep_inside = true;
    desktop.layout({ 0, 0, 100, 100 });
    CHECK(w.rect() == Rect { 60, 10, 100, 30 });
    // It stays where it was put back.
    desktop.layout(kView);
    CHECK(w.rect() == Rect { 60, 10, 100, 30 });
}

TEST_CASE("the pointer goes to the top-most widget that takes it")
{
    Desktop desktop;
    Probe& back = desktop.root().add<Probe>("back");
    back.placement = top_left(0, 0, 100, 100);
    Probe& front = desktop.root().add<Probe>("front");
    front.placement = top_left(50, 0, 100, 100);
    Widget& label = front.add<Widget>("label"); // Lets the pointer through to its parent
    label.placement = fill();
    desktop.layout(kView);

    CHECK(rct3::as_pointer(desktop.hit({ 10, 10 })) == &back);
    CHECK(rct3::as_pointer(desktop.hit({ 60, 10 })) == &front);
    CHECK_FALSE(rct3::as_pointer(desktop.hit({ 190, 90 })) == &back);
    CHECK_FALSE(rct3::as_pointer(desktop.hit({ 199, 99 })) == &label);

    SUBCASE("invisible widgets and their children are skipped")
    {
        front.visible = false;
        CHECK(rct3::as_pointer(desktop.hit({ 60, 10 })) == &back);
    }
    SUBCASE("update reports whether the interface has the pointer")
    {
        CHECK(desktop.update(at(60, 10)));
        CHECK(front.hovered());
        CHECK_FALSE(back.hovered());
        desktop.root().remove(back);
        desktop.root().remove(front);
        CHECK_FALSE(desktop.update(at(60, 10)));
    }
}

TEST_CASE("a pressed widget holds the pointer until release")
{
    Desktop desktop;
    Probe& p = desktop.root().add<Probe>("p");
    p.placement = top_left(0, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10, true));
    CHECK(p.pressed());
    desktop.update(at(120, 10, true));
    CHECK_FALSE(p.pressed()); // Held, but the pointer is outside
    CHECK_FALSE(p.hovered());
    desktop.update(at(120, 10, false));
    CHECK(p.events == std::vector<std::string> { "press 10", "drag 120", "release outside" });

    SUBCASE("released over it")
    {
        p.events.clear();
        desktop.update(at(10, 10, true));
        desktop.update(at(12, 10, false));
        CHECK(p.events == std::vector<std::string> { "press 10", "release inside" });
    }
    SUBCASE("a press that starts outside never reaches it")
    {
        p.events.clear();
        desktop.update(at(120, 10, true));
        desktop.update(at(10, 10, true));
        desktop.update(at(10, 10, false));
        CHECK(p.events.empty());
    }
    SUBCASE("disabled widgets take the pointer but are not pressed")
    {
        p.events.clear();
        p.enabled = false;
        CHECK(desktop.update(at(10, 10, true)));
        CHECK(p.events.empty());
    }
    SUBCASE("an absent pointer releases outside")
    {
        p.events.clear();
        desktop.update(at(10, 10, true));
        desktop.update({ { 10, 10 }, true, false });
        CHECK(p.events == std::vector<std::string> { "press 10", "release outside" });
    }
}

TEST_CASE("pressing a top-level widget raises it and gives it focus")
{
    Desktop desktop;
    Probe& a = desktop.root().add<Probe>("a");
    a.placement = top_left(0, 0, 100, 100);
    Widget& a_child = a.add<Widget>("child");
    Probe& b = desktop.root().add<Probe>("b");
    b.placement = top_left(150, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(190, 90));
    CHECK(b.focused());
    CHECK_FALSE(a.focused());

    desktop.update(at(10, 10, true));
    CHECK(desktop.root().children().back().get() == &a);
    CHECK(a.focused());
    CHECK(a_child.focused());
    CHECK_FALSE(b.focused());
}

TEST_CASE("a top-level widget that stays behind is not raised when pressed")
{
    Desktop desktop;
    Probe& a = desktop.root().add<Probe>("a");
    a.placement = top_left(0, 0, 100, 100);
    a.stays_behind = true;
    Probe& b = desktop.root().add<Probe>("b");
    b.placement = top_left(150, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10, true));
    CHECK(desktop.root().children().back().get() == &b);
}

TEST_CASE("closing widgets are detached after the update")
{
    Desktop desktop;
    Probe& a = desktop.root().add<Probe>("a");
    a.placement = top_left(0, 0, 50, 50);
    Probe& b = desktop.root().add<Probe>("b");
    b.placement = top_left(100, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10, true)); // a holds the pointer
    CHECK(a.updates == 1);
    a.close();
    desktop.update(at(10, 10, true));
    REQUIRE(desktop.root().children().size() == 1);
    CHECK(desktop.root().children()[0].get() == &b);
    // The held widget is gone; the release goes nowhere.
    CHECK_FALSE(desktop.update(at(150, 90, false)));
}

TEST_CASE("clipped parents hide children outside them")
{
    Desktop desktop;
    Widget& box = desktop.root().add<Widget>("box");
    box.placement = top_left(0, 0, 50, 50);
    box.clip_children = true;
    Probe& p = box.add<Probe>("p");
    p.placement = top_left(40, 0, 40, 10);
    desktop.layout(kView);

    CHECK(rct3::as_pointer(desktop.hit({ 45, 5 })) == &p);
    CHECK_FALSE(desktop.hit({ 60, 5 }));

    DrawList list;
    SkinLibrary skins;
    const FontSet fonts;
    DrawContext context { list, skins, fonts };
    desktop.draw(context);
    CHECK(!list.clip()); // Balanced
}

// Keyboard focus and key routing.
namespace
{
    // Records keys; uses only the ones listed in `uses`.
    class KeyProbe : public Widget
    {
    public:
        using Widget::Widget;
        bool focusable = false;
        bool blocks = true;
        std::vector<Key> uses;
        std::vector<Key> seen;

        [[nodiscard]] bool takes_pointer() const override
        {
            return blocks;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return focusable;
        }
        bool on_key(const KeyPress& key) override
        {
            seen.push_back(key.key);
            return std::ranges::contains(uses, key.key);
        }
    };

    KeyboardState keys(std::initializer_list<Key> list)
    {
        KeyboardState k;
        for (Key key : list)
            k.presses.push_back({ key });
        return k;
    }

    struct Fixture
    {
        Desktop desktop;
        KeyProbe& back = desktop.root().add<KeyProbe>("back");
        KeyProbe& top = desktop.root().add<KeyProbe>("top");
        KeyProbe& field = top.add<KeyProbe>("field");
        KeyProbe& inner = field.add<KeyProbe>("inner");

        Fixture()
        {
            back.placement = top_left(0, 0, 50, 50);
            top.placement = top_left(100, 0, 100, 100);
            field.placement = top_left(10, 10, 40, 40);
            inner.placement = fill(5);
            inner.blocks = false; // A label, say: pressing it presses the field
            field.focusable = true;
            desktop.layout(kView);
        }
    };
} // namespace

TEST_CASE("without a focused widget, keys go to the top-most window")
{
    Fixture f;
    f.desktop.update(at(0, 0), keys({ Key::Escape }));
    CHECK(f.top.seen == std::vector { Key::Escape });
    CHECK(f.back.seen.empty());
    CHECK(f.field.seen.empty());
    CHECK(!f.desktop.keyboard_focus());
}

TEST_CASE("pressing gives the focus to the nearest widget that takes it")
{
    Fixture f;
    f.desktop.update(at(120, 20, true)); // On `inner`, which does not take the focus
    f.desktop.update(at(120, 20));
    CHECK(rct3::as_pointer(f.desktop.keyboard_focus()) == &f.field);
    CHECK(f.field.has_keyboard_focus());
    CHECK_FALSE(f.top.has_keyboard_focus());

    SUBCASE("keys go to it, then up to its parents until one uses them")
    {
        f.field.uses = { Key::Down };
        f.desktop.update(at(120, 20), keys({ Key::Down, Key::Escape }));
        CHECK(f.field.seen == std::vector { Key::Down, Key::Escape });
        CHECK(f.top.seen == std::vector { Key::Escape });
        CHECK(f.inner.seen.empty());
    }
    SUBCASE("taking it out of the tree takes it away at once")
    {
        auto taken = f.field.parent()->remove(f.field);
        CHECK(!f.desktop.keyboard_focus());
    }
    SUBCASE("pressing elsewhere takes it away")
    {
        f.desktop.update(at(150, 80, true));
        CHECK(!f.desktop.keyboard_focus());
    }
    SUBCASE("hiding or disabling it moves it to the nearest parent still in use")
    {
        f.field.enabled = false;
        f.desktop.update(at(0, 0), keys({ Key::Up }));
        CHECK(rct3::as_pointer(f.desktop.keyboard_focus()) == &f.top);
        CHECK(f.field.seen.empty());
        CHECK(f.top.seen == std::vector { Key::Up });
    }
    SUBCASE("closing its window takes it away")
    {
        f.top.close();
        f.desktop.update(at(0, 0));
        CHECK(!f.desktop.keyboard_focus());
        f.desktop.update(at(0, 0), keys({ Key::Up }));
        CHECK(f.back.seen == std::vector { Key::Up });
    }
}

TEST_CASE("a press moves the focus before keys of the same update")
{
    Fixture f;
    f.field.uses = { Key::Tab };
    f.desktop.update(at(120, 20, true), keys({ Key::Tab }));
    CHECK(f.field.seen == std::vector { Key::Tab });
    CHECK(f.top.seen.empty());
}

TEST_CASE("the focus can be set and cleared from outside")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.back);
    f.desktop.update(at(0, 0), keys({ Key::Home }));
    CHECK(f.back.seen == std::vector { Key::Home });
    f.desktop.set_keyboard_focus(nullptr);
    f.desktop.update(at(0, 0), keys({ Key::End }));
    CHECK(f.top.seen == std::vector { Key::End });
}

TEST_CASE("a key that hides the focused widget sends the next keys to its parent")
{
    struct Hider : Widget
    {
        using Widget::Widget;
        Widget* target = nullptr;
        int keys = 0;
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        bool on_key(const KeyPress&) override
        {
            ++keys;
            if (target != nullptr)
                target->visible = false;
            return true;
        }
    };
    Desktop desktop;
    Hider& window = desktop.root().add<Hider>("window");
    Hider& field = window.add<Hider>("field");
    field.target = &field;
    desktop.layout({ 0, 0, 100, 100 });
    desktop.set_keyboard_focus(&field);

    KeyboardState keys;
    keys.presses.push_back({ Key::Down });
    keys.presses.push_back({ Key::Down });
    desktop.update({ }, keys);
    CHECK(field.keys == 1);
    CHECK(window.keys == 1);
    CHECK(rct3::as_pointer(desktop.keyboard_focus()) == &window);
}

TEST_CASE("a modal widget shuts out the widgets below it")
{
    Fixture f;
    f.desktop.update(at(120, 20, true));
    f.desktop.update(at(120, 20));
    REQUIRE(rct3::as_pointer(f.desktop.keyboard_focus()) == &f.field);

    KeyProbe& modal = f.desktop.root().add<KeyProbe>("modal");
    modal.modal = true;
    modal.placement = top_left(60, 60, 30, 30);
    f.desktop.layout(kView);

    SUBCASE("keys go to it, not to the focus below")
    {
        f.desktop.update(at(0, 0), keys({ Key::Escape }));
        CHECK(modal.seen == std::vector { Key::Escape });
        CHECK(f.field.seen.empty());
        CHECK(!f.desktop.keyboard_focus());
    }
    SUBCASE("the pointer does not reach below it, nor the 3D view")
    {
        CHECK(!f.desktop.hit({ 120, 20 }));
        CHECK(rct3::as_pointer(f.desktop.hit({ 70, 70 })) == &modal);
        CHECK(f.desktop.update(at(10, 10, true)));
        CHECK(f.desktop.root().children().back().get() == &modal); // Nothing below was raised
        f.desktop.update(at(10, 10));
        CHECK(f.desktop.update(at(195, 95)));
    }
    SUBCASE("what is put above it still works")
    {
        KeyProbe& above = f.desktop.root().add<KeyProbe>("above");
        above.placement = top_left(0, 0, 20, 20);
        f.desktop.layout(kView);
        CHECK(rct3::as_pointer(f.desktop.hit({ 10, 10 })) == &above);
        f.desktop.update(at(0, 0), keys({ Key::Escape }));
        CHECK(above.seen == std::vector { Key::Escape });
    }
    SUBCASE("everything below is shaded")
    {
        DrawList list;
        const SkinLibrary skins;
        const FontSet fonts;
        DrawContext context { list, skins, fonts };
        f.desktop.draw(context);
        REQUIRE(list.vertices().size() == 4);
        CHECK(list.vertices()[0].color[3] == f.desktop.modal_shade.a);
    }
    SUBCASE("once it closes, the rest takes input again")
    {
        modal.close();
        f.desktop.update(at(0, 0));
        CHECK(rct3::as_pointer(f.desktop.hit({ 120, 20 })) == &f.field);
    }
}

TEST_CASE("pressing and releasing a widget that clicks asks for its sounds")
{
    class Clicker : public Probe
    {
    public:
        using Probe::Probe;
        [[nodiscard]] bool clicks() const override
        {
            return true;
        }
    };
    Desktop desktop;
    Clicker& c = desktop.root().add<Clicker>("c");
    c.placement = top_left(0, 0, 50, 50);
    Probe& quiet = desktop.root().add<Probe>("quiet");
    quiet.placement = top_left(100, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10, true));
    CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Press });
    CHECK(desktop.take_sounds().empty());
    desktop.update(at(12, 10, false));
    CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Release });

    SUBCASE("released elsewhere: no second click")
    {
        desktop.update(at(10, 10, true));
        desktop.update(at(120, 10, false));
        CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Press });
    }
    SUBCASE("widgets that do not click, and disabled ones, are quiet")
    {
        desktop.update(at(110, 10, true));
        desktop.update(at(110, 10, false));
        c.enabled = false;
        desktop.update(at(10, 10, true));
        desktop.update(at(10, 10, false));
        CHECK(desktop.take_sounds().empty());
    }
    SUBCASE("a press that closes a popup sounds like a release")
    {
        Probe& menu = desktop.root().add<Probe>("menu");
        menu.popup = true;
        menu.placement = top_left(60, 60, 20, 20);
        desktop.layout(kView);
        desktop.update(at(110, 10, true));
        CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Release });
    }
}

TEST_CASE("widgets that click on hover click once as the pointer comes over them")
{
    class Hoverer : public Probe
    {
    public:
        using Probe::Probe;
        [[nodiscard]] bool clicks_on_hover() const override
        {
            return true;
        }
    };
    Desktop desktop;
    Hoverer& a = desktop.root().add<Hoverer>("a");
    a.placement = top_left(0, 0, 50, 50);
    Hoverer& b = desktop.root().add<Hoverer>("b");
    b.placement = top_left(60, 0, 50, 50);
    desktop.layout(kView);

    desktop.update(at(10, 10));
    CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Press });
    desktop.update(at(20, 10)); // Still over it
    CHECK(desktop.take_sounds().empty());
    desktop.update(at(70, 10));
    CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Press });
    desktop.update(at(150, 10));
    desktop.update(at(70, 10)); // Back over it after leaving
    CHECK(desktop.take_sounds() == std::vector { InterfaceSound::Press });

    SUBCASE("not while something is held, nor when disabled")
    {
        desktop.update(at(70, 10, true)); // b held
        desktop.update(at(10, 10, true));
        CHECK(desktop.take_sounds().empty());
        desktop.update(at(150, 10, false));
        b.enabled = false;
        desktop.update(at(70, 10));
        CHECK(desktop.take_sounds().empty());
    }
}
