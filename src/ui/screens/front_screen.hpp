// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front screen: the game's main menu and the menus under it, laid out as in front_screen_layout.hpp.
//
// Each menu is a short list of round picture buttons. A button either opens another menu, whose buttons fly
// in along the circle while the old ones move out of the way, or asks for something the front screen does
// not do itself (start a game, open the options); the owner takes those with take_request(). Back returns
// to the menu before, and Escape does the same. Quit is a request too, so the owner can ask first.
//
// Resting the pointer on a button shows its name and a line of help along the bottom of the screen; the
// names also show in the boxes beside the buttons once they have arrived.
//
// All text comes from the game's string table by key, and every picture is a skin by the game's name for
// it; what is missing is left out.
#pragma once

#include "ui/screens/front_screen_layout.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class FrontMenu : std::uint8_t
    {
        Main,
        Play,
        Tools,
        Sandbox,
        CustomScenario,
        Campaigns, ///< Which game's career to play: the original or an expansion
        Tutorials, ///< Likewise for the tutorials
        Count_,
    };
    inline constexpr std::size_t kFrontMenuCount = static_cast<std::size_t>(FrontMenu::Count_);

    /// What a button does.
    enum class FrontAction : std::uint8_t
    {
        OpenMenu,
        Options,
        PeopleDesigner,
        ScenarioEditor,
        CoasterDesigner,
        BuildingDesigner,
        NewSandbox,
        LoadSandbox,
        NewCustomScenario,
        LoadCustomScenario,
        Campaign, ///< Of `pack`
        Tutorial, ///< Of `pack`
        Quit,
    };

    struct FrontEntry
    {
        std::string_view label_key; ///< The button's name
        std::string_view help_key;  ///< Its line of help; may be empty
        std::string_view icon;      ///< Skin
        FrontAction action = FrontAction::OpenMenu;
        FrontMenu menu = FrontMenu::Main; ///< For OpenMenu
        int pack = 0;                     ///< For Campaign and Tutorial: 0 the original game, then each expansion
    };

    struct FrontMenuInfo
    {
        FrontMenu parent = FrontMenu::Main;
        std::span<const FrontEntry> entries;
        std::string_view title_key; ///< Shown on the bottom strip while no button is under the pointer
    };

    /// Every menu, by FrontMenu.
    [[nodiscard]] const FrontMenuInfo& front_menu(FrontMenu menu);

    inline constexpr int kExpansionCount = 2;

    /// What the owner is asked to do.
    struct FrontRequest
    {
        FrontAction action = FrontAction::Options;
        int pack = 0;

        friend bool operator==(const FrontRequest&, const FrontRequest&) = default;
    };

    /// A round button: its background stays put, the picture on it grows while the pointer is over it.
    class FrontButton : public Button
    {
    public:
        using Button::Button;
        /// The front screen's buttons click as the pointer passes over them, as in the game.
        [[nodiscard]] bool clicks_on_hover() const override
        {
            return true;
        }
        void draw(DrawContext& context) const override;
    };

    /// The front screen's artwork, shared with the screens that stand in for it: the bands and the bottom
    /// strip; the arc and the logo; the two lines along the bottom strip.
    void draw_front_bands(DrawContext& context, const front_screen::Frame& frame);
    void draw_front_arc(DrawContext& context, const front_screen::Frame& frame, std::string_view logo);
    void draw_front_help(DrawContext& context, const front_screen::Frame& frame, std::u16string_view title, std::u16string_view text);
    /// The logo's skin for `language`.
    [[nodiscard]] std::string front_logo(std::string_view language);

    /// The expansion packs the strings name: a guess at which are installed, as their names come with them.
    [[nodiscard]] std::array<bool, kExpansionCount> named_expansions(const StringTable& strings);

    class FrontScreen : public Widget
    {
    public:
        /// `expansions` says which expansion packs are installed; the others' buttons are disabled and
        /// their badges not shown. `language` picks the logo.
        FrontScreen(std::string name, const StringTable& strings, std::array<bool, kExpansionCount> expansions, std::string_view language);

        [[nodiscard]] FrontMenu current() const
        {
            return current_;
        }
        /// Opens `menu` as if its button had been pressed: its parent moves out, its buttons fly in.
        void open(FrontMenu menu);
        /// Back to the parent menu; nothing on the main menu.
        void back();
        /// Puts every button where it is going and shows the labels, skipping the animation; for a screen
        /// built again in place of one already shown.
        void settle();

        [[nodiscard]] std::optional<FrontRequest> take_request();

        /// The button of entry `index` of `menu`; null when there is none.
        [[nodiscard]] FrontButton* button(FrontMenu menu, std::size_t index) const;
        [[nodiscard]] FrontButton& back_button() const
        {
            return *back_;
        }
        [[nodiscard]] FrontButton& quit_button() const
        {
            return *quit_;
        }
        /// The name shown in the box beside button `index`, and how far it has faded in (0 to 1).
        [[nodiscard]] std::u16string_view label(std::size_t index) const;
        [[nodiscard]] float label_alpha(std::size_t index) const;
        /// The two lines of help along the bottom.
        [[nodiscard]] std::u16string_view help_title() const
        {
            return help_title_;
        }
        [[nodiscard]] std::u16string_view help_text() const
        {
            return help_text_;
        }

        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        struct MenuButton
        {
            FrontButton* button = nullptr;
            const FrontEntry* entry = nullptr;
            front_screen::Polar at = front_screen::kStart;
            bool arrived = false;
        };
        struct Badge
        {
            Widget* widget = nullptr;
            std::string skin;
        };

        [[nodiscard]] front_screen::Polar goal(FrontMenu menu, std::size_t index) const;
        void clear_labels();

        const StringTable& strings_;
        std::string logo_;
        std::array<std::vector<MenuButton>, kFrontMenuCount> buttons_;
        FrontButton* back_ = nullptr;
        FrontButton* quit_ = nullptr;
        std::vector<Badge> badges_;
        FrontMenu current_ = FrontMenu::Main;
        std::optional<FrontMenu> parent_; // Moved out, while a child of it is open
        std::array<std::u16string, front_screen::kMaxMenuEntries> labels_;
        std::array<float, front_screen::kMaxMenuEntries> label_alpha_ { };
        std::u16string help_title_;
        std::u16string help_text_;
        std::vector<FrontRequest> requests_;
    };

} // namespace rct3::ui
