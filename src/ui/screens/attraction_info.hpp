// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about an attraction: which tabs it has, where each sits on the ring, and the
// lines of each tab's content, written from the attraction's saved state. Every kind of attraction has the
// view, finances, thoughts, open-or-close and customers tabs; rides add vehicles, test results, operating
// mode and maintenance, tracked rides construction and graphs, and stalls their items and extras.
//
// The open-or-close tab has no page: it offers the states the attraction can be put in, and the construction
// tab acts at once instead, asking for the construction window. A page shows a value the save does not hold
// as missing rather than make one up; what the player can change in the save (the price, waits, inspections
// and colours) comes as the page's controls.
#pragma once

#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "ui/money_text.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class AttractionKind : std::uint8_t
    {
        Tracked,
        Flat,
        Facility, ///< Toilets, cash machines and the like
        Stall,
    };

    enum class AttractionTab : std::uint8_t
    {
        View,
        Finances,
        Thoughts,
        State, ///< Open, closed or testing
        Customers,
        Vehicles,
        Tests,
        Mode,
        Maintenance,
        Construction,
        Graphs,
        Items,
        Extras,
    };

    /// The tabs saved messages name by number for their subject to open at, at their number.
    inline constexpr std::array<AttractionTab, 11> kMessageTabs {
        AttractionTab::View, AttractionTab::Vehicles, AttractionTab::Tests, AttractionTab::Finances, AttractionTab::Items, AttractionTab::Extras, AttractionTab::Mode, AttractionTab::Graphs, AttractionTab::Maintenance, AttractionTab::Customers, AttractionTab::Thoughts,
    };

    /// The tab a message's number names, if any.
    [[nodiscard]] constexpr std::optional<AttractionTab> message_tab(std::int32_t panel)
    {
        return panel >= 0 && static_cast<std::size_t>(panel) < kMessageTabs.size() ? std::optional(kMessageTabs[static_cast<std::size_t>(panel)]) : std::nullopt;
    }

    /// The number a message names `tab` by.
    [[nodiscard]] constexpr std::int32_t message_panel(AttractionTab tab)
    {
        return static_cast<std::int32_t>(std::ranges::find(kMessageTabs, tab) - kMessageTabs.begin());
    }

    struct AttractionTabInfo
    {
        AttractionTab tab = AttractionTab::View;
        int slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
        bool action = false;   ///< Does something at once instead of opening a page
    };

    /// The tabs of an attraction of `kind` in the state `state`, in the order of their slots.
    [[nodiscard]] std::vector<AttractionTabInfo> attraction_tabs(AttractionKind kind, save::AttractionState state);

    /// The kind of `ride`.
    [[nodiscard]] AttractionKind attraction_kind(const save::Ride& ride);

    /// The attraction's name as the player sees it: the name they gave it, a named string, or the name of its
    /// kind and its number.
    [[nodiscard]] std::u16string attraction_name(const save::AttractionCommon& attraction, const StringTable& strings);

    /// Open, testing, closed or broken down, in words.
    [[nodiscard]] std::u16string attraction_status(const save::AttractionCommon& attraction, const StringTable& strings);

    /// The word for a rating of `value` (excitement, intensity or nausea): low, medium, high and so on.
    [[nodiscard]] std::u16string rating_word(float value, const StringTable& strings);

    enum class DistanceUnits : std::uint8_t
    {
        Metric,   ///< km/h and metres
        Imperial, ///< mph and feet
        SI,       ///< m/s and metres
    };

    struct InfoUnits
    {
        Currency currency;
        DistanceUnits distance = DistanceUnits::Metric;
    };

    /// The attraction a page is written about: its common part, and the ride or the stall it is.
    /// What the running park knows of a ride now: seats a train (or a go) has, guests in its queue, riders an
    /// hour (0 before it has measured).
    struct RideFlow
    {
        std::uint32_t seats = 0;
        std::size_t queued = 0;
        float per_hour = 0;
        std::size_t riding = 0; ///< On it now
    };

    /// How many trains and cars a tracked ride may be given.
    struct TrainLimits
    {
        int fewest_cars = 1;
        int most_cars = 1;
        int usual_cars = 1; ///< What a new ride's trains have
        int most_trains = 1;
    };

    /// The graph's series, as AttractionFacts::graph_series has them: speed alone at first.
    inline constexpr std::uint8_t kSpeedOnly = 1;

    struct AttractionFacts
    {
        const save::AttractionCommon* common = nullptr;
        const save::Ride* ride = nullptr;
        const save::Stall* stall = nullptr;
        /// The colours it is painted in, one for each pot it offers; none when it cannot be painted.
        std::vector<std::size_t> colours { };
        /// While the park runs; the pages show these over what the save kept.
        std::optional<RideFlow> flow { };
        /// The park's guests, to tell how many the ride suits.
        std::span<const save::Guest> guests { };
        /// Of a ride, whether signs stand over its queues.
        std::optional<bool> queue_signs { };
        /// Of a show ride, what it puts on.
        const save::RideShow* show = nullptr;
        /// Of a tracked ride, whether its track has a station, which its entrance and exit stand beside.
        std::optional<bool> station { };
        /// Of a tracked ride whose trains can still be changed, how far.
        std::optional<TrainLimits> trains { };
        /// String key: why it was last not opened, tested or edited. Shown on its view and state pages.
        std::string_view refusal { };
        /// Of a ride, the tunes it may play, and which it plays: 0 for none, k for music[k - 1].
        std::span<const std::u16string> music { };
        std::size_t tune = 0;
        /// Of a tracked ride whose trains can still be changed, whether its kind runs other kinds of train.
        bool other_cars = false;
        /// Of a tracked ride, whether its track is greased.
        bool low_friction = false;
        /// Which of the graph's series show, a bit each: speed, altitude, vertical and lateral forces.
        std::uint8_t graph_series = kSpeedOnly;
    };

    struct GuestsSuited
    {
        std::size_t suited = 0; ///< Whose preferred intensity takes in the ride's
        std::size_t in_park = 0;
    };

    /// How many of the guests in the park like rides of `intensity` (as saved, about 0 to 10).
    [[nodiscard]] GuestsSuited guests_suited(float intensity, std::span<const save::Guest> guests);

    /// Ids of the controls on an attraction's pages (InfoControl::id).
    enum class AttractionControl : std::uint8_t
    {
        Price,          ///< A ride's admission, in hundredths
        Colours,        ///< Its pots
        MinWaitOn,      ///< Whether the trains wait at least a while
        MinWait,        ///< Seconds
        MaxWaitOn,      ///< Whether they leave after a while at most
        MaxWait,        ///< Seconds
        Inspection,     ///< The interval's place in the list
        WaitFor,        ///< The load the trains wait for, its place in the list
        QueueSigns,     ///< Whether signs stand over a ride's queues
        Circuits,       ///< Times a flat ride runs round each go
        ShowTrick,      ///< The trick at the show's picked place, by place in show_offers; a new place's adds one
        ShowDelete,     ///< Takes the trick at the picked place out of the show
        Entrance,       ///< Places a tracked ride's entrance
        Exit,           ///< And its exit
        Trains,         ///< How many trains a tracked ride runs
        Cars,           ///< Cars in each of its trains
        Name,           ///< Its new name, as typed
        Music,          ///< The tune it plays, as AttractionFacts::tune
        Demolish,       ///< Pulls it down
        SaveDesign,     ///< Keeps a tracked ride's design
        Fence,          ///< Picks its queues' fence
        Locate,         ///< Brings the view round to it
        RideCam,        ///< Rides along in its cars
        Mode,           ///< The operating mode, its place in the list
        BlockSection,   ///< Whether trains keep a block apart
        SyncStations,   ///< Whether trains leave with those at stations beside
        LeaveOnArrival, ///< Whether a train leaves when another comes into the station
        LowFriction,    ///< Whether the track is greased
        LaunchSpeed,    ///< In hundredths of the player's speed unit
        LiftSpeed,      ///< Likewise
        SpeedSeries,    ///< Whether the graph shows the speed
        AltitudeSeries, ///< Its height
        VerticalSeries, ///< Its vertical forces
        LateralSeries,  ///< Its lateral forces
        CarKind,        ///< Moves a tracked ride on to the next kind of train it may run
        Mechanic,       ///< Calls a mechanic to it
        SceneryPick,    ///< Picks scenery to keep with a tracked ride's design
        SceneryClear,   ///< Forgets the scenery picked
        ItemPrice,      ///< The first of a stall's items' prices; the next item's is one more, and so on
    };

    /// The tricks the player may put in `show`, by place in its tricks: not the first, which the performers do
    /// between shows, nor hidden ones, nor ones not yet offered or researched.
    [[nodiscard]] std::vector<std::uint32_t> show_offers(const save::RideShow& show);

    /// The id of `control` on a page, and of item `item`'s price.
    [[nodiscard]] constexpr int control_id(AttractionControl control, std::size_t item = 0)
    {
        return static_cast<int>(control) + static_cast<int>(item);
    }

    /// The text shown for a value the save does not hold.
    inline constexpr std::u16string_view kMissingValue = u"?";

    /// Most waiting time the operating mode page offers, and the step it goes in (seconds).
    inline constexpr int kMostWait = 250;
    inline constexpr int kWaitStep = 5;
    /// Most times round a flat ride can be set to run each go.
    inline constexpr int kMostCircuits = 10;
    /// Most letters in an attraction's name, and how wide its box and its tune's list are.
    inline constexpr int kLongestAttractionName = 80;
    inline constexpr float kNameWidth = 180;
    /// Most admission price, for rides and for other attractions and items, and its step (hundredths).
    inline constexpr int kMostRidePrice = 2000;
    inline constexpr int kMostPrice = 1000;
    inline constexpr int kPriceStep = 10;

    /// Speeds as the player reads them: so many of their unit to a metre a second, and the unit after a number.
    [[nodiscard]] float speed_scale(DistanceUnits units);
    [[nodiscard]] std::u16string_view speed_unit(DistanceUnits units);

    /// The page of `tab`, at `section` when the page has several.
    [[nodiscard]] InfoPage attraction_page(AttractionTab tab, const AttractionFacts& facts, const StringTable& strings, const InfoUnits& units, std::size_t section = 0);

    /// A state the open-or-close tab offers.
    struct StateChoice
    {
        save::AttractionState state = save::AttractionState::Open;
        std::string_view icon;          ///< Skin
        std::string_view disabled_icon; ///< Skin when it is the state already
        std::string_view help;          ///< String key
    };

    /// The states an attraction of `kind` can be put in: open and closed, and testing for rides.
    [[nodiscard]] std::vector<StateChoice> state_choices(AttractionKind kind);

} // namespace rct3::ui
