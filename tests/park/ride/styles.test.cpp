// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride/styles.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using park::BuildItem;
using park::BuildKind;

namespace
{
    BuildItem ride(std::string overlay, std::string title, std::string fence, std::string fence_overlay, std::string door_overlay)
    {
        return BuildItem {
            .kind = BuildKind::FlatRide,
            .overlay = std::move(overlay),
            .entrance = door_overlay.empty() ? "" : "Gate",
            .exit = door_overlay.empty() ? "" : "Arch",
            .door_overlay = std::move(door_overlay),
            .fence = std::move(fence),
            .fence_overlay = std::move(fence_overlay),
            .style_title = std::move(title),
        };
    }
} // namespace

TEST_CASE("each theme's fence and doors are offered once, named after the theme whose own they are")
{
    const std::vector<BuildItem> items {
        // A garden ride borrowing the plain doors, listed before the plain ride that owns them.
        ride("Themes/Garden", "Garden", "Hedge", "Themes/Garden", "Themes/Plain"), ride("Themes/Plain", "Plain", "Rail", "Themes/Plain", "Themes/Plain"), ride("Themes/Garden", "Garden", "Hedge", "Themes/Garden", "Themes/Plain"), ride("Themes/Castle", "Castle", "Rail", "Themes/Plain", "Themes/Castle"), BuildItem { .kind = BuildKind::Scenery, .entrance = "Gate", .exit = "Arch", .door_overlay = "Themes/Shed", .fence = "Planks", .fence_overlay = "Themes/Shed" },
    };
    const park::ride::RideStyles styles = park::ride::ride_styles(items);
    REQUIRE(styles.fences.size() == 2);
    CHECK(styles.fences[0].symbol == "Hedge");
    CHECK(styles.fences[0].title == "Garden");
    CHECK(styles.fences[1].symbol == "Rail");
    CHECK(styles.fences[1].title == "Plain");
    REQUIRE(styles.doors.size() == 2);
    CHECK(styles.doors[0].title == "Plain");
    CHECK(styles.doors[0].symbol == "Gate");
    CHECK(styles.doors[0].exit_symbol == "Arch");
    CHECK(styles.doors[1].overlay == "Themes/Castle");
}

TEST_CASE("a ride's fence or door is found among the styles by its item, in any case and with either slash")
{
    const std::vector<park::ride::ExtraStyle> doors { { .symbol = "Gate", .exit_symbol = "Arch", .overlay = "Themes/Plain" }, { .symbol = "Portcullis", .exit_symbol = "Drawbridge", .overlay = "Themes/Castle" } };
    CHECK(park::ride::style_of(doors, "portcullis", "themes\\castle") == 1);
    CHECK(park::ride::style_of(doors, "Drawbridge", "Themes/Castle", park::ride::DoorEnd::Exit) == 1);
    CHECK_FALSE(park::ride::style_of(doors, "Drawbridge", "Themes/Castle"));
    CHECK_FALSE(park::ride::style_of(doors, "Gate", "Themes/Castle"));
}
