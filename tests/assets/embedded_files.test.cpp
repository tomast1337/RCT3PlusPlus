// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets/embedded_files.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::assets;

TEST_CASE("built-in files are found by their path below assets")
{
    const auto files = embedded_files();
    REQUIRE_FALSE(files.empty());
    CHECK(std::ranges::is_sorted(files, { }, &EmbeddedFile::path));
    for (const EmbeddedFile& f : files)
    {
        const auto found = embedded_file(f.path);
        REQUIRE(found);
        CHECK(found->data() == f.bytes.data());
        CHECK(found->size() == f.bytes.size());
    }
    const auto english = embedded_file("lang/English.json");
    REQUIRE(english);
    CHECK(as_text(*english).starts_with("{"));
    CHECK_FALSE(embedded_file("lang/missing.json"));
    CHECK_FALSE(embedded_file(""));
}
