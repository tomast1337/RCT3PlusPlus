// SPDX-License-Identifier: GPL-3.0-or-later
//
// The controls that build a tracked ride piece by piece, as the game lays them out in the strip of the round
// panel in the bottom-left corner (info_panel.hpp), which they sit in: 652 by 100 units, the first 32 under
// the disc. Their left part has two rows of seven buttons, the curve on top (tight, medium and loose left,
// straight, then loose, medium and tight right) and below it the slope the piece ends at (vertical, steep and
// gentle down, level, then gentle, steep and vertical up). Right of them are the toggles (lift chain, tube,
// water, wide channel, booster: those the track has pieces with), two to a column, then the bank dial: eight small buttons
// round a disc, upside down at the top, level at the bottom. A toggle the picked piece cannot have is hidden
// in its place, not greyed. Under the rows go the picked piece's name and its price, and the buttons that finish the track
// for the player and that pull down the last piece sit to the right of them. Between the dial and the list,
// the pieces with a picture of their own (stations, photo sections, brakes) stand as picture buttons, two by
// two, moving right with the dial when it moves; a brake picked shows its speed to set beside its name. The right part lists the other pieces with a
// use of their own (loops, helices and the like) in a list of striped rows, each its name after two small
// marks for the way it goes and its kind. Pieces that cannot go on where the track ends stay in their place,
// greyed. A line divides the rows of buttons from the name below them.
//
// Each picture button shows one of three pictures: picked, offered, or not offered (see IconState). The
// curve, slope and bank buttons have no help text in the game; the toggles and the demolish button do.
//
// The panel does not know about track: its owner shows it what to offer (show()) and hears what the player
// did (take_event()). Picking the picked curve, slope or bank again is the game's way to build the piece; the
// owner hears that as a Build event. The round panel closes them.
#pragma once

