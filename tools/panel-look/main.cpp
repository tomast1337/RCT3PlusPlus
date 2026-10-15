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
#include <functional>
#include <map>
#include <optional>
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

    // The round panel in a corner, with a page of lines so the strip shows.
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

    std::vector<PanelCase> panel_cases()
    {
        return {
            { "info-panel", 520, 175, draw_info_panel },
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
        ui::DrawList list;
        list.set_scale(scale);
        ui::DrawContext context { list, assets.skins, assets.fonts };
        panel.draw(context, look, { 0, 0, panel.width, panel.height });
        auto picture = ui::blank_picture(static_cast<std::uint32_t>(std::lround(panel.width * scale)), static_cast<std::uint32_t>(std::lround(panel.height * scale)), kBackground);
        ui::paint_draw_list(list, assets.textures, picture);
        return picture;
    }

    std::expected<Assets, std::string> load_assets(const config::GameInstall& install)
    {
        const auto path = install.find(ui::game_skins::kMainOverlay);
        if (!path)
            return std::unexpected(std::string(ui::game_skins::kMainOverlay) + " not found");
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
            assets.skins.add(name, { skin.layout, id->second, static_cast<float>(image.width), static_cast<float>(image.height) });
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
        if (std::ranges::find(cases, name, &PanelCase::name) == cases.end())
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
        if (!wanted.empty() && std::ranges::find(wanted, panel.name) == wanted.end())
            continue;
        const ovl::RgbaImage texture = render(panel, PanelLook::Texture, *assets, scale);
        const ovl::RgbaImage coded = render(panel, PanelLook::Coded, *assets, scale);
        ovl::RgbaImage diff;
        const Score score = compare(texture, coded, diff);
        const std::string stem = std::string(panel.name);
        for (const auto& [suffix, picture] : { std::pair { "texture", &texture }, { "coded", &coded }, { "diff", &diff } })
        {
            const auto path = out / (stem + "." + suffix + ".png");
            if (auto saved = image::save_png(path, *picture); !saved)
                std::fprintf(stderr, "%s\n", saved.error().c_str());
        }
        const bool over = score.mean > threshold;
        flagged = flagged || over;
        std::printf("%-14s score %6.2f  far %5.1f%%  over %zu px%s\n", stem.c_str(), score.mean, score.far * 100, score.pixels, over ? "  FLAGGED: keep it on the texture" : "");
    }
    std::printf("pictures in %s (game art: do not commit)\n", out.string().c_str());
    return flagged ? 1 : 0;
}
