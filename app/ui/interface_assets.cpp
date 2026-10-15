// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/interface_assets.hpp"

#include "logging/log.hpp"
#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/skin_set.hpp"
#include "ui/translation_file.hpp"
#include "ui/truetype_font.hpp"

#include <utility>

namespace rct3::ui
{

    std::expected<InterfaceAssets, std::string> load_interface_assets(render::Device& device, const config::GameInstall& install, const std::string& language, const std::filesystem::path& fallback_font, std::span<const std::string_view> more_skins)
    {
        const auto log = logging::logger("ui");
        const auto main_ovl = install.find(game_skins::kMainOverlay);
        if (!main_ovl)
            return std::unexpected(std::string(game_skins::kMainOverlay) + " not found");
        auto set = load_skin_set(*main_ovl, game_skins::kWholeTextures);
        if (!set)
            return std::unexpected(set.error());
        for (const std::string_view name : more_skins)
        {
            const auto path = install.find(name);
            auto more = path ? load_skin_set(*path) : std::unexpected(std::string("not found"));
            if (!more)
            {
                log.warning("skins {}: {}", name, more.error());
                continue;
            }
            set->skins.merge(more->skins);
            set->images.merge(more->images);
        }

        InterfaceAssets assets;
        assets.install = install;
        assets.fonts.language = language;
        if (auto fallback = load_truetype(fallback_font))
            assets.fonts.fallback = std::move(*fallback);
        else
            log.warning("fallback font {}: {}; characters the game fonts lack will not show (run ./nob fetch)", fallback_font.string(), fallback.error());

        auto renderer = create_ui_renderer(device);
        if (!renderer)
            return std::unexpected(renderer.error());
        assets.renderer = *renderer;

        auto textures = upload_textures(device, set->images);
        if (!textures)
        {
            destroy_interface_assets(device, assets);
            return std::unexpected(textures.error());
        }
        assets.textures = std::move(*textures);
        assets.skins = skin_library(set->skins, assets.textures);

        // The language's strings, else English's: a language whose file is missing is shown in English.
        for (const std::string& name : { language, std::string(kDefaultLanguage) })
        {
            const auto path = install.find(text_overlay(name));
            if (!path)
                continue;
            if (auto strings = StringTable::load(*path))
            {
                *assets.strings = std::move(*strings);
                break;
            }
        }
        // The main overlay has the strings every language shares, such as the languages' own names.
        if (auto shared = StringTable::load(*main_ovl))
            assets.strings->merge(*shared);
        // Ours fill in what the game's tables lack.
        assets.strings->merge(own_strings(language));
        return assets;
    }

    const FontSet& fonts_for(render::Device& device, InterfaceAssets& assets, float view_width)
    {
        return fonts_for(device, *assets.install, assets.fonts, view_width);
    }

    void destroy_interface_assets(render::Device& device, InterfaceAssets& assets)
    {
        destroy_textures(device, assets.textures);
        destroy_interface_fonts(device, assets.fonts);
        destroy_ui_renderer(device, assets.renderer);
        assets = { };
    }

} // namespace rct3::ui