#include "ui/font_set.hpp"
#include "ui/widget.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/list_box.hpp"
#include "ui/widgets/spinner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    namespace construction_panel
    {
        inline constexpr float kWidth = 652;
        inline constexpr float kHeight = 100;
        inline constexpr float kStripLeft = 32; ///< Where the strip starts; the ring panel covers the rest
        inline constexpr float kStripRight = 648;
        inline constexpr float kRowLeft = 40; ///< Of the first curve and slope button
        inline constexpr float kStep = 30;    ///< Between buttons of a row
        inline constexpr float kCurveTop = 7;
        inline constexpr Point kCurveSize { 28, 25 };
        inline constexpr float kSlopeTop = 33;
        inline constexpr Point kSlopeSize { 29, 30 };
        inline constexpr float kToggleLeft = 250;
        inline constexpr Point kToggleSize { 28, 25 };
        inline constexpr float kDialTop = 3;
        inline constexpr float kDialSize = 60;
        inline constexpr float kBankSize = 17;
        inline constexpr float kLabelLeft = 40;
        inline constexpr float kNameTop = 66;
        inline constexpr float kCostTop = 81;
        inline constexpr float kLabelWidth = 220;
        inline constexpr float kLabelHeight = 15;
        inline constexpr Point kDemolishAt { 322, 72 };
        inline constexpr Point kDemolishSize { 40, 26 };
        inline constexpr Point kCompleteAt { 278, 70 };
        inline constexpr Point kCompleteSize { 40, 28 };
        inline constexpr Point kHeightsAt { 366, 70 };
        inline constexpr Point kHeightsSize { 40, 28 };
        inline constexpr Point kNewSegmentAt { 410, 70 }; ///< The list starts right of it while it shows
        inline constexpr Point kNewSegmentSize { 40, 28 };
        inline constexpr float kListLeft = 410; ///< Of the list of the other named pieces
        inline constexpr float kListTop = 2;
        inline constexpr float kListRight = 640;
        inline constexpr float kListHeight = 94;
        inline constexpr float kListRowHeight = 16;
        inline constexpr float kDividerTop = 63;      ///< Of the line over the piece's name, from the rows' left to the list
        inline constexpr Point kSpecialAt { 344, 5 }; ///< The first picture piece; the rest go down, then right
        inline constexpr float kSpecialSize = 28;
        inline constexpr float kSpecialStep = 30;
        inline constexpr std::size_t kSpecials = 4;
        inline constexpr Point kSpeedAt { 136, 80 }; ///< Of the speed's picture, the spinner right of it
        inline constexpr float kSpeedIconSize = 18;
        inline constexpr Point kSpeedSize { 118, 18 }; ///< Of the spinner

        inline constexpr std::size_t kCurves = 7;
        inline constexpr std::size_t kSlopes = 7;
        inline constexpr std::size_t kBanks = 8;   ///< From hardest left to upside down
        inline constexpr std::size_t kToggles = 5; ///< Lift chain, tube, water, wide, booster
    } // namespace construction_panel

    /// What a picture button shows.
    enum class IconState : std::uint8_t
    {
        On,  ///< Picked
        Off, ///< Offered
        Dis, ///< Not offered
    };

    /// The skin of `base` in `state`: the base name with the state's name after it.
    [[nodiscard]] std::string icon_skin(std::string_view base, IconState state);

    /// Where each bank button sits in the dial, top-left corners from the dial's top-left, in the order of the
    /// panel's banks: hardest left first, level in the middle, upside down last.
    [[nodiscard]] std::array<Point, construction_panel::kBanks> bank_spots();

    /// Where the bank dial goes when `toggles` toggles stand before it.
    [[nodiscard]] float dial_left(std::size_t toggles);
    /// Where the picture pieces start when `toggles` toggles stand before the dial; the list starts after them.
    [[nodiscard]] float specials_left(std::size_t toggles);

    struct ConstructionPanelStyle
    {
        std::array<std::string, construction_panel::kCurves> curves; ///< Base names, leftmost first
        std::array<std::string, construction_panel::kSlopes> slopes; ///< Steepest down first
        std::array<std::string, construction_panel::kBanks> banks;   ///< In the panel's order
        std::string dial;
        std::array<std::string, construction_panel::kToggles> toggles; ///< Base names
        std::string demolish;
        std::string complete;    ///< Finishes the track
        std::string new_segment; ///< Begins another stretch of track
        std::string heights;     ///< Shows the track's heights: off, then on
        std::string heights_on;
        ListBoxStyle list; ///< Of the named pieces without a picture
        ScrollBarStyle list_bar;
        Color divider = hex("#c8c8c8");
        std::string speed_icon; ///< Beside the speed
        SpinnerStyle speed;
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
    };

    /// A piece with a use of its own, as the panel offers it.
    struct NamedPiece
    {
        std::u16string name { };
        std::string icon { }; ///< Skin of its picture: one with a picture stands as a picture button, not in the list
        /// Skins of the small marks before its name in the list, its way's then its kind's; empty for none.
        std::array<std::string, 2> marks { };
        bool offered = true; ///< It can go on where the track ends; greyed otherwise
    };

    /// What the panel shows: one state per button, the piece's name and price, the named pieces.
    struct ConstructionView
    {
        std::array<IconState, construction_panel::kCurves> curves { };
        std::array<IconState, construction_panel::kSlopes> slopes { };
        std::array<IconState, construction_panel::kBanks> banks { };
        /// One per toggle; none for a toggle the track has no pieces with at all.
        std::array<std::optional<IconState>, construction_panel::kToggles> toggles { };
        bool can_demolish = false;
        bool can_complete = false;       ///< The track has an open end to finish from
        std::optional<bool> new_segment; ///< None when the track is one stretch; else whether another can begin now
        bool heights = false;            ///< The track's heights are shown
        bool heights_locked = false;     ///< Shown for every ride, from the view menu, so the button cannot hide them
        std::u16string piece;            ///< Name of the piece picked, empty when nothing fits
        std::u16string cost;
        std::vector<NamedPiece> named;
        std::optional<std::size_t> named_on; ///< The one picked
        /// The picked piece's speed, in hundredths of `speed_unit`, when it has one to set.
        std::optional<int> speed;
        int speed_most = 0; ///< In hundredths, as `speed`
        std::u16string speed_unit;
    };

    enum class ConstructionAction : std::uint8_t
    {
        Curve, ///< `index` of the curves
        Slope,
        Bank,
        Toggle, ///< `index` of the toggles
        Named,  ///< `index` of the view's named pieces; only offered ones
        Build,  ///< The picked button clicked again
        Demolish,
        Complete,   ///< Finish the track
        NewSegment, ///< Begin another stretch of track
        Heights,    ///< Show or hide the track's heights
        Speed,      ///< `index` the new speed, as the view's
    };

    struct ConstructionEvent
    {
        ConstructionAction action = ConstructionAction::Build;
        std::size_t index = 0;

        friend bool operator==(const ConstructionEvent&, const ConstructionEvent&) = default;
    };

    class ConstructionPanel : public Widget
    {
    public:
        /// Help texts come from `toggle_helps` and `demolish_help`, as the strings name them.
        ConstructionPanel(std::string name, const ConstructionPanelStyle& style, std::array<std::u16string, construction_panel::kToggles> toggle_helps, std::u16string demolish_help);

        /// Shows `view` from now on.
        void show(ConstructionView view);
        [[nodiscard]] const ConstructionView& view() const
        {
            return view_;
        }

        /// What the player did since the last call, oldest first, once.
        [[nodiscard]] std::optional<ConstructionEvent> take_event();

        [[nodiscard]] Button& curve_button(std::size_t i) const
        {
            return *curves_.at(i);
        }
        [[nodiscard]] Button& slope_button(std::size_t i) const
        {
            return *slopes_.at(i);
        }
        [[nodiscard]] Button& bank_button(std::size_t i) const
        {
            return *banks_.at(i);
        }
        [[nodiscard]] Button& toggle_button(std::size_t i) const
        {
            return *toggles_.at(i);
        }
        [[nodiscard]] Button& demolish_button() const
        {
            return *demolish_;
        }
        [[nodiscard]] Button& complete_button() const
        {
            return *complete_;
        }
        [[nodiscard]] Button& new_segment_button() const
        {
            return *new_segment_;
        }
        [[nodiscard]] Button& heights_button() const
        {
            return *heights_;
        }
        std::u16string heights_help;        ///< The heights button's help
        std::u16string heights_locked_help; ///< Its help while the view menu shows every ride's heights
        /// The named pieces without a picture.
        [[nodiscard]] ListBox& list() const
        {
            return *list_;
        }
        [[nodiscard]] Label& piece_label() const
        {
            return *piece_;
        }
        [[nodiscard]] Label& cost_label() const
        {
            return *cost_;
        }
        [[nodiscard]] Button& special_button(std::size_t i) const
        {
            return *specials_.at(i);
        }
        [[nodiscard]] Spinner& speed_spinner() const
        {
            return *speed_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void lay_out();
        void restyle();

        ConstructionPanelStyle style_;
        ConstructionView view_;
        std::array<Button*, construction_panel::kCurves> curves_ { };
        std::array<Button*, construction_panel::kSlopes> slopes_ { };
        std::array<Button*, construction_panel::kBanks> banks_ { };
        ListBox* list_ = nullptr;
        std::array<Button*, construction_panel::kToggles> toggles_ { };
        float list_left_ = construction_panel::kListLeft;
        Button* demolish_ = nullptr;
        Button* complete_ = nullptr;
        Button* new_segment_ = nullptr;
        Button* heights_ = nullptr;
        std::array<Button*, construction_panel::kSpecials> specials_ { };
        Button* speed_icon_ = nullptr;
        Spinner* speed_ = nullptr;
        std::vector<std::size_t> listed_;   ///< The named pieces in the list, by index in the view
        std::vector<std::size_t> pictured_; ///< Those with picture buttons, by index in the view
        Label* piece_ = nullptr;
        Label* cost_ = nullptr;
        std::vector<ConstructionEvent> events_;
    };

} // namespace rct3::ui
