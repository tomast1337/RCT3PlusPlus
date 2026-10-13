// SPDX-License-Identifier: GPL-3.0-or-later
//
// The career map: the screen a career is played from, in place of the front screen and over its bands,
// without its arc or logo. The scenarios sit on an arc round the screen's open circle, each a round picture
// marked with a padlock (shut while locked, open until done) or the hat of the best level reached; locked
// ones look the same but cannot be picked. The picked scenario is drawn larger than the rest, and every
// button grows while the pointer is over it. On the left a panel gives its name and
// description, with the new, resume and load buttons under it, and its large picture sits in the bottom-left
// corner, the piece in front of it settling from a little too big each time a scenario is picked. Return to
// the main menu and quit sit at the bottom of the arc. The bottom strip names what is under the pointer:
// a button, or a scenario and, if it is locked, what it waits for.
//
// Picking a scenario only shows it; new and resume ask first, as do going back to the main menu and
// quitting. Escape asks to go back. The screen knows nothing of files or careers: the owner gives the
// scenarios and says whether each has a game to resume, and takes what the user asked for.
#pragma once

#include "career/career.hpp"
#include "ui/career_scenarios.hpp"
#include "ui/screens/front_screen.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/question_box.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    namespace career_board
    {
        // In the front screen's 1024 by 768 design frame, y down (front_screen_layout.hpp).

        /// The scenarios on a circle round the buttons' centre, the first at the top left.
        inline constexpr float kScenarioRadius = 435;
        inline constexpr float kScenarioFirstAngle = 2.7f;
        inline constexpr float kScenarioAngleStep = 0.15f;
        inline constexpr float kScenarioSize = 64;
        inline constexpr float kPickedFactor = 1.388f; ///< The picked scenario's size, times kScenarioSize
        /// The round pictures' size, times their own: the others', the picked one's.
        inline constexpr float kIconScale = 0.5f;
        inline constexpr float kPickedIconScale = 0.752f;
        /// Where a scenario's mark goes, in fractions of its button: from the left, from the top, and its size.
        inline constexpr Point kMarkAt { 0.341f, 0.441f };
        inline constexpr float kMarkSize = 0.5f;
        /// How much a button grows under the pointer, times its size: the picked scenario, the rest; and how
        /// fast, in sizes a second.
        inline constexpr float kPickedSwell = 1.05f;
        inline constexpr float kSwell = 1.46f;
        inline constexpr float kSwellRate = 5;

        /// The panel: its sides, its top, and the least it reaches down; a long description takes it lower.
        inline constexpr float kPanelLeft = 15;
        inline constexpr float kPanelRight = 260;
        inline constexpr float kPanelTop = 40;
        inline constexpr float kPanelBottom = 300;
        inline constexpr float kTextLeft = 25; ///< The name and description, inside the panel
        inline constexpr float kTextRight = 250;
        inline constexpr float kTitleGap = 10; ///< Under the name

        /// The buttons under the description: squares of kButtonSize, kButtonUp of a button above the
        /// panel's bottom, counted back from its right edge in steps of kButtonStep buttons.
        inline constexpr float kButtonSize = 32;
        inline constexpr float kButtonUp = 0.4f;
        inline constexpr float kButtonStep = 1.2f;
        inline constexpr std::array<float, 3> kButtonSteps = { 4.1f, 3.1f, 2.1f }; ///< New, resume, load

        /// Main menu and quit, on the front screen's small circle.
        inline constexpr std::array<float, 2> kSmallAngles = { 4.02f, 4.1f };

        /// The large picture: its bottom-left corner kPictureBottom above the screen's bottom, at
        /// kPictureScale of its own size when a front piece goes over it. The front piece settles from
        /// kPopFactor bigger over kPopTime seconds.
        inline constexpr float kPictureBottom = 74;
        inline constexpr float kPictureScale = 0.75f;
        inline constexpr float kPopFactor = 1.1f;
        inline constexpr float kPopTime = 0.3f;

        [[nodiscard]] float scenario_angle(std::size_t index);
        /// How big the front piece is, times its settled size, `time` seconds after the pick.
        [[nodiscard]] float pop(float time);
    } // namespace career_board

    struct CareerBoardStyle
    {
        std::string scenario_button;
        std::string small_button; ///< Under the panel, and main menu and quit
        std::string panel;
        std::string new_icon;
        std::string resume_icon;
        std::string load_icon;
        std::string menu_icon;
        std::string quit_icon;
        std::string locked; ///< Marks: a locked scenario, an open one, then the hats of the levels reached
        std::string unlocked;
        std::array<std::string, 3> hats; ///< Apprentice, entrepreneur, tycoon
        std::string no_picture;          ///< For a scenario without a large picture
        QuestionBoxStyle question;
        Color text_colour { 0, 0, 0, 255 };
    };

    /// One scenario as the map shows it.
    struct CareerBoardScenario
    {
        CareerScenarioInfo info;
        career::ScenarioStatus status = career::ScenarioStatus::Locked;
        career::Lock lock;
        bool can_resume = false; ///< Its last game is there to go back to
    };

    class CareerBoard : public Widget
    {
    public:
        /// `picked` is shown first.
        CareerBoard(std::string name, CareerBoardStyle style, const StringTable& strings, std::vector<CareerBoardScenario> scenarios, std::size_t picked);

        [[nodiscard]] std::size_t size() const
        {
            return scenarios_.size();
        }
        [[nodiscard]] std::size_t picked() const
        {
            return picked_;
        }
        /// Shows scenario `index`, if it is not locked.
        void pick(std::size_t index);
        void set_can_resume(std::size_t index, bool can);

        /// The scenario the user picked, once each time.
        [[nodiscard]] std::optional<std::size_t> take_picked();
        /// The scenario to start anew, or whose last game to resume, once the user has said yes.
        [[nodiscard]] std::optional<std::size_t> take_new();
        [[nodiscard]] std::optional<std::size_t> take_resume();
        /// True once when the load button was pressed.
        [[nodiscard]] bool take_load();
        /// True once the user has said yes to going back to the main menu.
        [[nodiscard]] bool take_menu();
        /// True once when quit was pressed; the owner asks.
        [[nodiscard]] bool take_quit();

        // For tests and owners that drive the screen.
        [[nodiscard]] FrontButton& scenario_button(std::size_t index) const
        {
            return *buttons_.at(index);
        }
        [[nodiscard]] FrontButton& new_button() const
        {
            return *new_;
        }
        [[nodiscard]] FrontButton& resume_button() const
        {
            return *resume_;
        }
        [[nodiscard]] FrontButton& load_button() const
        {
            return *load_;
        }
        [[nodiscard]] FrontButton& menu_button() const
        {
            return *menu_;
        }
        [[nodiscard]] FrontButton& quit_button() const
        {
            return *quit_;
        }
        [[nodiscard]] QuestionBox* question() const
        {
            return question_;
        }
        /// The panel's text and the two lines along the bottom.
        [[nodiscard]] std::u16string_view title() const
        {
            return scenarios_.at(picked_).info.title;
        }
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
        enum class Asking : std::uint8_t
        {
            New,
            Resume,
            Menu,
        };

        [[nodiscard]] std::u16string text(std::string_view key) const;
        [[nodiscard]] std::u16string numbered(std::string_view key, std::size_t number) const;
        void ask(Asking what);
        void hover_text();
        void layout(const UpdateContext& context);

        CareerBoardStyle style_;
        const StringTable& strings_;
        std::vector<CareerBoardScenario> scenarios_;
        std::vector<FrontButton*> buttons_;
        FrontButton* new_ = nullptr;
        FrontButton* resume_ = nullptr;
        FrontButton* load_ = nullptr;
        FrontButton* menu_ = nullptr;
        FrontButton* quit_ = nullptr;
        QuestionBox* question_ = nullptr;
        Asking asking_ = Asking::New;
        std::size_t picked_ = 0;
        float since_pick_ = 0;
        Rect panel_;
        Rect title_box_;
        Rect description_box_;
        std::u16string help_title_;
        std::u16string help_text_;
        std::optional<std::size_t> picked_out_;
        std::optional<std::size_t> new_out_;
        std::optional<std::size_t> resume_out_;
        bool load_out_ = false;
        bool menu_out_ = false;
        bool quit_out_ = false;
    };

} // namespace rct3::ui
