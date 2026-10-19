// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_loading.hpp"

#include "logging/log.hpp"
#include "strings/unicode.hpp"
#include "ui/image_set.hpp"
#include "ui/screens/loading_screen.hpp"
#include "ui/string_format.hpp"
#include "ui/ui_renderer.hpp"
#include "ui/ui_textures.hpp"

#include <array>
#include <string>

namespace rct3::scene
{

    namespace
    {
        constexpr std::array kStages = { ParkLoadStage::ReadFile, ParkLoadStage::Ground, ParkLoadStage::Objects, ParkLoadStage::Scene };
        static_assert(kStages.size() == kParkLoadStages);
    } // namespace

    std::string_view park_load_text(ParkLoadStage stage)
    {
        switch (stage)
        {
            case ParkLoadStage::ReadFile:
                return "LoadingScreen_LoadResources";
            case ParkLoadStage::Ground:
                return "LoadingScreen_Terrain";
            case ParkLoadStage::Objects:
                return "LoadingScreen_LoadObjects";
            case ParkLoadStage::Scene:
                return "CacheWarmingMessage";
        }
        return { };
    }

    std::string_view park_load_name(ParkLoadStage stage)
    {
        switch (stage)
        {
            case ParkLoadStage::ReadFile:
                return "reading the file";
            case ParkLoadStage::Ground:
                return "ground";
            case ParkLoadStage::Objects:
                return "objects";
            case ParkLoadStage::Scene:
                return "scene";
        }
        return { };
    }

    void add_loading_logo(render::Device& device, const config::GameInstall& install, ui::InterfaceAssets& assets)
    {
        namespace loading = ui::loading_screen;
        const auto log = logging::logger("ui");
        const auto path = install.find(loading::kLogoFile);
        auto images = path ? ui::load_image_set(*path) : std::unexpected(std::string("not found"));
        if (!images)
        {
            log.warning("loading screen logo, {}: {}", loading::kLogoFile, images.error());
            return;
        }
        // Only the logo: the overlay's other pictures are the startup's.
        std::erase_if(*images, [](const auto& image) { return image.first != loading::kLogo; });
        if (images->empty())
        {
            log.warning("loading screen logo: no {} in {}", loading::kLogo, loading::kLogoFile);
            return;
        }
        auto uploaded = ui::upload_textures(device, *images);
        if (!uploaded)
        {
            log.warning("loading screen logo: {}", uploaded.error());
            return;
        }
        assets.textures.merge(*uploaded);
        ui::destroy_textures(device, *uploaded); // Left over only where the name was taken already
    }

    void draw_park_loading(render::Device& device, ui::InterfaceAssets& assets, ui::DrawList& list, ParkLoadStage stage, std::uint64_t log_from)
    {
        namespace loading = ui::loading_screen;
        const render::Extent extent = device.extent();
        const ui::Rect screen { 0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height) };
        list.clear();
        // In pixels: the column is sized from the window, and text keeps its font's size, as in the game's menus.
        list.set_scale(1);
        const ui::FontSet& fonts = ui::fonts_for(device, assets, screen.width());
        loading::Look look {
            .heading_font = fonts.get(ui::FontRole::Large),
            .stage_font = fonts.get(ui::FontRole::Medium),
            .detail_font = fonts.get(ui::FontRole::Small),
        };
        if (const auto logo = assets.textures.find(loading::kLogo); logo != assets.textures.end())
            look.logo = loading::Picture { ui::texture_id(logo->second.handle), logo->second.width, logo->second.height };

        std::array<loading::Stage, kParkLoadStages> stages;
        for (std::size_t i = 0; i < kStages.size(); ++i)
        {
            const auto at = static_cast<std::size_t>(stage);
            stages[i] = { assets.strings->get(park_load_text(kStages[i])), i < at ? loading::StageState::Done : i == at ? loading::StageState::Current : loading::StageState::Waiting };
        }

        // The park's last word, and how many warnings loading has given.
        std::u16string detail;
        std::size_t warnings = 0;
        for (const logging::Entry& entry : logging::global().entries_since(log_from))
        {
            if (entry.level >= logging::Level::Warning)
                ++warnings;
            if (entry.category == "park")
                detail = strings::to_utf16(entry.message);
        }
        const std::array<ui::FormatArg, 1> count { { { u"count", strings::to_utf16(std::to_string(warnings)) } } };
        const std::u16string warning = warnings == 0 ? std::u16string { } : ui::format_string(assets.strings->get("Rct3pp_LoadingWarnings"), count);

        const float progress = loading::stage_progress(static_cast<std::size_t>(stage), kParkLoadStages);
        loading::draw(list, screen, look, { assets.strings->get(loading::kHeadingText), stages, detail, warning, progress });
        ui::render_draw_list(device, assets.renderer, list);
    }

} // namespace rct3::scene
