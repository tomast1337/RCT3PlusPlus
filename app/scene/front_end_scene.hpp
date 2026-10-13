// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end over the park: the front screen's menus and the windows they open, with the game's skins,
// strings and fonts. The game options are read from the options file at the start and written back when
// their window closes. What the window itself must do (quit, change size or go full screen) and a change
// of language, which needs the strings and fonts again, are for the owner to take.
#pragma once

#include "config/game_install.hpp"
#include "config/game_options.hpp"
#include "render/device.hpp"
#include "scene/career_browser.hpp"
#include "scene/save_browser.hpp"
#include "scene/slideshow_view.hpp"
#include "ui/desktop.hpp"
#include "ui/draw_list.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/front_end.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace rct3::scene
{

    struct FrontEndSceneSetup
    {
        std::vector<std::string> languages; ///< Installed
        std::string language;               ///< The one to use
        std::filesystem::path fallback_font;
        std::optional<std::filesystem::path> options_file; ///< Without it the options are not kept
        std::optional<config::UserData> data;              ///< Where the saves and careers are; without it there are none
        std::vector<ui::ScreenSize> screen_sizes;
        std::optional<std::size_t> screen_size;
        ui::KeyMap keys;                                    ///< In use, for the options window to change
        std::vector<std::filesystem::path> picture_folders; ///< Of the slideshow behind the front screen, in order
        std::uint32_t picture_seed = 0;                     ///< Shuffles the slideshow
        bool reload = false;                                ///< Built again after a language change: no opening animation, options window open
    };

    struct FrontEndScene
    {
        ui::InterfaceAssets assets;
        SlideshowView slideshow;
        std::vector<std::filesystem::path> picture_folders; ///< The slideshow's usual pictures
        std::uint32_t picture_seed = 0;
        std::optional<std::string> slideshow_shown; ///< The career map's folder of pictures shown instead
        SaveBrowser saves;
        CareerBrowser careers;
        std::optional<std::filesystem::path> options_file;
        // On the heap: the front end keeps references to the desktop and the options, and the scene moves.
        struct Live
        {
            config::GameOptions options;
            ui::Desktop desktop;
            std::optional<ui::FrontEnd> front;
        };
        std::unique_ptr<Live> live = std::make_unique<Live>();
        const ui::FontSet* current_fonts = nullptr; ///< The set for the width of the last update
        ui::DrawList list;
        std::optional<float> scale; ///< Chosen interface scale; nothing picks one from the window height
        bool fullscreen = false;    ///< As last applied
    };

    /// Things the scene wants done to the window or the program.
    struct FrontEndRequests
    {
        bool quit = false;
        std::optional<bool> fullscreen;
        std::optional<std::size_t> screen_size; ///< Into FrontEndSceneSetup::screen_sizes
        std::optional<std::string> language;
        std::optional<ui::KeyMap> keys;
    };

    [[nodiscard]] std::expected<FrontEndScene, std::string> create_front_end_scene(render::Device& device, const config::GameInstall& install, FrontEndSceneSetup setup);

    /// Lays out for the device's size and routes the pointer, given in framebuffer pixels, and the keys.
    /// Returns whether the interface has the pointer.
    bool update_front_end_scene(render::Device& device, FrontEndScene& scene, const ui::PointerState& pointer_pixels, const ui::KeyboardState& keyboard, float elapsed);

    /// What the user asked for since the last call.
    [[nodiscard]] FrontEndRequests take_front_end_requests(FrontEndScene& scene);

    /// True while the interface waits for a key combination, which should then do nothing else.
    [[nodiscard]] bool front_end_waiting_for_keys(const FrontEndScene& scene);

    void draw_front_end_scene(render::Device& device, FrontEndScene& scene);

    /// Saves the options if their window is still open, and frees the scene.
    void destroy_front_end_scene(render::Device& device, FrontEndScene& scene);

} // namespace rct3::scene
