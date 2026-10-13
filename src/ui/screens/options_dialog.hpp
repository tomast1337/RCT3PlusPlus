// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game options window, with four pages picked by picture tabs in its top-right corner: general (units,
// money, sound and language), keys, graphics and camera. The name of the open page shows on the left of
// the tabs.
//
// Every control writes the option it shows as soon as it changes; there is no OK or Cancel, and the owner
// saves the options once the window has closed. Rows that only matter for some choices appear with them:
// the detailed graphics settings only with the custom quality level, the per-kind detail sliders only
// while asked for, the reflection choices only with reflective water, and the camera settings of the
// chosen control mode only.
//
// The language and the screen size are not game options: the window offers them and the owner takes the
// user's choice with take_language() and take_screen_size(). The keys page lists the keys the interface
// uses; they cannot be changed yet.
//
// The layout is ours, after the game's: a 460 unit wide page, names on the left and controls in a column
// on the right.
#pragma once

#include "config/game_options.hpp"
#include "ui/screens/option_rows.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct ScreenSize
    {
        int width = 0;
        int height = 0;

        friend bool operator==(const ScreenSize&, const ScreenSize&) = default;
    };

    /// What the window offers besides the options.
    struct OptionsChoices
    {
        std::vector<std::string> languages; ///< Installed, by the game's names; the row shows with two or more
        std::string language;               ///< The one in use
        std::vector<ScreenSize> screen_sizes;
        std::optional<std::size_t> screen_size;    ///< The one in use, if it is in the list
        std::vector<int> anti_aliasing_levels { }; ///< Offered after "none", e.g. 2, 4 and 8 samples
    };

    struct OptionsDialogStyle
    {
        WindowStyle window;
        OptionRowsStyle rows;
        ScrollBarStyle scroll;
        float page_width = 460;
        float page_height = 330;
        float tab_size = 23;
        float tab_step = 25;
        float tabs_x = 265; ///< Where the tabs start, from the page's left
        float tab_bar_height = 26;
        float scroll_height = 230; ///< Of the custom graphics settings
        FontRole caption_font = FontRole::Medium;
    };

    enum class OptionsPage : std::uint8_t
    {
        General,
        Keys,
        Graphics,
        Camera,
        Count_,
    };
    inline constexpr std::size_t kOptionsPageCount = static_cast<std::size_t>(OptionsPage::Count_);

    class OptionsDialog : public Window
    {
    public:
        /// The window writes to `options`, which must outlive it.
        OptionsDialog(std::string name, const OptionsDialogStyle& style, const StringTable& strings, config::GameOptions& options, OptionsChoices choices);

        [[nodiscard]] OptionsPage page() const
        {
            return page_;
        }
        void show_page(OptionsPage page);

        /// True once after the user changed any option.
        [[nodiscard]] bool take_change();
        /// The language the user picked, once.
        [[nodiscard]] std::optional<std::string> take_language();
        /// The index into OptionsChoices::screen_sizes the user picked, once.
        [[nodiscard]] std::optional<std::size_t> take_screen_size();

        [[nodiscard]] Button& tab(OptionsPage page) const
        {
            return *tabs_.at(static_cast<std::size_t>(page));
        }
        [[nodiscard]] ScrollArea& custom_graphics() const
        {
            return *custom_;
        }
        [[nodiscard]] Dropdown* language_list() const
        {
            return language_;
        }

        void update(const UpdateContext& context) override;

    private:
        void build_general(Widget& page, const StringTable& strings);
        void build_keys(Widget& page, const StringTable& strings);
        void build_graphics(Widget& page, const StringTable& strings);
        void build_camera(Widget& page, const StringTable& strings);

        OptionsDialogStyle style_;
        config::GameOptions& options_;
        OptionsChoices choices_;
        std::array<Widget*, kOptionsPageCount> pages_ { };
        std::array<Button*, kOptionsPageCount> tabs_ { };
        std::array<std::u16string, kOptionsPageCount> captions_;
        Label* caption_ = nullptr;
        std::vector<std::unique_ptr<OptionRows>> rows_;
        OptionRows* graphics_top_ = nullptr;
        ScrollArea* custom_ = nullptr;
        Dropdown* language_ = nullptr;
        Dropdown* resolution_ = nullptr;
        OptionsPage page_ = OptionsPage::General;
        int changes_ = 0;
        std::optional<std::string> picked_language_;
        std::optional<std::size_t> picked_screen_size_;
    };

} // namespace rct3::ui
