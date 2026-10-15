// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the park's build menus offer: every ride, shop and piece of scenery the install holds, with what
// the lists that open from the tool row show of it, and the list it belongs in.
//
// Where they come from: each theme folder below "Style" has one overlay named "Style" that holds the
// theme's flat rides, shops, facilities and scenery; each folder below "Tracks/Coasters" and
// "Tracks/TrackedRides" holds one ride with a track. A ride folder has a single overlay; folders full of
// overlays hold track pieces instead and are passed over, unless one of their overlays is named after the
// folder.
//
// Which list an item goes in follows from a number it carries: the attraction kind for rides and shops,
// the item category for scenery. Scenery without an icon is never listed, and items flagged as billboards
// go in the billboard list besides their own.
#pragma once

#include "ovl/overlay.hpp"
#include "save/model/park.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// The lists the build menus open.
    enum class BuildList : std::uint8_t
    {
        // Rides
        Coasters,
        JuniorRides,
        ThrillRides,
        WaterRides,
        GentleRides,
        TransportRides,
        OtherRides,
        PoolRides,
        // Shops
        FoodShops,
        DrinkShops,
        Stalls,
        Facilities,
        // Scenery
        Trees,
        Fences,
        Walls,
        PathExtras,
        Props,
        ParkEntrances,
        RideEvents,
        Fireworks,
        GroundSurfaces,
        Billboards,
    };

    /// The list of a ride by its attraction kind; nothing for kinds the rides menu leaves out.
    [[nodiscard]] std::optional<BuildList> ride_list(std::uint32_t kind) noexcept;
    /// The list of a shop or facility by its attraction kind; nothing for kinds the shops menu leaves out.
    [[nodiscard]] std::optional<BuildList> shop_list(std::uint32_t kind) noexcept;
    /// The list of a piece of scenery by its item category; nothing for categories no list shows.
    [[nodiscard]] std::optional<BuildList> scenery_list(std::uint32_t category) noexcept;

    /// Item flag of scenery that also goes in the billboard list.
    inline constexpr std::uint32_t kBillboardFlag = 1u << 25;

    enum class BuildKind : std::uint8_t
    {
        TrackedRide, ///< Built piece by piece
        FlatRide,    ///< Placed whole: rides without a track and the attractions that stand alone
        Shop,
        Scenery,
    };

    struct BuildItem
    {
        BuildList list = BuildList::Props;
        BuildKind kind = BuildKind::Scenery;
        std::string symbol;           ///< Without its tag
        std::string overlay;          ///< Relative to the install, '/' separated, without ".common.ovl"
        std::string name_text;        ///< String key
        std::string description_text; ///< String key; empty when there is none
        std::string icon;             ///< Skin
        std::int32_t price = 0;       ///< Hundredths of the currency unit; 0 for rides with a track
        /// Scenery shown together under one heading, such as the pieces of a building set; empty otherwise.
        std::string group_icon;
        std::string group_text;
        /// How the item sits on the grid, and its footprint in tiles; zero for rides with a track.
        std::uint16_t placement = 0;
        std::uint32_t width = 0;
        std::uint32_t length = 0;
        std::uint32_t addon = 0; ///< 0 for the base game, else the expansion it came with
    };

    /// Adds the items `overlay` offers. `path` is where it lies, in BuildItem::overlay's form.
    void add_build_items(const ovl::Overlay& overlay, std::string_view path, std::vector<BuildItem>& items);

    /// Reads every item the install under `root` offers, in no particular order. Overlays that cannot be
    /// read are skipped.
    [[nodiscard]] std::vector<BuildItem> read_build_catalogue(const std::filesystem::path& root);

    /// Which items a park lets the player build, by what it records of each: those it has made available
    /// and invented, and does not hide. A park that records nothing lets every item be built.
    class BuildOffer
    {
    public:
        BuildOffer() = default;
        explicit BuildOffer(const save::RequiredContent& content);

        /// Whether the park says anything about which items it offers.
        [[nodiscard]] bool known() const noexcept
        {
            return known_;
        }
        [[nodiscard]] bool offers(const BuildItem& item) const;

    private:
        bool known_ = false;
        std::set<std::string> offered_; // "overlay|symbol", in lower case
    };

} // namespace rct3::park
