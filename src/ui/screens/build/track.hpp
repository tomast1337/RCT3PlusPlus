// SPDX-License-Identifier: GPL-3.0-or-later
//
// The controls that build a tracked ride piece by piece, as the game lays them out in the strip of the round
// panel in the bottom-left corner (info/panel.hpp), which they sit in: 652 by 100 units, the first 32 under
// the disc. Their left part has two rows of seven buttons, the curve on top (tight, medium and loose left,
// straight, then loose, medium and tight right) and below it the slope the piece ends at (vertical, steep and
// gentle down, level, then gentle, steep and vertical up). Right of them are the toggles (lift chain, tube,
// water, wide channel, booster: those the track has pieces with; then which way the cars face, on rides where
// the player sets it), two to a column, then the bank dial: eight small buttons
// round a disc, upside down at the top, level at the bottom. A toggle the picked piece cannot have is hidden
// in its place, not greyed. Under the rows go the picked piece's name and its price, and the buttons that finish the track
// for the player and that pull down the last piece sit to the right of them. Between the dial and the list,
// the pieces with a picture of their own (stations, photo sections, brakes) stand as picture buttons, two by
// two, moving right with the dial when it moves. Boxes for the picked piece's settings (how far the cars turn,
// how long a train is held, a brake's speed) stand right of its price, those it has. The right part lists the other pieces with a
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

#include "core/types.hpp"
#include "ui/font/set.hpp"
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
#include <utility>
#include <vector>

namespace rct3::ui::screens::build
{

    namespace construction_panel
    {
        inline constexpr f32 kWidth = 652;
        inline constexpr f32 kHeight = 100;
        inline constexpr f32 kStripLeft = 32; ///< Where the strip starts; the ring panel covers the rest
        inline constexpr f32 kStripRight = 648;
        inline constexpr f32 kRowLeft = 40; ///< Of the first curve and slope button
        inline constexpr f32 kStep = 30;    ///< Between buttons of a row
        inline constexpr f32 kCurveTop = 7;
        inline constexpr Point kCurveSize { 28, 25 };
        inline constexpr f32 kSlopeTop = 33;
        inline constexpr Point kSlopeSize { 29, 30 };
        inline constexpr f32 kToggleLeft = 250;
        inline constexpr Point kToggleSize { 28, 25 };
        inline constexpr f32 kDialTop = 3;
        inline constexpr f32 kDialSize = 60;
        inline constexpr f32 kBankSize = 17;
        inline constexpr f32 kLabelLeft = 40;
        inline constexpr f32 kNameTop = 66;
        inline constexpr f32 kCostTop = 81;
        inline constexpr f32 kLabelWidth = 220;
        inline constexpr f32 kLabelHeight = 15;
        inline constexpr Point kDemolishAt { 322, 72 };
        inline constexpr Point kDemolishSize { 40, 26 };
        inline constexpr Point kCompleteAt { 278, 70 };
        inline constexpr Point kCompleteSize { 40, 28 };
        inline constexpr Point kHeightsAt { 366, 70 };
        inline constexpr Point kHeightsSize { 40, 28 };
        inline constexpr Point kNewSegmentAt { 410, 70 }; ///< The list starts right of it while it shows
        inline constexpr Point kNewSegmentSize { 40, 28 };
        inline constexpr Point kSplitSize { 40, 28 }; ///< At the new stretch button's place, or right of it while it shows
        inline constexpr usize kSplitPictures = 3;
        inline constexpr f32 kListLeft = 410; ///< Of the list of the other named pieces
        inline constexpr f32 kListTop = 2;
        inline constexpr f32 kListRight = 640;
        inline constexpr f32 kListHeight = 94;
        inline constexpr f32 kListRowHeight = 16;
        inline constexpr f32 kDividerTop = 63;        ///< Of the line over the piece's name, from the rows' left to the list
        inline constexpr Point kSpecialAt { 344, 5 }; ///< The first picture piece; the rest go down, then right
        inline constexpr f32 kSpecialSize = 28;
        inline constexpr f32 kSpecialStep = 30;
        inline constexpr usize kSpecials = 4;
        inline constexpr f32 kParamsRight = 272; ///< Where the last setting box ends, on the price's row
        inline constexpr f32 kParamsTop = 80;
        inline constexpr f32 kParamIconSize = 18; ///< Of the picture before each box
        inline constexpr f32 kParamHeight = 18;
        inline constexpr f32 kParamGap = 6;
        inline constexpr usize kParams = 3;                                     ///< Car turn, holding time, brake speed
        inline constexpr std::array<f32, kParams> kParamWidths { 60, 50, 118 }; ///< Of the boxes, past their pictures

        inline constexpr usize kCurves = 7;
        inline constexpr usize kSlopes = 7;
        inline constexpr usize kBanks = 8;   ///< From hardest left to upside down
        inline constexpr usize kToggles = 5; ///< Lift chain, tube, water, wide, booster
        inline constexpr usize kFacings = 3; ///< Forwards, backwards, spinning freely
    } // namespace construction_panel

    /// The settings a piece may have, as the boxes stand left to right.
    enum class PieceParam : u8
    {
        CarTurn,     ///< Degrees the cars turn to
        HoldingTime, ///< Seconds a train is held
        Speed,       ///< A brake's speed
    };

    /// One setting's box: its value and range, in units of `unit` shown with `decimals` places.
    struct ParamBox
    {
        i32 value = 0;
        i32 least = 0;
        i32 most = 0;
        i32 decimals = 0;
        std::u16string unit;
        i32 step = 1;
    };

