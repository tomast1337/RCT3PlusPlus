// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that leads the player through a tutorial, one step at a time. It is a large framed window with
// the tutorial's guide in the frame's corner circle, the tutorial's name as its title and the step's title on
// a line under the title rule. Below that is the step's text, wrapped, with Quit in the bottom-left corner and
// Continue in the bottom-right; Continue is missing on the last step. There is no close button.
//
// Each step stores how wide its page is and where on screen its window goes: the middle, or the top-right or
// bottom-right corner. The window is as tall as the step's text needs, with a floor, and goes to the step's
// place whenever a new step starts. Within a step the player may drag it; a change of size keeps its top-left
// corner, so it does not jump while the text is measured or the language changes.
//
// The text is the language's where the strings have it, under keys built from the tutorial's name and the
// step's number, else the English stored in the park. The window does not stop the player using the park
// round it. Quit and Escape only ask: the owner confirms and leaves the tutorial.
#pragma once

#include "save/model/tutorial.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace rct3::ui
{

    /// The title and text of step `index` (from 0) of `script`, in the language where the strings have it.
    struct TutorialStepText
    {
        std::u16string title;
        std::u16string text;
    };
    [[nodiscard]] TutorialStepText tutorial_step_text(const StringTable& strings, const save::TutorialScript& script, std::size_t index);

    /// The tutorial's own name, in the language where the strings have it.
    [[nodiscard]] std::u16string tutorial_title(const StringTable& strings, const save::TutorialScript& script);

    /// The skins and measurements of the guide window. Lengths in logical units.
    struct TutorialGuideStyle
    {
        WindowStyle window; ///< Its content inset leaves room for the step line; the content is the page
        std::string portrait;
        std::string button; ///< Skin family of Quit and Continue
        Rect button_inset;  ///< See Button::text_inset
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Rect step_line { 67, 46, 13, 21 }; ///< Left inset, top from the window's top, right inset, height
        float min_width = 282;             ///< Of the window
        float min_page_width = 281;        ///< A step may ask for a wider page, not a narrower one
        float min_page_height = 220;
        float text_top = 5;  ///< From the page's top to its text
        float text_side = 3; ///< Each side of the text inside the page
        float text_gap = 15; ///< Between the text's last line and the buttons
        Point button_size { 90, 30 };
        float screen_margin = 24; ///< From the desktop's edges, for steps that go in a corner
    };

    /// Where a step's window goes on the desktop.
    enum class TutorialGuidePlace : std::uint8_t
    {
        Centre,
        TopRight,
        BottomRight,
    };

    /// The place a step stores as a number; numbers without a meaning go in the middle.
    [[nodiscard]] TutorialGuidePlace tutorial_guide_place(std::int32_t stored);

    /// Sizes of the window for one step: the page holds the text and the buttons.
    struct TutorialGuideSize
    {
        Point page;
        Point window;
    };
    /// For a step that asks for a page `stored_width` wide (0 or less when it does not say) whose text is
    /// `text_height` tall when wrapped at the page's text width.
    [[nodiscard]] TutorialGuideSize tutorial_guide_size(const TutorialGuideStyle& style, float stored_width, float text_height);
    /// Where the text of a page `page_width` wide wraps.
    [[nodiscard]] float tutorial_guide_text_width(const TutorialGuideStyle& style, float page_width);

    /// A window of `size` at `place` on the desktop.
    [[nodiscard]] Placement tutorial_guide_placement(const TutorialGuideStyle& style, TutorialGuidePlace place, Point size);

    class TutorialGuide : public Window
    {
    public:
        /// Shows step one of `script`.
        TutorialGuide(std::string name, const TutorialGuideStyle& style, const StringTable& strings, save::TutorialScript script);

        [[nodiscard]] std::size_t step() const
        {
            return step_;
        }
        [[nodiscard]] std::size_t steps() const
        {
            return script_.steps.size();
        }
        [[nodiscard]] const save::TutorialScript& script() const
        {
            return script_;
        }
        [[nodiscard]] Label& step_line() const
        {
            return *step_line_;
        }
        [[nodiscard]] Label& text() const
        {
            return *text_;
        }
        [[nodiscard]] Button& continue_button() const
        {
            return *continue_;
        }
        [[nodiscard]] Button& quit_button() const
        {
            return *quit_;
        }

        /// True once each time the player asks to leave.
        [[nodiscard]] bool take_quit();

        void update(const UpdateContext& context) override;

    private:
        void show(std::size_t index);
        void fit(float text_height, bool measured);

        TutorialGuideStyle style_;
        const StringTable& strings_;
        save::TutorialScript script_;
        Label* step_line_ = nullptr;
        Label* text_ = nullptr;
        Button* continue_ = nullptr;
        Button* quit_ = nullptr;
        std::size_t step_ = 0;
        Point size_;            // Of the window as last fitted
        bool to_place_ = false; // A new step goes to its place once its text has been measured
        bool quit_out_ = false;
    };

} // namespace rct3::ui
