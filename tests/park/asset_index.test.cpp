// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/asset_index.hpp"

#include <doctest/doctest.h>

using namespace rct3::park;

TEST_CASE("a symbol in one file is found from anywhere")
{
    AssetIndex index;
    index.add("Bench:sid", "Things/Seats/Seats");
    CHECK(index.find("Bench:sid", "Elsewhere/Other") == "Things/Seats/Seats");
    CHECK_FALSE(index.find("Bench:svd", "Things/Seats/Seats"));
    CHECK_FALSE(index.find("Table:sid", "Things/Seats/Seats"));
    CHECK(index.symbol_count() == 1);
}

TEST_CASE("of several files with a symbol, the nearest wins")
{
    AssetIndex index;
    index.add("Rail:shs", "Rides/Track2/Track2");
    index.add("Rail:shs", "Rides/Track1/Track1");
    index.add("Rail:shs", "Rides/Track3/Parts/Rails");
    CHECK(index.find("Rail:shs", "Rides/Track3/Track3") == "Rides/Track3/Parts/Rails");
    CHECK(index.find("Rail:shs", "Rides/Track2/Track2") == "Rides/Track2/Track2");
    // Equally near: always the same one.
    CHECK(index.find("Rail:shs", "Other/Thing") == "Rides/Track1/Track1");
    // Hints come with either slash and in any case.
    CHECK(index.find("Rail:shs", "rides\\track2\\Track2") == "Rides/Track2/Track2");
}

TEST_CASE("an overlay hint is matched to the files")
{
    AssetIndex index;
    index.add("Bench:sid", "Things/Seats/Seats");
    CHECK(index.overlay("things\\SEATS\\seats") == "Things/Seats/Seats");
    CHECK_FALSE(index.overlay("Things/Seats/Missing"));
    CHECK(index.file("Things/Seats/Seats") == std::filesystem::path("Things/Seats/Seats.common.ovl"));
}
