// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/option_effects.hpp"

#include <doctest/doctest.h>

using namespace rct3::config;

TEST_CASE("the options nothing reads yet are the ones not acted on")
{
    const auto inert = inert_options();
    CHECK(inert.count() + acted_on_options().size() == kGameOptionCount);
    CHECK_FALSE(inert.test(static_cast<std::size_t>(GameOption::Fullscreen)));
    CHECK_FALSE(inert.test(static_cast<std::size_t>(GameOption::MusicVolume)));
    CHECK(inert.test(static_cast<std::size_t>(GameOption::Brightness)));
    CHECK(inert.test(static_cast<std::size_t>(GameOption::Bloom)));
}
