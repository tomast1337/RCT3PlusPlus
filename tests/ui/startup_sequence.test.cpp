// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/startup_sequence.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("the step list depends on language and expansions")
{
    const auto english = startup_steps({ });
    REQUIRE(english.size() == 4);
    CHECK(english[0].image == "FrontierLogo");
    CHECK(english[1].image == "RCT3Logo");
    CHECK(english[3].image == "AOPLogo_1");

    const auto chinese = startup_steps({ .simplified_chinese = true, .soaked = false, .wild = false });
    REQUIRE(chinese.size() == 3);
    CHECK(chinese[0].seconds == kStartupWarningSeconds);
    CHECK_FALSE(chinese[0].skippable);
}

TEST_CASE("a step fades in, holds, fades out, then the next one starts")
{
    StartupSequence s({ { "a" }, { "b" } });
    REQUIRE(s.frame());
    CHECK(s.frame()->image == "a");
    CHECK(s.frame()->opacity == 0);
    s.update(0.5f, false);
    CHECK(s.frame()->opacity == doctest::Approx(0.5f));
    s.update(1.0f, false);
    CHECK(s.frame()->opacity == 1);
    s.update(2.5f, false); // 4 s: fade out starts
    CHECK(s.frame()->opacity == doctest::Approx(1));
    s.update(0.25f, false);
    CHECK(s.frame()->opacity == doctest::Approx(0.75f));
    s.update(0.75f, false);
    REQUIRE(s.frame());
    CHECK(s.frame()->image == "b");
    CHECK(s.frame()->opacity == 0);
    for (int i = 0; i < 60; ++i)
        s.update(0.1f, false);
    CHECK(s.finished());
    CHECK_FALSE(s.frame());
}

TEST_CASE("skipping ends the sequence, except during a step that forbids it")
{
    StartupSequence s({ { "warning", kStartupWarningSeconds, false }, { "logo" }, { "other" } });
    s.update(1, true);
    REQUIRE(s.frame());
    CHECK(s.frame()->image == "warning");
    s.update(kStartupWarningSeconds + kStartupFadeSeconds, false);
    REQUIRE(s.frame());
    CHECK(s.frame()->image == "logo");
    s.update(0.1f, true);
    CHECK(s.finished());
}

TEST_CASE("an empty sequence is finished from the start")
{
    StartupSequence s({ });
    CHECK(s.finished());
    s.update(1, true);
    CHECK_FALSE(s.frame());
}
