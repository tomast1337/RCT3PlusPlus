// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/interface_fonts.hpp"

#include "logging/log.hpp"
#include "ui/fallback_font.hpp"
#include "ui/font/images.hpp"
#include "ui/font/sets.hpp"
#include "ui/ui_renderer.hpp"

#include <format>
#include <vector>

namespace rct3::ui
{

    namespace
    {

        // Uploads `images` under their names and returns the page references in order, keeping the textures.
        std::expected<std::vector<font::Font::Page>, std::string> upload_pages(render::Device& device, InterfaceFonts& fonts, const std::string& prefix, std::vector<ovl::texture::RgbaImage> images)
        {
            std::map<std::string, ovl::texture::RgbaImage, std::less<>> named;
            for (std::size_t i = 0; i < images.size(); ++i)
                named.emplace(prefix + std::to_string(i), std::move(images[i]));
            auto uploaded = upload_textures(device, named);
            if (!uploaded)
                return std::unexpected(uploaded.error());
            std::vector<font::Font::Page> refs;
            for (std::size_t i = 0; i < named.size(); ++i)
            {
                const UiTexture& t = uploaded->at(prefix + std::to_string(i));
                refs.push_back({ texture_id(t.handle), t.width, t.height });
            }
            fonts.pages.merge(*uploaded);
            return refs;
        }

        // The fallback rendered at the size that matches `font`, merged in.
        std::expected<font::Font, std::string> with_fallback(render::Device& device, InterfaceFonts& fonts, const font::Font& font, const std::string& prefix)
        {
            const float em = fallback_em_pixels(cap_height(font), fonts.fallback->cap_per_em, kUnifontGrid);
            auto baked = bake_truetype(*fonts.fallback, em, kFallbackRanges, { .hard_edges = true });
            if (!baked)
                return std::unexpected(baked.error());
            auto refs = upload_pages(device, fonts, std::format("{}#fallback", prefix), std::move(baked->pages));
            if (!refs)
                return std::unexpected(refs.error());
            return font.with_fallback(font_from_baked(*baked, *refs));
        }

    } // namespace

    std::expected<font::FontSet, std::string> load_font_set(render::Device& device, const config::game::GameInstall& install, InterfaceFonts& fonts, int width)
    {
        const std::string relative = font::font_overlay(fonts.language, width);
        const auto path = install.find(relative);
        if (!path)
            return std::unexpected(std::format("{} not found", relative));
        auto loaded = font::load_font_images(*path);
        if (!loaded)
            return std::unexpected(std::format("{}: {}", relative, loaded.error()));

        const auto log = logging::logger("ui");
        font::FontSet set;
        for (font::FontImages& f : *loaded)
        {
            const auto role = font::set_font_role(f.name);
            if (!role)
                continue;
            const std::string prefix = std::format("{}/{}", width, f.name);
            auto refs = upload_pages(device, fonts, std::format("{}#", prefix), std::move(f.pages));
            if (!refs)
                return std::unexpected(refs.error());
            font::Font font = font::Font::from(f.font, *refs, f.face);
            if (fonts.fallback)
            {
                if (auto merged = with_fallback(device, fonts, font, prefix))
                    font = std::move(*merged);
                else
                    log.warning("fallback font for {}: {}", prefix, merged.error());
            }
            set.set(*role, std::move(font));
        }
        return set;
    }

    namespace
    {

        const font::FontSet& loaded_set(render::Device& device, const config::game::GameInstall& install, InterfaceFonts& fonts, int width)
        {
            if (const auto it = fonts.sets.find(width); it != fonts.sets.end())
                return it->second;
            auto set = load_font_set(device, install, fonts, width);
            if (!set)
            {
                logging::logger("ui").warning("fonts for {} at {}: {}", fonts.language, width, set.error());
                return fonts.sets[width];
            }
            return fonts.sets.emplace(width, std::move(*set)).first->second;
        }

    } // namespace

    const font::FontSet& fonts_for(render::Device& device, const config::game::GameInstall& install, InterfaceFonts& fonts, float view_width)
    {
        const int width = font::font_set_width(view_width);
        if (const auto it = fonts.mixed.find(width); it != fonts.mixed.end())
            return it->second;
        font::FontSet mixed = loaded_set(device, install, fonts, font::kFixedFontSetWidth);
        const font::FontSet& scaled = loaded_set(device, install, fonts, width);
        for (std::size_t i = 0; i < font::kFontRoleCount; ++i)
        {
            const auto role = font::scaled_role(static_cast<font::FontRole>(i));
            auto font = role ? scaled.get(static_cast<font::FontRole>(i)) : std::nullopt;
            if (font)
                mixed.set(*role, *font);
        }
        return fonts.mixed.emplace(width, std::move(mixed)).first->second;
    }

    void destroy_interface_fonts(render::Device& device, InterfaceFonts& fonts)
    {
        destroy_textures(device, fonts.pages);
        fonts.sets.clear();
        fonts.mixed.clear();
    }

} // namespace rct3::ui
