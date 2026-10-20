// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game options window, with five pages picked by picture tabs in its top-right corner: general (units,
// money, sound and language), keys, graphics (the screen, the quality level and its custom settings),
// scenic (trees, terrain, water, sky and background, whatever the quality level) and camera. The name of
// the open page shows on the left of the tabs.
//
// Every control writes the option it shows as soon as it changes; there is no OK or Cancel, and the owner
// saves the options once the window has closed. Rows that only matter for some choices appear with them:
// the detailed graphics settings only with the custom quality level, the per-kind detail sliders only
// while asked for, the reflection choices only with reflective water, and the camera settings of the
// chosen control mode only.
//
// The language and the screen size are not game options: the window offers them and the owner takes the
// user's choice with take_language() and take_screen_size(). Nor are the keys: the keys page lists the game's
// commands as the game groups them, then the interface's, each on the keys the user picks (see key_map.hpp),
// and the owner takes the new key map with take_keys(). While one of its boxes waits for keys, the owner gives
// the window the keys as pressed.
//
// Options the program does not act on yet are listed in OptionsChoices::inert: their rows show, greyed, so the
// pages read as the game's do. The camera page ends with what the mouse buttons do in the chosen control
// mode, as the game tells it.
//
// The layout is ours, after the game's: a 460 unit wide page, names on the left and controls in a column
// on the right.
#pragma once

#include "config/game_options.hpp"
#include "core/maybe.hpp"
#include "ui/key_map.hpp"
#include "ui/screens/option_rows.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <bitset>
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
        KeyMap keys { };                           ///< In use; the keys page changes a copy
        /// The music files the park can play when zoomed out; the row shows with any. The choice goes into the
        /// options.
        std::vector<std::string> background_music { };
        std::optional<std::size_t> background_track { }; ///< Shown while the player has not chosen; nothing for no music
        std::bitset<config::kGameOptionCount> inert { }; ///< Options nothing acts on yet: shown, not usable
        bool language_locked = false;                    ///< The language shows but cannot change, e.g. in a park
    };

    /// The keys page: one scrolling list, the commands in groups under blue bars.
    struct KeyListStyle
    {
        float height = 270;
        Color background = hex("#ffffff");
        float top = 10;       ///< Before the first group
        float group_gap = 10; ///< Above each group's bar
        float bar_height = 24;
        float bar_text_inset = 5;
        Color bar_colour = hex("#2384bf");
        Color bar_text_colour = hex("#ffffff");
        float row_gap = 2; ///< Above each row
        float row_height = 18;
        float name_x = 5;
        float name_width = 280;
        float keys_x = 295;
        float keys_right = 18; ///< Where the keys end, from the list's right
        float bottom = 5;      ///< After the last row
        std::string focused;   ///< Skin of a key box waiting for keys
    };

    struct OptionsDialogStyle
    {
        WindowStyle window;
        OptionRowsStyle rows;
        KeyListStyle keys;
        ScrollBarStyle scroll;
        float page_width = 460;
        float page_height = 330;
        float tab_size = 23;
        float tab_step = 20; ///< The tabs overlap a little, as in the game
        float tabs_x = 304;  ///< Where the tabs start, from the page's left
        float tab_bar_height = 23;
        float caption_x = 59; ///< Right of the window's icon, under the title
        float caption_width = 160;
        float caption_height = 20;
        float scroll_height = 230; ///< Of the custom graphics settings
        FontRole caption_font = FontRole::Small;
        Color caption_colour = hex("#2484be");
        std::string icon;   ///< In the window's corner
        std::string button; ///< Skin family of the text buttons
        Rect button_inset;  ///< Of their text; see Button::text_inset
        float button_width = 110;
        float button_height = 22;
        std::string auto_detect_icon; ///< On the graphics page, top right
        float auto_detect_size = 40;
        float auto_detect_right = 40; ///< From the page's right edge to the icon's
    };

    enum class OptionsPage : std::uint8_t
    {
        General,
        Keys,
        Graphics,
        Scenic, ///< Ours, after the game's manual: what the park's surroundings show
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
        /// The key map with the keys the user set, once after each change.
        [[nodiscard]] std::optional<KeyMap> take_keys();
        /// True while a key box waits for keys, which should then do nothing else.
        [[nodiscard]] bool waiting_for_keys() const;

        [[nodiscard]] Button& tab(OptionsPage page) const
        {
            return *tabs_.at(static_cast<std::size_t>(page));
        }
        [[nodiscard]] ScrollArea& custom_graphics() const
        {
            return *custom_;
        }
        [[nodiscard]] std::optional<Dropdown&> language_list() const
        {
            return maybe(language_);
        }
        [[nodiscard]] KeyBox& key_box(Command command) const
        {
            return *key_boxes_.at(static_cast<std::size_t>(command));
        }
        /// Nothing for a game command whose keys cannot be changed.
        [[nodiscard]] std::optional<KeyBox&> play_key_box(PlayCommand command) const
        {
            return maybe(play_boxes_.at(static_cast<std::size_t>(command)));
        }
        [[nodiscard]] std::optional<Dropdown&> background_music_list() const
        {
            return maybe(music_);
        }
        /// The lines saying what each mouse button does in the chosen camera mode.
        [[nodiscard]] const Label& camera_help(std::size_t line) const
        {
            return *camera_help_.at(line);
        }
        [[nodiscard]] Button& auto_detect() const
        {
            return *auto_detect_;
        }

        void update(const UpdateContext& context) override;

    private:
        /// Makes the window as tall as the open page's shown rows.
        void fit_page();
        void build_general(Widget& page, const StringTable& strings);
        /// Selects the park music the options hold.
        void show_background_music();
        void update_background_music();
        void build_keys(Widget& page, const StringTable& strings);
        void build_graphics(Widget& page, const StringTable& strings);
        void build_scenic(Widget& page, const StringTable& strings);
        void build_camera(Widget& page, const StringTable& strings);
        void update_keys();
        void update_camera_help();

        OptionsDialogStyle style_;
        config::GameOptions& options_;
        OptionsChoices choices_;
        KeyMap keys_;
        std::array<Widget*, kOptionsPageCount> pages_ { };
        std::array<Button*, kOptionsPageCount> tabs_ { };
        std::array<std::u16string, kOptionsPageCount> captions_;
        Label* caption_ = nullptr;
        std::vector<std::unique_ptr<OptionRows>> rows_;
        OptionRows* graphics_top_ = nullptr;
        ScrollArea* custom_ = nullptr;
        Dropdown* language_ = nullptr;
        Dropdown* resolution_ = nullptr;
        Dropdown* music_ = nullptr; // The park's: the files, then shuffle, then none
        std::array<KeyBox*, kCommandCount> key_boxes_ { };
        std::array<KeyBox*, kPlayCommandCount> play_boxes_ { };
        Button* usual_keys_ = nullptr;
        Button* auto_detect_ = nullptr;
        std::array<Label*, 5> camera_help_ { };
        std::array<std::u16string, 5> camera_buttons_; // The help lines' starts
        std::vector<std::u16string> camera_actions_;   // Each CameraAction's words
        OptionsPage page_ = OptionsPage::General;
        int changes_ = 0;
        std::optional<std::string> picked_language_;
        std::optional<std::size_t> picked_screen_size_;
        std::optional<KeyMap> picked_keys_;
    };

} // namespace rct3::ui
