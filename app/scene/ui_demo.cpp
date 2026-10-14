// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/ui_demo.hpp"

#include "ui/skin.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace rct3::scene
{

    namespace
    {

        // Generated skin texture: a 16 x 16 window frame at the top-left, a 16 x 8 button below it.
        constexpr std::uint32_t kSkinSize = 32;
        using SkinPixels = std::array<std::byte, render::texture_bytes(kSkinSize, kSkinSize, render::TextureFormat::Rgba8)>;

        void put(SkinPixels& pixels, std::uint32_t x, std::uint32_t y, ui::Color c)
        {
            std::byte* p = &pixels[(y * kSkinSize + x) * 4];
            p[0] = std::byte { c.r };
            p[1] = std::byte { c.g };
            p[2] = std::byte { c.b };
            p[3] = std::byte { c.a };
        }

        SkinPixels make_skins()
        {
            SkinPixels pixels { };
            for (std::uint32_t y = 0; y < 16; ++y)
            {
                for (std::uint32_t x = 0; x < 16; ++x)
                {
                    const std::uint32_t edge = std::min({ x, y, 15 - x, 15 - y });
                    const ui::Color c = edge == 0 ? ui::hex("#14181e") : edge == 1 ? ui::hex("#96aac8") : edge < 4 ? ui::hex("#465a78") : ui::hex("#28303cd7");
                    put(pixels, x, y, c);
                }
            }
            for (std::uint32_t y = 16; y < 24; ++y)
            {
                for (std::uint32_t x = 0; x < 16; ++x)
                {
                    const bool rim = x == 0 || x == 15 || y == 16 || y == 23;
                    const bool cap = x < 3 || x > 12;
                    const auto shade = static_cast<std::uint8_t>(255 - (y - 16) * 12);
                    put(pixels, x, y, rim ? ui::hex("#1e1e1e") : cap ? ui::Color { shade, shade, shade, 255 } : ui::Color { shade, shade, 220, 255 });
                }
            }
            return pixels;
        }

        const ovl::GuiSkin kWindowSkin { static_cast<std::uint32_t>(ovl::GuiSkinKind::NinePatch), "demo", { 0, 4, 12, 16, 0, 4, 12, 16 } };
        const ovl::GuiSkin kButtonSkin { static_cast<std::uint32_t>(ovl::GuiSkinKind::HorizontalThree), "demo", { 0, 3, 13, 16, 16, 24 } };

    } // namespace

    std::expected<UiDemo, std::string> create_ui_demo(render::Device& device)
    {
        UiDemo demo;
        auto renderer = ui::create_ui_renderer(device);
        if (!renderer)
            return std::unexpected(renderer.error());
        demo.renderer = *renderer;

        const SkinPixels pixels = make_skins();
        auto skins = device.create_texture({ kSkinSize, kSkinSize, render::TextureFormat::Rgba8, pixels, "demo skins" });
        if (!skins)
        {
            destroy_ui_demo(device, demo);
            return std::unexpected(skins.error());
        }
        demo.skins = *skins;
        return demo;
    }

    void draw_ui_demo(render::Device& device, UiDemo& demo, double seconds)
    {
        static const ui::SkinLayout window_skin = *ui::skin_layout(kWindowSkin);
        static const ui::SkinLayout button_skin = *ui::skin_layout(kButtonSkin);
        const ui::TextureId skins = ui::texture_id(demo.skins);
        const auto t = static_cast<float>(seconds);
        constexpr auto kSize = static_cast<float>(kSkinSize);

        ui::DrawList& list = demo.list;
        list.clear();

        const render::Extent extent = device.extent();
        const float width = 320.0f + 60.0f * std::sin(t * 0.7f);
        const ui::Rect window { 24, static_cast<float>(extent.height) - 300.0f, 24 + width, static_cast<float>(extent.height) - 24.0f };
        ui::draw_skin(list, window_skin, window, skins, kSize, kSize);

        const ui::Rect title { window.x0 + 4, window.y0 + 4, window.x1 - 4, window.y0 + 26 };
        list.gradient(title, { { 60, 110, 190, 255 }, { 30, 60, 120, 255 }, { 20, 40, 90, 255 }, { 50, 90, 160, 255 } });

        // A list taller than its area, scrolling, cut to the area by the clip rectangle.
        const ui::Rect area { window.x0 + 10, title.y1 + 8, window.x1 - 10, window.y1 - 50 };
        if (list.push_clip(area))
        {
            const float scroll = std::fmod(t * 20.0f, 160.0f);
            for (int row = 0; row < 12; ++row)
            {
                const float y = area.y0 + static_cast<float>(row) * 20.0f - scroll;
                const auto shade = static_cast<std::uint8_t>(row % 2 == 0 ? 80 : 60);
                list.fill({ area.x0, y, area.x1, y + 18 }, { shade, shade, static_cast<std::uint8_t>(shade + 30), 255 });
                list.fill({ area.x0 + 4, y + 4, area.x0 + 14, y + 14 }, { static_cast<std::uint8_t>(row * 20), 200, 120, 255 });
            }
        }
        list.pop_clip();

        // Buttons: one skin, the state shown by the tint, as the original changes looks without new pixels.
        constexpr std::array<ui::Color, 3> kTints = { ui::hex("#ffffff"), ui::hex("#ffe696"), ui::hex("#aaaaaa") };
        const float button_width = (area.width() - 16) / 3;
        for (std::size_t i = 0; i < kTints.size(); ++i)
        {
            const float x = area.x0 + static_cast<float>(i) * (button_width + 8);
            ui::draw_skin(list, button_skin, { x, window.y1 - 40, x + button_width, window.y1 - 14 }, skins, kSize, kSize, kTints[i]);
        }

        ui::render_draw_list(device, demo.renderer, list);
    }

    void destroy_ui_demo(render::Device& device, UiDemo& demo)
    {
        device.destroy_texture(demo.skins);
        ui::destroy_ui_renderer(device, demo.renderer);
        demo = { };
    }

} // namespace rct3::scene
