// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/slider.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // A 120 wide slider with a 20 wide thumb: the thumb's centre travels from 10 to 110.
    SliderStyle style()
    {
        SliderStyle s;
        s.thumb_width = 20;
        s.thumb_height = 10;
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        Slider& slider = desktop.root().add<Slider>("slider", style());

        Fixture()
        {
            slider.placement = top_left(0, 0, 120, 20);
            slider.minimum = 0;
            slider.maximum = 100;
            desktop.layout({ 0, 0, 300, 300 });
        }

        void key(Key k)
        {
            KeyboardState keys;
            keys.presses.push_back({ k });
            desktop.update({ { 200, 200 }, false, true }, keys);
        }
    };
} // namespace

TEST_CASE("pressing the rail puts the thumb there and drags it")
{
    Fixture f;
    CHECK(f.slider.thumb() == Rect { 0, 5, 20, 15 });
    f.desktop.update({ { 60, 10 }, true, true });
    CHECK(f.slider.value() == doctest::Approx(50));
    CHECK(f.slider.take_change());
    CHECK(f.desktop.keyboard_focus() == &f.slider);
    f.desktop.update({ { 85, 10 }, true, true });
    CHECK(f.slider.value() == doctest::Approx(75));
    f.desktop.update({ { 250, 10 }, true, true });
    CHECK(f.slider.value() == doctest::Approx(100));
    f.desktop.update({ { 250, 10 }, false, true });
    CHECK(f.slider.thumb() == Rect { 100, 5, 120, 15 });
}

TEST_CASE("grabbing the thumb off its centre does not make it jump")
{
    Fixture f;
    f.slider.set_value(50); // Thumb from 50 to 70
    CHECK_FALSE(f.slider.take_change());
    f.desktop.update({ { 66, 10 }, true, true });
    CHECK(f.slider.value() == doctest::Approx(50));
    CHECK_FALSE(f.slider.take_change());
    f.desktop.update({ { 76, 10 }, true, true });
    CHECK(f.slider.value() == doctest::Approx(60));
}

TEST_CASE("a step snaps the value")
{
    Fixture f;
    f.slider.step = 25;
    f.desktop.update({ { 43, 10 }, true, true }); // 33 of the way
    CHECK(f.slider.value() == 25);
    f.slider.set_value(90);
    CHECK(f.slider.value() == 100);
}

TEST_CASE("the keys and the wheel move the slider")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.slider);
    f.key(Key::Right);
    CHECK(f.slider.value() == doctest::Approx(5)); // A twentieth without a step
    f.key(Key::PageUp);
    CHECK(f.slider.value() == doctest::Approx(15));
    f.key(Key::Down);
    CHECK(f.slider.value() == doctest::Approx(10));
    f.key(Key::End);
    CHECK(f.slider.value() == 100);
    f.key(Key::Home);
    CHECK(f.slider.value() == 0);
    f.key(Key::Left);
    CHECK(f.slider.value() == 0);
    f.desktop.update({ { 60, 10 }, false, true, { 0, 2 } });
    CHECK(f.slider.value() == doctest::Approx(10));

    f.slider.step = 2;
    f.key(Key::Up);
    CHECK(f.slider.value() == 12);
    CHECK_FALSE(f.slider.on_key({ Key::Escape }));
}

TEST_CASE("a bar takes the value under the pointer from its left edge")
{
    Desktop desktop;
    SliderStyle s;
    s.bar = true;
    s.thumb_width = 0;
    s.thumb_height = 0;
    Slider& bar = desktop.root().add<Slider>("bar", s);
    bar.placement = top_left(0, 0, 200, 10);
    bar.maximum = 1;
    desktop.layout({ 0, 0, 300, 300 });
    desktop.update({ { 50, 5 }, true, true });
    CHECK(bar.value() == doctest::Approx(0.25));
    desktop.update({ { 150, 5 }, true, true });
    CHECK(bar.value() == doctest::Approx(0.75));
    desktop.update({ { 260, 5 }, true, true });
    CHECK(bar.value() == doctest::Approx(1));
}
