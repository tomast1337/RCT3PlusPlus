// SPDX-License-Identifier: GPL-3.0-or-later
//
// The panel that builds a tracked ride piece by piece, as the game lays it out: a strip along the bottom-left
// of the screen, 652 by 100 units. Its left part has two rows of seven buttons, the curve on top (tight,
// medium and loose left, straight, then loose, medium and tight right) and below it the slope the piece ends
// at (vertical, steep and gentle down, level, then gentle, steep and vertical up). Right of them are the
// toggles (only the lift chain here) in a column, then the bank dial: eight small buttons round a disc,
// upside down at the top, level at the bottom. Under the rows go the picked piece's name and its price, and
// the buttons that finish the track for the player and that pull down the last piece sit to the right of them. The right part lists the pieces with
// a use of their own (stations, loops, brakes and the like) by name, a page at a time.
//
// Each picture button shows one of three pictures: picked, offered, or not offered (see IconState). The
// curve, slope and bank buttons have no help text in the game; the toggles and the demolish button do.
//
// The panel does not know about track: its owner shows it what to offer (show()) and hears what the player
// did (take_event()). Picking the picked curve, slope or bank again is the game's way to build the piece; the
// owner hears that as a Build event. Closing goes through take_closed().
#pragma once

#include "ui/font_set.hpp"
#include "ui/widget.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"

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
        inline constexpr Point kNewSegmentAt { 366, 70 };
        inline constexpr Point kNewSegmentSize { 40, 28 };
        inline constexpr float kNamedLeft = 410;
        inline constexpr float kNamedWidth = 205;
        inline constexpr std::size_t kNamedRows = 4;
        inline constexpr std::size_t kNamedColumns = 1; ///< One a row: two would cut the longer names
        inline constexpr float kNamedTop = 6;
        inline constexpr float kNamedRowHeight = 22;
        inline constexpr float kPagerLeft = 622; ///< Of the paging arrows, right of the names
        inline constexpr float kPagerSize = 16;
        inline constexpr float kCloseSize = 14;
        inline constexpr Point kCloseAt { 8, 8 };

        inline constexpr std::size_t kCurves = 7;
        inline constexpr std::size_t kSlopes = 7;
        inline constexpr std::size_t kBanks = 8; ///< From hardest left to upside down
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

    struct ConstructionPanelStyle
    {
        std::string strip;        ///< Behind everything right of the ring
        float bottom = 0;         ///< From the screen's bottom edge: the tool row and its open menu stay clear below
        std::string title_button; ///< Skin family of the close button
        std::string close_icon;
        std::array<std::string, construction_panel::kCurves> curves; ///< Base names, leftmost first
        std::array<std::string, construction_panel::kSlopes> slopes; ///< Steepest down first
        std::array<std::string, construction_panel::kBanks> banks;   ///< In the panel's order
        std::string dial;
        std::string chain; ///< Base name
        std::string demolish;
        std::string complete;     ///< Finishes the track
        std::string new_segment;  ///< Begins another stretch of track
        std::string named_button; ///< Skin family of the named pieces' buttons
        Rect named_inset;
        std::string page_up;
        std::string page_down;
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
    };

    /// What the panel shows: one state per button, the piece's name and price, the named pieces.
    struct ConstructionView
    {
        std::array<IconState, construction_panel::kCurves> curves { };
        std::array<IconState, construction_panel::kSlopes> slopes { };
        std::array<IconState, construction_panel::kBanks> banks { };
        std::optional<IconState> chain; ///< None when the track has no chain pieces at all
        bool can_demolish = false;
        bool can_complete = false;       ///< The track has an open end to finish from
        std::optional<bool> new_segment; ///< None when the track is one stretch; else whether another can begin now
        std::u16string piece;            ///< Name of the piece picked, empty when nothing fits
        std::u16string cost;
        std::vector<std::u16string> named;   ///< Names of the pieces with a use of their own
        std::optional<std::size_t> named_on; ///< The one picked
    };

    enum class ConstructionAction : std::uint8_t
    {
        Curve, ///< `index` of the curves
        Slope,
        Bank,
        Chain,
        Named, ///< `index` of the view's named pieces
        Build, ///< The picked button clicked again
        Demolish,
        Complete,   ///< Finish the track
        NewSegment, ///< Begin another stretch of track
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
        /// Help texts come from `chain_help` and `demolish_help`, as the strings name them.
        ConstructionPanel(std::string name, const ConstructionPanelStyle& style, std::u16string chain_help, std::u16string demolish_help);

        /// Shows `view` from now on.
        void show(ConstructionView view);
        [[nodiscard]] const ConstructionView& view() const
        {
            return view_;
        }

        /// What the player did since the last call, oldest first, once.
        [[nodiscard]] std::optional<ConstructionEvent> take_event();
        /// True once after the close button was clicked.
        [[nodiscard]] bool take_closed();

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
        [[nodiscard]] Button& chain_button() const
        {
            return *chain_;
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
        [[nodiscard]] Button& named_button(std::size_t i) const
        {
            return *named_.at(i);
        }
        [[nodiscard]] Label& piece_label() const
        {
            return *piece_;
        }
        [[nodiscard]] Label& cost_label() const
        {
            return *cost_;
        }
        [[nodiscard]] std::size_t page() const
        {
            return page_;
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
        std::array<Button*, construction_panel::kNamedRows * construction_panel::kNamedColumns> named_ { };
        Button* chain_ = nullptr;
        Button* demolish_ = nullptr;
        Button* complete_ = nullptr;
        Button* new_segment_ = nullptr;
        Button* close_ = nullptr;
        Button* page_up_ = nullptr;
        Button* page_down_ = nullptr;
        Label* piece_ = nullptr;
        Label* cost_ = nullptr;
        std::vector<ConstructionEvent> events_;
        std::size_t page_ = 0;
        bool closed_ = false;
    };

} // namespace rct3::ui
