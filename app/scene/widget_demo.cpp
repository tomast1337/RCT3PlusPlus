// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/widget_demo.hpp"

#include "logging/log.hpp"
#include "ui/fallback_font.hpp"
#include "ui/font_images.hpp"
#include "ui/game_style.hpp"
#include "ui/scale.hpp"
#include "ui/skin_set.hpp"
#include "ui/truetype_font.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/list_box.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/scroll_area.hpp"
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

                // The text area stretches with the window; the rows below it stay on the bottom edge.
                auto& list = body.add<ui::Panel>("list", std::string(skins::kListBox));
                list.placement = { { 0, 0, 0.6f, 1 }, { 0, 0, -3, -130 } };
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

                for (std::size_t i = 0; i < kOptionKeys.size(); ++i)
                {
                    const float bottom = 80 - static_cast<float>(i) * 18;
                    auto& tick = body.add<ui::Tickbox>("option" + std::to_string(i), std::string(skins::kTickbox), std::string(skins::kTick));
                    tick.placement = ui::bottom_left(0, bottom, 12, 12);
                    tick.ticked = i == 0;
                    tick.tooltip = strings.get(kOptionTips[i]);
                    auto& label = body.add<ui::Label>("label" + std::to_string(i), std::u16string(strings.get(kOptionKeys[i])), ui::FontRole::Small);
                    label.placement = { { 0, 1, 1, 1 }, { 18, -bottom - 15, -80, -bottom + 3 } };
                    label.style.vertical = ui::Align::Centre;
                }
                // Beside it, rides to pick a window name from: a double click or Enter takes one.
                rides_ = &body.add<ui::ListBox>("rides", skins::list_box_style(), skins::scroll_bar_style(ui::Orientation::Vertical));
                rides_->placement = { { 0.6f, 0, 1, 1 }, { 3, 0, 0, -130 } };
                rides_->items.assign(kRideNames.begin(), kRideNames.end());

                // Typing here renames the window.
                name_ = &body.add<ui::EditBox>("name", title);
                name_->skin = std::string(skins::kListBox);
                name_->focused_skin = std::string(skins::kListBoxFocused);
                name_->placement = { { 0, 1, 1, 1 }, { 0, -124, 0, -104 } };
                name_->max_length = kNameLength;

                auto& toggle = body.add<ui::Button>("toggle", std::string(kToggleFamily));
                toggle.toggle = true;
                toggle.placement = ui::bottom_right(0, 60, 20, 20);

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
                text_buttons[2]->enabled = false;
            }

            void update(const ui::UpdateContext& context) override
            {
                ui::Window::update(context);
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
        };

        // Renders the fallback at the size that matches `font`, uploads its pages and merges it in. On failure
        // the font is returned as it was.
        std::expected<ui::Font, std::string> with_fallback(render::Device& device, WidgetDemo& demo, const ui::Font& font, const std::string& name, const ui::TrueTypeFile& fallback)
        {
            const float em = ui::fallback_em_pixels(ui::cap_height(font), fallback.cap_per_em, ui::kUnifontGrid);
            auto baked = ui::bake_truetype(fallback, em, ui::kFallbackRanges, { .hard_edges = true });
            if (!baked)
                return std::unexpected(baked.error());
            std::map<std::string, ovl::RgbaImage, std::less<>> images;
            for (std::size_t i = 0; i < baked->pages.size(); ++i)
                images.emplace(name + "#fallback" + std::to_string(i), std::move(baked->pages[i]));
            auto pages = ui::upload_textures(device, images);
            if (!pages)
                return std::unexpected(pages.error());
            std::vector<ui::Font::Page> refs;
            for (std::size_t i = 0; i < images.size(); ++i)
            {
                const ui::UiTexture& t = pages->at(name + "#fallback" + std::to_string(i));
                refs.push_back({ ui::texture_id(t.handle), t.width, t.height });
            }
            demo.textures.merge(*pages);
            return font.with_fallback(ui::font_from_baked(*baked, refs));
        }

        float scale_for(const render::Device& device, const WidgetDemo& demo)
        {
            return ui::interface_scale(static_cast<float>(device.extent().height), demo.scale);
        }

    } // namespace

    std::expected<WidgetDemo, std::string> create_widget_demo(render::Device& device, const config::GameInstall& install, const std::filesystem::path& fallback_font)
    {
        const auto log = logging::logger("ui");
        const auto main_ovl = install.find("Main.common.ovl");
        if (!main_ovl)
            return std::unexpected("Main.common.ovl not found");
        auto set = ui::load_skin_set(*main_ovl);
        if (!set)
            return std::unexpected(set.error());
        auto fonts = ui::load_font_images(*main_ovl);
        if (!fonts)
            return std::unexpected("fonts: " + fonts.error());

        const auto fallback = ui::load_truetype(fallback_font);
        if (!fallback)
            log.warning("fallback font {}: {}; characters the game fonts lack will not show (run ./nob fetch)", fallback_font.string(), fallback.error());

        WidgetDemo demo;
        auto renderer = ui::create_ui_renderer(device);
        if (!renderer)
            return std::unexpected(renderer.error());
        demo.renderer = *renderer;

        auto textures = ui::upload_textures(device, set->images);
        if (!textures)
        {
            destroy_widget_demo(device, demo);
            return std::unexpected(textures.error());
        }
        demo.textures = std::move(*textures);
        demo.skins = ui::skin_library(set->skins, demo.textures);

        // Font pages go up under the font's name and page number; each role gets the font of its name.
        for (const ui::FontImages& f : *fonts)
        {
            const auto role = std::ranges::find(skins::kFonts, std::string_view(f.name), &std::pair<ui::FontRole, std::string_view>::second);
            if (role == skins::kFonts.end())
                continue;
            std::map<std::string, ovl::RgbaImage, std::less<>> images;
            for (std::size_t i = 0; i < f.pages.size(); ++i)
                images.emplace(f.name + "#" + std::to_string(i), f.pages[i]);
            auto pages = ui::upload_textures(device, images);
            if (!pages)
            {
                destroy_widget_demo(device, demo);
                return std::unexpected(pages.error());
            }
            std::vector<ui::Font::Page> refs;
            for (std::size_t i = 0; i < f.pages.size(); ++i)
            {
                const ui::UiTexture& t = pages->at(f.name + "#" + std::to_string(i));
                refs.push_back({ ui::texture_id(t.handle), t.width, t.height });
            }
            demo.textures.merge(*pages);
            ui::Font font = ui::Font::from(f.font, refs);
            if (fallback)
            {
                if (auto merged = with_fallback(device, demo, font, f.name, *fallback))
                    font = std::move(*merged);
                else
                    log.warning("fallback font for {}: {}", f.name, merged.error());
            }
            demo.fonts.set(role->first, std::move(font));
        }

        if (const auto english = install.find("gui/English.common.ovl"))
        {
            if (auto strings = ui::StringTable::load(*english))
                *demo.strings = std::move(*strings);
        }
        demo.desktop.tooltip_style = skins::tooltip_style();
        demo.desktop.root().add<DemoWindow>("window0", 0.0f, *demo.strings);
        return demo;
    }

    bool update_widget_demo(render::Device& device, WidgetDemo& demo, const ui::PointerState& pointer_pixels, const ui::KeyboardState& keyboard, float elapsed)
    {
        const float scale = scale_for(device, demo);
        const render::Extent extent = device.extent();
        demo.desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Something to show again once every window is closed.
        if (demo.desktop.root().children().empty())
            demo.desktop.root().add<DemoWindow>("window0", 0.0f, *demo.strings);

        ui::PointerState pointer = pointer_pixels;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        return demo.desktop.update(pointer, keyboard, { elapsed, &demo.fonts });
    }

    void draw_widget_demo(render::Device& device, WidgetDemo& demo)
    {
        demo.list.clear();
        demo.list.set_scale(scale_for(device, demo));
        ui::DrawContext context { demo.list, demo.skins, demo.fonts };
        demo.desktop.draw(context);
        ui::render_draw_list(device, demo.renderer, demo.list);
    }

    void destroy_widget_demo(render::Device& device, WidgetDemo& demo)
    {
        ui::destroy_textures(device, demo.textures);
        ui::destroy_ui_renderer(device, demo.renderer);
        demo = { };
    }

} // namespace rct3::scene
