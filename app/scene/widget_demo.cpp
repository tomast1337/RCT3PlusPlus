// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/widget_demo.hpp"

#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/scale.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/list_box.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tabs.hpp"
#include "ui/widgets/tickbox.hpp"
#include "ui/widgets/window.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kToggleFamily = "Staff_TaskButtonBins";
        constexpr float kWidth = 340;
        constexpr float kHeight = 300;
        constexpr float kMinWidth = 320; ///< Room for the button row
        constexpr float kMinHeight = 280;
        constexpr float kCascade = 28; ///< How far each new window opens from the last

        // Game string keys the demo shows.
        constexpr std::string_view kTitleKey = "GameOptionsDialog_Title";
        constexpr std::string_view kHelpKey = "GameOptionsDialog_WaterReflect_EnabledHelp";
        constexpr std::array<std::string_view, 3> kOptionKeys = { "GameOptionsDialog_AutoScrollAtEdge", "GameOptionsDialog_BackgroundMusic", "GameOptionsDialog_Bloom" };
        constexpr std::array<std::string_view, 3> kOptionTips = { "GameOptionsDialog_AutoScrollAtEdgeHelp", "GameOptionsDialog_BackgroundMusicTT", "GameOptionsDialog_Bloom_TT" };
        constexpr std::array<std::string_view, 3> kTextButtonKeys = { "SMB_Label_OK", "SMB_Label_Cancel", "SMB_Label_Help" };
        constexpr float kTextButtonWidth = 70;
        constexpr float kTextButtonHeight = 26;
        constexpr std::size_t kNameLength = 40;
        constexpr float kButtonRow = 36; ///< Below the tabs
        constexpr float kRowHeight = 18; ///< Of the controls page
        constexpr std::array<std::u16string_view, 3> kDetailLevels = { u"Low detail", u"Medium detail", u"High detail" };
        constexpr float kMenuBarHeight = 20;
        constexpr std::array<std::string_view, 4> kToolIcons = { "File_GameOptions_Game", "File_GameOptions_Graphics", "File_GameOptions_Camera", "File_GameOptions_Keyboard" };
        constexpr std::array<std::u16string_view, 4> kToolTips = { u"Open a window", u"Graphics", u"Camera", u"Keyboard" };
        // The menus, by index in the bar.
        constexpr std::size_t kWindowMenu = 0;
        constexpr std::size_t kScaleMenu = 1;
        constexpr std::size_t kNewWindowItem = 0;
        constexpr std::size_t kCloseWindowItem = 1;
        constexpr std::size_t kAutoScaleItem = 0; ///< Then one item for each whole scale from 1
        constexpr int kScaleChoices = 4;
        constexpr std::size_t kCloseAllItem = 3;
        constexpr std::size_t kCloseAllYes = 0;
        constexpr float kIconSwell = 2; ///< Of the confirm and cancel buttons
        constexpr std::array<std::u16string_view, 12> kRideNames = { u"Bumper Cars", u"Carousel", u"Chairlift", u"Drop Tower", u"Ferris Wheel", u"Ghost Train", u"Go Karts", u"Log Flume", u"Monorail", u"Spiral Slide", u"Swinging Ship", u"Wooden Coaster" };
        // Characters the game fonts lack, to show the fallback font: "hello world" in Greek and Russian,
        // arrows and a few signs.
        constexpr std::u16string_view kFallbackSample = u"\n\n\u0393\u03B5\u03B9\u03AC \u03C3\u03BF\u03C5 \u03BA\u03CC\u03C3\u03BC\u03B5 \u00B7 \u041F\u0440\u0438\u0432\u0435\u0442, \u043C\u0438\u0440 \u00B7 \u2190 \u2191 \u2192 \u2193 \u00B7 \u00BD \u00BE \u2122 \u25A0";

        class DemoWindow : public ui::Window
        {
        public:
            DemoWindow(std::string name, float cascade, const ui::StringTable& strings)
                : ui::Window(std::move(name), skins::window_style(), kWidth, kHeight)
                , strings_(strings)
            {
                placement = ui::centred(kWidth, kHeight, cascade, cascade);
                title = strings.get(kTitleKey);
                // Everything below is anchored to the edges it follows, so the window can be resized.
                resizable = true;
                min_size = { kMinWidth, kMinHeight };
                ui::Widget& body = content();

                // Three pages above the button row: text, a list and controls.
                auto& tabs = body.add<ui::Tabs>("tabs", skins::tabs_style());
                tabs.placement = { { 0, 0, 1, 1 }, { 0, 0, 0, -kButtonRow } };
                build_text_page(tabs.add_page("text", u"Text"), strings);
                build_list_page(tabs.add_page("list", u"List"));
                build_controls_page(tabs.add_page("controls", u"Controls"), strings);

                cancel_ = &body.add<ui::Button>("cancel", std::string(skins::kButton), std::string(skins::kCancelIcon));
                cancel_->placement = ui::bottom_right(0, 4, 30, 30);
                confirm_ = &body.add<ui::Button>("confirm", std::string(skins::kButton), std::string(skins::kConfirmIcon));
                confirm_->placement = ui::bottom_right(36, 4, 30, 30);

                // Text buttons: OK opens another window like confirm, Cancel closes this one, Help is disabled.
                std::array<ui::Button*, kTextButtonKeys.size()> text_buttons { };
                for (std::size_t i = 0; i < kTextButtonKeys.size(); ++i)
                {
                    auto& b = body.add<ui::Button>("text" + std::to_string(i), std::string(skins::kTextButton));
                    b.text = strings.get(kTextButtonKeys[i]);
                    b.text_inset = skins::kLozengeInset;
                    b.placement = ui::bottom_left(static_cast<float>(i) * (kTextButtonWidth + 6), 6, kTextButtonWidth, kTextButtonHeight);
                    text_buttons[i] = &b;
                }
                ok_ = text_buttons[0];
                text_cancel_ = text_buttons[1];
                default_button = ok_;
                cancel_button = text_cancel_;
                cancel_->swell = kIconSwell;
                confirm_->swell = kIconSwell;
                text_buttons[2]->enabled = false;
            }

            void build_text_page(ui::Widget& page, const ui::StringTable& strings)
            {
                // The text area stretches with the window; the name field stays on the bottom edge.
                auto& list = page.add<ui::Panel>("list", std::string(skins::kListBox));
                list.placement = { { 0, 0, 1, 1 }, { 0, 0, 0, -26 } };
                // The help text runs on with the options' help, more than fits, to show scrolling.
                std::u16string text = std::u16string(strings.get(kHelpKey)) + std::u16string(kFallbackSample);
                for (const std::string_view key : kOptionTips)
                    text += u"\n\n" + std::u16string(strings.get(key));
                auto& scroll = list.add<ui::ScrollArea>("scroll", skins::scroll_bar_style(ui::Orientation::Vertical));
                scroll.placement = ui::fill(2);
                auto& help = scroll.content().add<ui::Label>("help", std::move(text), ui::FontRole::Small);
                help.placement = ui::fill(4);
                help.style.wrap = true;
                list.clip_children = true;

                // Typing here renames the window.
                name_ = &page.add<ui::EditBox>("name", title);
                name_->skin = std::string(skins::kListBox);
                name_->focused_skin = std::string(skins::kListBoxFocused);
                name_->placement = { { 0, 1, 1, 1 }, { 0, -20, 0, 0 } };
                name_->max_length = kNameLength;
            }

            void build_list_page(ui::Widget& page)
            {
                // Rides to pick a window name from: a double click or Enter takes one.
                rides_ = &page.add<ui::ListBox>("rides", skins::list_box_style(), skins::scroll_bar_style(ui::Orientation::Vertical));
                rides_->items.assign(kRideNames.begin(), kRideNames.end());
            }

            void build_controls_page(ui::Widget& page, const ui::StringTable& strings)
            {
                for (std::size_t i = 0; i < kOptionKeys.size(); ++i)
                {
                    const float y = static_cast<float>(i) * kRowHeight;
                    auto& tick = page.add<ui::Tickbox>("option" + std::to_string(i), std::string(skins::kTickbox), std::string(skins::kTick));
                    tick.placement = ui::top_left(0, y + 3, 12, 12);
                    tick.ticked = i == 0;
                    tick.tooltip = strings.get(kOptionTips[i]);
                    auto& label = page.add<ui::Label>("label" + std::to_string(i), std::u16string(strings.get(kOptionKeys[i])), ui::FontRole::Small);
                    label.placement = { { 0, 0, 1, 0 }, { 18, y, -30, y + kRowHeight } };
                    label.style.vertical = ui::Align::Centre;
                }
                auto& toggle = page.add<ui::Button>("toggle", std::string(kToggleFamily));
                toggle.toggle = true;
                toggle.placement = ui::top_right(0, 0, 20, 20);

                // A slider and a spinner, each showing its value beside it.
                const float y = static_cast<float>(kOptionKeys.size()) * kRowHeight + 8;
                volume_ = &page.add<ui::Slider>("volume", skins::slider_style());
                volume_->maximum = 100;
                volume_->set_value(70);
                volume_->placement = { { 0, 0, 1, 0 }, { 0, y, -50, y + kRowHeight } };
                volume_label_ = &page.add<ui::Label>("volume_value", u"", ui::FontRole::Small);
                volume_label_->placement = { { 1, 0, 1, 0 }, { -44, y, 0, y + kRowHeight } };
                volume_label_->style.vertical = ui::Align::Centre;

                guests_ = &page.add<ui::Spinner>("guests", skins::spinner_style());
                guests_->maximum = 9999;
                guests_->step = 50;
                guests_->set_value(500);
                guests_->placement = ui::top_left(0, y + kRowHeight + 8, 90, 16);
                guests_label_ = &page.add<ui::Label>("guests_label", u"guests", ui::FontRole::Small);
                guests_label_->placement = ui::top_left(96, y + kRowHeight + 8, 120, 16);
                guests_label_->style.vertical = ui::Align::Centre;

                auto& detail = page.add<ui::Dropdown>("detail", skins::dropdown_style());
                detail.items.assign(kDetailLevels.begin(), kDetailLevels.end());
                detail.select(1);
                detail.placement = ui::top_left(0, y + 2 * (kRowHeight + 8), 120, 18);
            }

            void update(const ui::UpdateContext& context) override
            {
                ui::Window::update(context);
                volume_label_->text = strings::to_utf16(std::to_string(static_cast<int>(volume_->value())) + "%");
                if (guests_->take_change())
                    guests_label_->text = guests_->value() == 1 ? u"guest" : u"guests";
                if (name_->take_change())
                    title = name_->text;
                if (rides_->take_activate() && rides_->selected())
                {
                    name_->text = rides_->items[*rides_->selected()];
                    name_->select_all();
                    title = name_->text;
                }
                if (cancel_->take_click() || text_cancel_->take_click())
                    close();
                const bool open = confirm_->take_click() || ok_->take_click();
                if (open && parent() != nullptr)
                {
                    const auto n = static_cast<float>(parent()->children().size());
                    parent()->add<DemoWindow>("window" + std::to_string(parent()->children().size()), n * kCascade, strings_);
                }
            }

        private:
            const ui::StringTable& strings_;
            ui::Button* confirm_ = nullptr;
            ui::Button* cancel_ = nullptr;
            ui::Button* ok_ = nullptr;
            ui::Button* text_cancel_ = nullptr;
            ui::EditBox* name_ = nullptr;
            ui::ListBox* rides_ = nullptr;
            ui::Slider* volume_ = nullptr;
            ui::Label* volume_label_ = nullptr;
            ui::Spinner* guests_ = nullptr;
            ui::Label* guests_label_ = nullptr;
        };

        bool is_demo_window(const ui::Widget& w)
        {
            return dynamic_cast<const DemoWindow*>(&w) != nullptr;
        }

        void open_window(WidgetDemo& demo)
        {
            ui::Widget& root = demo.desktop.root();
            const auto n = std::ranges::count_if(root.children(), [](const auto& c) { return is_demo_window(*c); });
            root.add<DemoWindow>("window" + std::to_string(root.children().size()), static_cast<float>(n) * kCascade, *demo.assets.strings);
        }

        std::vector<ui::MenuItem> scale_items(std::optional<float> chosen)
        {
            std::vector<ui::MenuItem> items;
            items.push_back({ .text = u"Automatic", .checked = !chosen });
            for (int s = 1; s <= kScaleChoices; ++s)
                items.push_back({ .text = strings::to_utf16(std::to_string(s) + "x"), .checked = chosen == static_cast<float>(s) });
            return items;
        }

        void add_bars(WidgetDemo& demo)
        {
            ui::Widget& root = demo.desktop.root();
            demo.menu_bar = &root.add<ui::MenuBar>("menu_bar", skins::menu_bar_style());
            demo.menu_bar->placement = { { 0, 0, 1, 0 }, { 0, 0, 0, kMenuBarHeight } };
            std::vector<ui::MenuItem> window_items(4);
            window_items[kNewWindowItem].text = u"New window";
            window_items[kCloseWindowItem].text = u"Close window";
            window_items[2].separator = true;
            window_items[kCloseAllItem].text = u"Close all windows...";
            demo.menu_bar->menus = { { u"Window", std::move(window_items) }, { u"Scale", scale_items(demo.scale) } };

            demo.toolbar = &root.add<ui::Toolbar>("toolbar", skins::toolbar_style(), ui::Orientation::Vertical);
            for (std::size_t i = 0; i < kToolIcons.size(); ++i)
                demo.toolbar->add_button("tool" + std::to_string(i), std::string(kToolIcons[i]), std::u16string(kToolTips[i]));
            demo.toolbar->placement = ui::top_left(0, kMenuBarHeight + 4, 38, demo.toolbar->length());
        }

        void follow_bars(WidgetDemo& demo)
        {
            if (const auto choice = demo.menu_bar->take_choice())
            {
                if (choice->menu == kWindowMenu && choice->item == kNewWindowItem)
                    open_window(demo);
                if (choice->menu == kWindowMenu && choice->item == kCloseWindowItem)
                {
                    const auto& tops = demo.desktop.root().children();
                    const auto top = std::ranges::find_if(tops.rbegin(), tops.rend(), [](const auto& c) { return is_demo_window(*c); });
                    if (top != tops.rend())
                        (*top)->close();
                }
                if (choice->menu == kWindowMenu && choice->item == kCloseAllItem && demo.question == nullptr)
                    demo.question = &demo.desktop.root().add<ui::QuestionBox>("question", skins::question_box_style(), u"Close every window? A new one opens in their place.", std::vector<ui::QuestionAnswer> { { u"Yes", std::string(skins::kConfirmIcon) }, { u"No", std::string(skins::kCancelIcon) } });
                if (choice->menu == kScaleMenu)
                {
                    demo.scale = choice->item == kAutoScaleItem ? std::nullopt : std::optional(static_cast<float>(choice->item));
                    demo.menu_bar->menus[kScaleMenu].items = scale_items(demo.scale);
                }
            }
            if (demo.question != nullptr)
            {
                if (const auto answer = demo.question->take_answer())
                {
                    if (*answer == kCloseAllYes)
                    {
                        for (const auto& c : demo.desktop.root().children())
                        {
                            if (is_demo_window(*c))
                                c->close();
                        }
                    }
                    demo.question = nullptr; // It closed with the answer
                }
            }
            if (demo.toolbar->children().front()->visible && static_cast<ui::Button&>(*demo.toolbar->children().front()).take_click())
                open_window(demo);
        }

        float scale_for(const render::Device& device, const WidgetDemo& demo)
        {
            return ui::interface_scale(static_cast<float>(device.extent().height), demo.scale);
        }

    } // namespace

    std::expected<WidgetDemo, std::string> create_widget_demo(render::Device& device, const config::GameInstall& install, const std::string& language, const std::filesystem::path& fallback_font)
    {
        auto assets = ui::load_interface_assets(device, install, language, fallback_font);
        if (!assets)
            return std::unexpected(assets.error());
        WidgetDemo demo;
        demo.assets = std::move(*assets);
        demo.desktop.tooltip_style = skins::tooltip_style();
        add_bars(demo);
        open_window(demo);
        return demo;
    }

    bool update_widget_demo(render::Device& device, WidgetDemo& demo, const ui::PointerState& pointer_pixels, const ui::KeyboardState& keyboard, float elapsed)
    {
        const float scale = scale_for(device, demo);
        const render::Extent extent = device.extent();
        demo.desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Something to show again once every window is closed.
        if (std::ranges::none_of(demo.desktop.root().children(), [](const auto& c) { return is_demo_window(*c); }))
            open_window(demo);

        ui::PointerState pointer = pointer_pixels;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        demo.current_fonts = &ui::fonts_for(device, demo.assets, demo.desktop.root().rect().width());
        const bool over = demo.desktop.update(pointer, keyboard, { elapsed, demo.current_fonts });
        follow_bars(demo);
        return over;
    }

    void draw_widget_demo(render::Device& device, WidgetDemo& demo)
    {
        demo.list.clear();
        demo.list.set_scale(scale_for(device, demo));
        static const ui::FontSet kNoFonts;
        ui::DrawContext context { demo.list, demo.assets.skins, demo.current_fonts != nullptr ? *demo.current_fonts : kNoFonts };
        demo.desktop.draw(context);
        ui::render_draw_list(device, demo.assets.renderer, demo.list);
    }

    void destroy_widget_demo(render::Device& device, WidgetDemo& demo)
    {
        ui::destroy_interface_assets(device, demo.assets);
        demo = { };
    }

} // namespace rct3::scene
