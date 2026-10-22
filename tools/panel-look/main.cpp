// SPDX-License-Identifier: GPL-3.0-or-later
//
// panel-look: draws each panel whose frame can be drawn in code both ways, from the game's skins and as coded
// shapes, and measures how far apart the two look. A check for moving panels off the game's pictures: a coded
// frame should read as the same panel before its style switches to it.
//
//   panel-look [--out DIR] [--scale S] [--threshold T] [--game PATH] [PANEL...]
//
// The skins come from the installed game (RCT3PP_GAME_PATH, or --game). Without PANEL names every panel is
// drawn; --list prints their names. For each panel, DIR (default build/panel-look, which git ignores) gets
// PANEL.texture.png, PANEL.coded.png and PANEL.diff.png, the difference brightened to be seen. Both are
// drawn on the CPU (ui/draw_list_painter.hpp) over a plain background, at S pixels per unit (default 1, the
// size the game draws them).
//
// The score is the mean difference of red, green and blue, 0 to 255, over the pixels either look colours.
// The tool also prints the share of those pixels that differ by more than a quarter. A panel scoring above
// T (default 12) is flagged, and the tool then exits with 1. The pictures hold game art: keep them out of git.
#include "config/game_install.hpp"
#include "image/png_encoder.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list_painter.hpp"
#include "ui/game_style.hpp"
#include "ui/skin_set.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

    using namespace rct3;
    using ui::PanelLook;

    // Behind both looks: a muted green-brown, near the park's ground the panels usually sit on.
    constexpr ui::Color kBackground = ui::hex("#6b6a4a");

    struct Assets
    {
        ui::SkinLibrary skins;
        ui::PainterTextures textures;
        ui::FontSet fonts; // Empty: frames draw no text
    };

    // A panel to compare: the size of the picture in units, and how to draw its frame in a look.
    struct PanelCase
    {
        std::string_view name;
        float width;
        float height;
        std::function<void(ui::DrawContext&, PanelLook, ui::Rect view)> draw;
    };

    // The round panel in a corner, with a page of lines so the strip shows. Its style picks the look.
    void draw_info_panel(ui::DrawContext& context, PanelLook look, ui::Rect view)
    {
        ui::InfoPanelStyle style = ui::game_skins::info_panel_style();
        style.look = look;
        ui::Desktop desktop;
        auto& panel = desktop.root().add<ui::InfoPanel>("info", style);
        panel.show({ }, 0);
        panel.set_body_size(ui::info_panel::kPageWidth, 60);
        desktop.layout(view);
        // The frame alone: tabs, the close button and the page are pictures and text either way.
        panel.draw(context);
    }

    // The other panels take their look from the skins: the coded look gives the library their frames.
    void draw_skin_at(ui::DrawContext& context, std::string_view skin, std::initializer_list<ui::Rect> rects)
    {
        for (const ui::Rect& r : rects)
            ui::draw_skin(context.list, context.skins.find(skin), r);
    }

    void draw_lozenges(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // A text button, a tab, a small square button and a tooltip of two lines.
        draw_skin_at(context, ui::game_skins::kTextButton, { { 8, 8, 128, 34 }, { 140, 8, 200, 32 }, { 212, 8, 242, 38 }, { 8, 48, 248, 96 } });
    }

    void draw_menus(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        draw_skin_at(context, ui::game_skins::kMenuBackground, { { 8, 8, 168, 138 }, { 180, 8, 290, 70 } });
    }

    void draw_selected_rows(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // As tall as a list's row, and a menu bar's title.
        draw_skin_at(context, ui::game_skins::kSelectedRow, { { 8, 8, 168, 26 }, { 8, 34, 68, 52 }, { 80, 34, 190, 58 } });
    }

    void draw_button_faces(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // At the size of the picture, of the tool row's items, swollen under the pointer, and small.
        draw_skin_at(context, ui::game_skins::kMenuButtonSmall, { { 8, 8, 40, 41 }, { 50, 8, 82, 40 }, { 92, 8, 128, 44 }, { 138, 8, 162, 32 } });
    }

    void draw_controls(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // Each at its picture's size; the boxes, rails and riders also stretched as their widgets stretch them.
        using namespace ui::game_skins;
        draw_skin_at(context, kStandardBorder, { { 8, 8, 88, 40 } });
        draw_skin_at(context, kListBoxFocused, { { 96, 8, 176, 40 } });
        draw_skin_at(context, kButton, { { 184, 8, 214, 38 } });
        draw_skin_at(context, kTickbox, { { 222, 8, 234, 20 } });
        draw_skin_at(context, kVerticalRail, { { 8, 48, 23, 128 } });
        draw_skin_at(context, "VScrollBar_RiderUp", { { 8, 70, 23, 100 } });
        draw_skin_at(context, kScrollUp, { { 8, 48, 23, 63 } });
        draw_skin_at(context, kScrollDown, { { 8, 113, 23, 128 } });
        draw_skin_at(context, kHorizontalRail, { { 32, 48, 132, 63 } });
        draw_skin_at(context, "HScrollBar_RiderUp", { { 60, 48, 100, 63 } });
        draw_skin_at(context, kScrollLeft, { { 32, 48, 47, 63 } });
        draw_skin_at(context, kScrollRight, { { 117, 48, 132, 63 } });
        float x = 32;
        for (const std::string_view name : { "SquareArrowUp", "SquareArrowDown", "SquareArrowLeft", "SquareArrowRight", "SquareArrowUpDisabled", "SquareArrowDownDisabled" })
        {
            draw_skin_at(context, name, { { x, 72, x + 17, 89 } });
            x += 22;
        }
        draw_skin_at(context, kDropdownOpener, { { 32, 96, 49, 113 } });
        draw_skin_at(context, "ListBoxOpenerDisabled", { { 54, 96, 69, 111 } });
        draw_skin_at(context, kSpinnerUp, { { 76, 96, 96, 104 } });
        draw_skin_at(context, kSpinnerDown, { { 76, 104, 96, 112 } });
        draw_skin_at(context, "SpinnerUpDisabled", { { 102, 96, 122, 104 } });
        draw_skin_at(context, "SpinnerDownDisabled", { { 102, 104, 122, 112 } });
    }

    // Each at its picture's size in a row: the slider's thumb, a list heading's boxes, the colour picker's
    // rings and back.
    void draw_small_parts(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        float x = 8;
        float y = 8;
        float row = 0;
        for (const std::string_view name : { ui::game_skins::kSliderThumb.data(), ui::game_skins::kOpenHeading.data(), ui::game_skins::kClosedHeading.data(), "FlexiColourHighlightBlue", "FlexiColourHighlightWhite", "FlexiColourSurround" })
        {
            const auto skin = context.skins.find(name);
            if (!skin)
            {
                std::println(stderr, "small-parts: no skin {}", name);
                continue;
            }
            const auto [w, h] = skin->size();
            if (x + w > 232)
            {
                x = 8;
                y += row + 8;
                row = 0;
            }
            ui::draw_skin(context.list, skin, { x, y, x + w, y + h });
            x += w + 8;
            row = std::max(row, h);
        }
    }

    // The window's title button in its three states, with the close icon on it as windows draw it: its
    // pictures carry an arrow under the icon, which the coded disc leaves out.
    void draw_title_buttons(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        float x = 8;
        for (const std::string_view name : { "GUI2Dialog_TitleButtonUp", "GUI2Dialog_TitleButtonUpOver", "GUI2Dialog_TitleButtonPressed" })
        {
            draw_skin_at(context, name, { { x, 8, x + 14, 22 } });
            draw_skin_at(context, ui::game_skins::kCloseIcon, { { x, 8, x + 14, 22 } });
            x += 22;
        }
    }

    void draw_park_bar(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // A short bar: the left part, the divider with its notch, the right part.
        constexpr float kTop = 8;
        constexpr float kBottom = kTop + ui::park_bar::kHeight;
        draw_skin_at(context, "Top_WindowFrame_Left", { { 8, kTop, 128, kBottom } });
        draw_skin_at(context, "Top_WindowFrame_Middle", { { 128, kTop, 128 + ui::park_bar::kDividerWidth, kBottom } });
        draw_skin_at(context, "Top_WindowFrame_Right", { { 128 + ui::park_bar::kDividerWidth, kTop, 260, kBottom } });
    }

    void draw_menu_circles(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        draw_skin_at(context, ui::game_skins::kMenuCircle, { { 8, 8, 86, 88 }, { 100, 8, 160, 68 }, { 170, 8, 210, 48 } });
    }

    void draw_tutorial_disc(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // The four quarters at half their size, put together into the disc.
        constexpr float kW = 162;
        constexpr float kH = 161;
        const auto& q = ui::game_skins::kQuarterCircles;
        draw_skin_at(context, q[0], { { 8, 8, 8 + kW, 8 + kH } });
        draw_skin_at(context, q[1], { { 8 + kW, 8, 8 + 2 * kW, 8 + kH } });
        draw_skin_at(context, q[2], { { 8, 8 + kH, 8 + kW, 8 + 2 * kH } });
        draw_skin_at(context, q[3], { { 8 + kW, 8 + kH, 8 + 2 * kW, 8 + 2 * kH } });
    }

    void draw_message_drawer(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        // Pulled down far enough to show the back repeated three times.
        ui::draw_drawer_frame(context, ui::game_skins::message_drawer_style(), { 8, 8, 8 + ui::message_drawer::kWidth, 155 });
    }

    void draw_window(ui::DrawContext& context, const ui::WindowStyle& style, ui::Rect view)
    {
        ui::Desktop desktop;
        auto& window = desktop.root().add<ui::Window>("window", style, view.width() - 16, view.height() - 16);
        window.placement = ui::top_left(8, 8, view.width() - 16, view.height() - 16);
        desktop.layout(view);
        // The frame and backgrounds alone: the title button and content are pictures and text either way.
        window.draw(context);
    }

    void draw_mini_window(ui::DrawContext& context, PanelLook, ui::Rect view)
    {
        draw_window(context, ui::game_skins::mini_window_style(), view);
    }

    void draw_main_window(ui::DrawContext& context, PanelLook, ui::Rect view)
    {
        draw_window(context, ui::game_skins::window_style(), view);
    }

    // The window's body alone, without its frame, so its fine lines are not lost in the frame's score.
    void draw_window_background(ui::DrawContext& context, PanelLook, ui::Rect view)
    {
        ui::WindowStyle style = ui::game_skins::window_style();
        style.frame.clear();
        style.frame_unfocused.clear();
        draw_window(context, style, view);
    }

    // The front screen's backdrop at its design size, from the three pictures it is drawn with.
    void draw_front_backdrop(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        namespace fs = ui::front_screen;
        draw_skin_at(context, fs::kBandTopSkin, { fs::kBandTop });
        draw_skin_at(context, fs::kBandBottomSkin, { fs::kBandBottom });
        draw_skin_at(context, fs::kStripRightSkin, { fs::kStripRight });
    }

    // A label's band as the menu draws it: the half disc behind the button, then the pointed end stretched to
    // a few shapes, and at its own size.
    void draw_label_band(ui::DrawContext& context, PanelLook, ui::Rect)
    {
        namespace fs = ui::front_screen;
        draw_skin_at(context, fs::kPatchSkin, { { 8, 8, 103, 105 } });
        draw_skin_at(context, fs::kBandEndSkin, { { 112, 8, 207, 93 }, { 216, 8, 266, 58 }, { 276, 8, 316, 98 }, { 112, 100, 232, 130 } });
    }

    std::vector<PanelCase> panel_cases()
    {
        return {
            { "info-panel", 520, 175, draw_info_panel },
            { "lozenge", 256, 104, draw_lozenges },
            { "menu", 298, 146, draw_menus },
            { "selected-row", 198, 66, draw_selected_rows },
            { "button-face", 170, 52, draw_button_faces },
            { "menu-circle", 218, 96, draw_menu_circles },
            { "tutorial-disc", 340, 338, draw_tutorial_disc },
            { "message-drawer", 490, 163, draw_message_drawer },
            { "mini-window", 256, 136, draw_mini_window },
            { "window", 336, 236, draw_main_window },
            { "window-background", 200, 140, draw_window_background },
            { "front-backdrop", 1024, 768, draw_front_backdrop },
            { "label-band", 324, 138, draw_label_band },
            { "controls", 240, 136, draw_controls },
            { "park-bar", 268, 44, draw_park_bar },
            { "small-parts", 240, 56, draw_small_parts },
            { "title-button", 74, 30, draw_title_buttons },
        };
    }

    struct Score
    {
        double mean = 0; // 0..255
        double far = 0;  // Share of pixels off by more than a quarter
        std::size_t pixels = 0;
    };

    bool is_background(const std::uint8_t* p)
    {
        return p[0] == kBackground.r && p[1] == kBackground.g && p[2] == kBackground.b;
    }

    Score compare(const ovl::RgbaImage& a, const ovl::RgbaImage& b, ovl::RgbaImage& diff)
    {
        Score score;
        double total = 0;
        std::size_t far = 0;
        diff = ui::blank_picture(a.width, a.height, ui::hex("#000000"));
        for (std::size_t i = 0; i < a.pixels.size(); i += 4)
        {
            const std::uint8_t* pa = &a.pixels[i];
            const std::uint8_t* pb = &b.pixels[i];
            if (is_background(pa) && is_background(pb))
                continue;
            int sum = 0;
            for (std::size_t c = 0; c < 3; ++c)
                sum += std::abs(int { pa[c] } - int { pb[c] });
            const double mean = sum / 3.0;
            total += mean;
            ++score.pixels;
            if (mean > 64)
                ++far;
            // Brightened four times, red where the coded look is lighter, blue where darker, so the eye finds it.
            const int signed_sum = (int { pb[0] } + pb[1] + pb[2]) - (int { pa[0] } + pa[1] + pa[2]);
            const auto level = static_cast<std::uint8_t>(std::min(255.0, mean * 4));
            diff.pixels[i] = signed_sum > 0 ? level : level / 3;
            diff.pixels[i + 1] = level / 3;
            diff.pixels[i + 2] = signed_sum > 0 ? level / 3 : level;
        }
        if (score.pixels > 0)
        {
            score.mean = total / static_cast<double>(score.pixels);
            score.far = static_cast<double>(far) / static_cast<double>(score.pixels);
        }
        return score;
    }

    ovl::RgbaImage render(const PanelCase& panel, PanelLook look, const Assets& assets, float scale)
    {
        // The coded look draws this panel's skins in code, whatever the game's look has them on.
        ui::SkinLibrary skins = assets.skins;
        if (look == PanelLook::Coded)
        {
            for (ui::game_skins::CodedSkin& coded : ui::game_skins::coded_skins())
            {
                if (coded.panel == panel.name)
                    skins.set_coded(coded.skin, std::move(coded.frame));
            }
        }
        ui::DrawList list;
        list.set_scale(scale);
        ui::DrawContext context { list, skins, assets.fonts };
        panel.draw(context, look, { 0, 0, panel.width, panel.height });
        auto picture = ui::blank_picture(static_cast<std::uint32_t>(std::lround(panel.width * scale)), static_cast<std::uint32_t>(std::lround(panel.height * scale)), kBackground);
        ui::paint_draw_list(list, assets.textures, picture);
        return picture;
    }

    std::expected<Assets, std::string> load_assets(const config::GameInstall& install)
    {
        const auto path = install.find(ui::game_skins::kMainOverlay);
        if (!path)
            return std::unexpected(std::format("{} not found", ui::game_skins::kMainOverlay));
        auto set = ui::load_skin_set(*path, ui::game_skins::kWholeTextures);
        if (!set)
            return std::unexpected(set.error());
        Assets assets;
        std::map<std::string, ui::TextureId, std::less<>> ids;
        for (auto& [name, image] : set->images)
        {
            const auto id = static_cast<ui::TextureId>(assets.textures.size() + 1);
            ids.emplace(name, id);
            assets.textures.emplace(id, std::move(image));
        }
        for (const auto& [name, skin] : set->skins)
        {
            const auto id = ids.find(skin.texture);
            if (id == ids.end())
                continue;
            const ovl::RgbaImage& image = assets.textures.at(id->second);
            assets.skins.add(name, { skin.layout, id->second, static_cast<float>(image.width), static_cast<float>(image.height), nullptr });
        }
        return assets;
    }

    bool parse_float(std::string_view text, float& out)
    {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
        return error == std::errc { } && end == text.data() + text.size() && out > 0;
    }

    int usage(const char* argv0)
    {
        std::fprintf(stderr, "usage: %s [--out DIR] [--scale S] [--threshold T] [--game PATH] [--list] [PANEL...]\n", argv0);
        return 2;
    }

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path out = "build/panel-look";
    float scale = 1;
    float threshold = 12;
    std::optional<std::filesystem::path> game;
    std::vector<std::string_view> wanted;
    const std::vector<PanelCase> cases = panel_cases();

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--out" && has_value)
            out = argv[++i];
        else if (arg == "--scale" && has_value)
        {
            if (!parse_float(argv[++i], scale))
                return usage(argv[0]);
        }
        else if (arg == "--threshold" && has_value)
        {
            if (!parse_float(argv[++i], threshold))
                return usage(argv[0]);
        }
        else if (arg == "--game" && has_value)
            game = argv[++i];
        else if (arg == "--list")
        {
            for (const PanelCase& c : cases)
                std::printf("%.*s\n", static_cast<int>(c.name.size()), c.name.data());
            return 0;
        }
        else if (arg.starts_with("-"))
            return usage(argv[0]);
        else
            wanted.push_back(arg);
    }
    for (const std::string_view name : wanted)
    {
        if (!std::ranges::contains(cases, name, &PanelCase::name))
        {
            std::fprintf(stderr, "no panel named %.*s; --list shows them\n", static_cast<int>(name.size()), name.data());
            return 2;
        }
    }

    const auto install = game ? config::open_game_install(*game) : config::find_game_install(config::Environment::from_process());
    if (!install)
    {
        std::fprintf(stderr, "%s\n", config::describe(install.error()).c_str());
        return 2;
    }
    const auto assets = load_assets(*install);
    if (!assets)
    {
        std::fprintf(stderr, "skins: %s\n", assets.error().c_str());
        return 2;
    }
    std::error_code error;
    std::filesystem::create_directories(out, error);
    if (error)
    {
        std::fprintf(stderr, "%s: %s\n", out.string().c_str(), error.message().c_str());
        return 2;
    }

    bool flagged = false;
    for (const PanelCase& panel : cases)
    {
        if (!wanted.empty() && !std::ranges::contains(wanted, panel.name))
            continue;
        const ovl::RgbaImage texture = render(panel, PanelLook::Texture, *assets, scale);
        const ovl::RgbaImage coded = render(panel, PanelLook::Coded, *assets, scale);
        ovl::RgbaImage diff;
        const Score score = compare(texture, coded, diff);
        const std::string stem = std::string(panel.name);
        for (const auto& [suffix, picture] : { std::pair { "texture", &texture }, { "coded", &coded }, { "diff", &diff } })
        {
            const auto path = out / std::format("{}.{}.png", stem, suffix);
            if (auto saved = image::save_png(path, *picture); !saved)
                std::fprintf(stderr, "%s\n", saved.error().c_str());
        }
        const bool over = score.mean > threshold;
        flagged = flagged || over;
        std::printf("%-15s score %6.2f  far %5.1f%%  over %zu px%s\n", stem.c_str(), score.mean, score.far * 100, score.pixels, over ? "  FLAGGED: keep it on the texture" : "");
    }
    std::printf("pictures in %s (game art: do not commit)\n", out.string().c_str());
    return flagged ? 1 : 0;
}