    /// What a picture button shows.
    enum class IconState : u8
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
    [[nodiscard]] f32 dial_left(usize toggles);
    /// Where the picture pieces start when `toggles` toggles stand before the dial; the list starts after them.
    [[nodiscard]] f32 specials_left(usize toggles);

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
        /// Which open end of side by side tracks is built on, and which kind of pieces: joined from either
        /// end, the left track alone, the right track alone.
        std::array<std::string, construction_panel::kSplitPictures> split;
        std::string heights; ///< Shows the track's heights: off, then on
        std::string heights_on;
        widgets::ListBoxStyle list; ///< Of the named pieces without a picture
        widgets::ScrollBarStyle list_bar;
        Color divider = hex("#c8c8c8");
        std::array<std::string, construction_panel::kParams> param_icons; ///< Before each setting's box
        widgets::SpinnerStyle param;
        std::string facing; ///< Base name; the facing's number after it, from 1
        font::FontRole font = font::FontRole::Small;
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
        bool complete_dim = false;       ///< Its button is dimmed: it flashes over a way found, waiting for a click
        std::optional<bool> new_segment; ///< None when the track is one stretch; else whether another can begin now
        std::optional<usize> split;      ///< Of the split pictures, the one shown; none on a ride of one track
        bool heights_shown = false;
        bool heights_locked = false; ///< Shown for every ride, from the view menu, so the button cannot hide them
        std::u16string piece;        ///< Name of the piece picked, empty when nothing fits
        std::u16string cost;
        std::vector<NamedPiece> named;
        std::optional<usize> named_on; ///< The one picked
        /// The picked piece's settings, in PieceParam order, those it has.
        std::array<std::optional<ParamBox>, construction_panel::kParams> params { };
        std::optional<usize> facing; ///< Which way the cars face, of kFacings; none on rides where they cannot
    };

    enum class ConstructionAction : u8
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
        Split,      ///< Build on the other open end of side by side tracks, or the other kind of pieces
        Heights,    ///< Show or hide the track's heights
        Param,      ///< `index` the PieceParam, `value` its new value
        Facing,     ///< The cars to face the next way
    };

    struct ConstructionEvent
    {
        ConstructionAction action = ConstructionAction::Build;
        usize index = 0;
        i32 value = 0;

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

        [[nodiscard]] widgets::Button& curve_button(usize i) const
        {
            return *curves_.at(i);
        }
        [[nodiscard]] widgets::Button& slope_button(usize i) const
        {
            return *slopes_.at(i);
        }
        [[nodiscard]] widgets::Button& bank_button(usize i) const
        {
            return *banks_.at(i);
        }
        [[nodiscard]] widgets::Button& toggle_button(usize i) const
        {
            return *toggles_.at(i);
        }
        [[nodiscard]] widgets::Button& demolish_button() const
        {
            return *demolish_;
        }
        [[nodiscard]] widgets::Button& complete_button() const
        {
            return *complete_;
        }
        [[nodiscard]] widgets::Button& new_segment_button() const
        {
            return *new_segment_;
        }
        [[nodiscard]] widgets::Button& split_button() const
        {
            return *split_;
        }
        [[nodiscard]] widgets::Button& heights_button() const
        {
            return *heights_;
        }
        std::u16string heights_help;        ///< The heights button's help
        std::u16string heights_locked_help; ///< Its help while the view menu shows every ride's heights
        /// The named pieces without a picture.
        [[nodiscard]] widgets::ListBox& list() const
        {
            return *list_;
        }
        [[nodiscard]] widgets::Label& piece_label() const
        {
            return *piece_;
        }
        [[nodiscard]] widgets::Label& cost_label() const
        {
            return *cost_;
        }
        [[nodiscard]] widgets::Button& special_button(usize i) const
        {
            return *specials_.at(i);
        }
        [[nodiscard]] widgets::Spinner& param_spinner(PieceParam param) const
        {
            return *params_.at(std::to_underlying(param));
        }
        [[nodiscard]] widgets::Button& facing_button() const
        {
            return *facing_;
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
        std::array<widgets::Button*, construction_panel::kCurves> curves_ { };
        std::array<widgets::Button*, construction_panel::kSlopes> slopes_ { };
        std::array<widgets::Button*, construction_panel::kBanks> banks_ { };
        widgets::ListBox* list_ = nullptr;
        std::array<widgets::Button*, construction_panel::kToggles> toggles_ { };
        f32 list_left_ = construction_panel::kListLeft;
        widgets::Button* demolish_ = nullptr;
        widgets::Button* complete_ = nullptr;
        widgets::Button* new_segment_ = nullptr;
        widgets::Button* split_ = nullptr;
        widgets::Button* heights_ = nullptr;
        std::array<widgets::Button*, construction_panel::kSpecials> specials_ { };
        std::array<widgets::Button*, construction_panel::kParams> param_icons_ { };
        std::array<widgets::Spinner*, construction_panel::kParams> params_ { };
        widgets::Button* facing_ = nullptr;
        std::vector<usize> listed_;   ///< The named pieces in the list, by index in the view
        std::vector<usize> pictured_; ///< Those with picture buttons, by index in the view
        widgets::Label* piece_ = nullptr;
        widgets::Label* cost_ = nullptr;
        std::vector<ConstructionEvent> events_;
    };

} // namespace rct3::ui::screens::build
