// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/version.hpp"

#include <doctest/doctest.h>

TEST_CASE("the build's version is one line of text, for titles, logs and reports")
{
    const std::string_view version = rct3::core::version();
    CHECK(!version.empty());
    CHECK(version.find_first_of("\n\r\"") == std::string_view::npos);
}
