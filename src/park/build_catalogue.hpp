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
//
// The pool list comes from the pool type's overlay, "Pool/Default/Default_Stub": tools for laying its
// squares, water at each depth and paved tiles, and for taking them up again.
//
// The enclosure and animal lists come from their own folders: "Enclosures" holds the kinds of enclosure
// fence and the animal shelters, "WildAnimals" the species and the toys. Each species gives one item per
// animal a park can buy: adult male and female, young male and female.
#pragma once

#include "ovl/overlay.hpp"
#include "save/model/park.hpp"

#include <array>
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
        Enclosures, ///< Fences, shelters and toys for animal enclosures
        Animals,
        Pools, ///< Swimming pools: the tools laying their squares
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
    /// The attraction kind of the special attraction (a toilet, a cash machine and the like) named `name` in the
    /// overlay `file`; nothing when the file has no such attraction.
    [[nodiscard]] std::optional<std::uint32_t> special_attraction_kind(const std::filesystem::path& file, std::string_view name);
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
        EnclosureFence, ///< Drawn round an area to make an enclosure
        Shelter,        ///< A scenery item placed inside an enclosure for its animals
        AnimalToy,      ///< Put inside an enclosure for the animals to play with
        Animal,         ///< One animal of a species, bought into an enclosure
        PoolSquare,     ///< A tool laying squares of a pool, or taking them up (park/pools.hpp)
        PoolExtra,      ///< A scenery item put in a pool, against a side of one of its squares
        PoolRoom,       ///< A changing room, built beside a paved square of a pool
    };

    /// The symbols of the pool list's tools: water at each depth, paved tiles, and taking squares up. They are
    /// the keys of the texts that name them.
    inline constexpr std::array<std::string_view, 3> kPoolWater { "Pool_DepthShallow", "Pool_DepthNormal", "Pool_DepthDeep" };
    inline constexpr std::string_view kPoolTiles = "Pool_PlaceTiles";
    inline constexpr std::string_view kPoolDelete = "Pool_DeleteArea";
    /// The tool laying spa pools, listed with the extras.
    inline constexpr std::string_view kPoolJacuzzi = "SceneryItem_Jacuzzi";
    /// The tool laying high diving boards, listed with the extras.
    inline constexpr std::string_view kPoolHighDive = "SegmentTypeHighDive";
    /// The overlay of the pool type, whose pieces pools are built of.
    inline constexpr std::string_view kPoolOverlay = "Pool/Default/Default_Stub";

    /// The symbol of the enclosure list's delete tool, listed first among the fences: it takes tiles out of
    /// enclosures as a fence puts them in.
    inline constexpr std::string_view kEnclosureDelete = "EnclosureDelete";

    /// The parts of the enclosure list, in the order it shows them.
    enum class EnclosurePart : std::uint8_t
    {
        None, ///< Items of other lists
        Fences,
        Shelters,
        Toys,
    };

    struct BuildItem
    {
        BuildList list = BuildList::Props;
        BuildKind kind = BuildKind::Scenery;
        std::string symbol;  ///< Without its tag
        std::string overlay; ///< Relative to the install, '/' separated, without ".common.ovl"
        std::string scenery; ///< Rides and shops: the scenery item they are placed as. Shelters: their keeper's hut
        /// Rides placed whole or built along a track: the scenery items of their entrance and exit, and the
        /// overlay they are in: their own theme's, else the plain theme's; empty when neither has one.
        std::string entrance, exit, door_overlay;
        /// Rides: the fence new ones take, as their theme's overlay names it, and the overlay that holds it;
        /// the plain theme's for rides of no theme, empty when neither names one.
        std::string fence, fence_overlay;
        std::string name_text;        ///< String key
        std::string description_text; ///< String key; empty when there is none
        std::string icon;             ///< Skin
        std::int32_t price = 0;       ///< Hundredths of the currency unit; 0 for rides with a track
        std::int32_t refund = 0;      ///< Enclosure fences: what taking a length down pays back, in hundredths; below 0
        /// Scenery shown together under one heading, such as the pieces of a building set; empty otherwise.
        std::string group_icon;
        std::string group_text;
        /// How the item sits on the grid, and its footprint in tiles; zero for rides with a track.
        std::uint16_t placement = 0;
        std::uint32_t width = 0;
        std::uint32_t length = 0;
        bool water_only = false; ///< Stands only on water
        std::uint32_t addon = 0; ///< 0 for the base game, else the expansion it came with
        /// The kind of attraction (ride_list's `kind`) or the scenery category (scenery_list's) that put the
        /// item in its list; 0 for others.
        std::uint32_t type = 0;
        /// The theme the item belongs to, as the theme's overlay numbers and names it: its name for skins
        /// ("Space") and its title shown to the player ("Sci-fi"). -1 and empty for items of no theme. Overlays
        /// that add to a theme give only its number.
        std::int32_t style_index = -1;
        std::string style;
        std::string style_title;
        EnclosurePart part = EnclosurePart::None;
        /// Animals: the symbol of the shelter their species needs, in any case; empty for other items.
        std::string needs;
        /// Enclosure fences: how strong they are. Animals: the least strength of fence that holds them.
        float strength = 0;
        float rarity = 0; ///< Animals: from 0, common, to 1, endangered
        /// Shops: whether placed as a stall, as against a special attraction such as a toilet.
        bool stall = false;
        /// Stalls: what a new one sells, each a carried item's name and the price it starts at, in hundredths;
        /// empty when the stall's overlay does not give them.
        struct Goods
        {
            std::string item;
            std::int32_t price = 0;
        };
        std::vector<Goods> goods;
        /// Pool tools: the pool type's price list (ovl::PoolType::prices), what their squares cost.
        std::vector<std::int32_t> pool_prices { };
        std::string pool_extra { }; ///< Pool extras: the words of their setting, read by park::pool_extra_fit
    };

    /// Names the theme of each item that has only its number, after another item of that number.
    void name_styles(std::vector<BuildItem>& items);

    /// Adds the items `overlay` offers. `path` is where it lies, in BuildItem::overlay's form.
    void add_build_items(const ovl::Overlay& overlay, std::string_view path, std::vector<BuildItem>& items);

    /// Reads every item the install under `root` offers, in no particular order. Overlays that cannot be
    /// read are skipped.
    [[nodiscard]] std::vector<BuildItem> read_build_catalogue(const std::filesystem::path& root);

    /// Which items a park lets the player build, by what it records of each: those it has made available
    /// and invented, and does not hide, and the animal species it has unlocked. A park that records nothing
    /// lets every item be built, and one that records no species every animal. Enclosure fences, shelters
    /// and toys are always offered: parks list them without saying whether they may be built. So are the
    /// pool tools, which no park lists.
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
        bool species_known_ = false;
        std::set<std::string> offered_; // "overlay|symbol", in lower case
        std::set<std::string> listed_;  // The same, offered or not
    };

} // namespace rct3::park
