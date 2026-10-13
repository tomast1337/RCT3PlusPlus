// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/options_dialog.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        using O = config::GameOption;

        struct TabInfo
        {
            std::string_view icon;
            std::string_view caption_key;
        };
        constexpr std::array<TabInfo, kOptionsPageCount> kTabs = { {
            { "File_GameOptions_Game", "GameOptionsDialog_GameSettingsTabHelp" },
            { "File_GameOptions_Keyboard", "GameOptionsDialog_KeyBindingsHelp" },
            { "File_GameOptions_Graphics", "GameOptionsDialog_GraphicSettingsTabHelp" },
            { "File_GameOptions_Camera", "GameOptionsDialog_CameraSettingsTabHelp" },
        } };

        constexpr std::string_view kTitle = "GameOptionsDialog_Title";
        constexpr std::string_view kLanguageName = "Language_"; ///< + the game's name: the language in its own script
        constexpr std::string_view kCurrencyBefore = "Money";   ///< + number + "a": what goes before amounts
        constexpr std::string_view kNoKeyChange = "GameOptionsDialog_NoKeyBindingHelp";
        constexpr std::string_view kNoMusic = "AttractionViewDialogNoMusic";

        constexpr std::u16string_view kPressKeys = u"Press keys...";
        constexpr std::u16string_view kNoKeys = u"None";
        constexpr std::u16string_view kKeysTip = u"Click, then press the keys: F1 to F12, Escape, Enter, Tab, Page Up, Page Down or Insert, with Ctrl or Shift if wanted, or Ctrl with a letter or digit. Escape alone keeps the keys.";
        constexpr std::u16string_view kUsualKeys = u"Usual keys";

        struct DetailRow
        {
            config::DetailPair options;
            std::string_view key;
        };

        std::u16string str(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        std::vector<std::u16string> items(const StringTable& strings, std::initializer_list<std::string_view> keys)
        {
            std::vector<std::u16string> out;
            for (const std::string_view k : keys)
                out.push_back(str(strings, k));
            return out;
        }

        std::u16string ascii(std::string_view s)
        {
            return { s.begin(), s.end() };
        }

        std::u16string trimmed(std::u16string s)
        {
            while (!s.empty() && s.front() == u' ')
                s.erase(s.begin());
            while (!s.empty() && s.back() == u' ')
                s.pop_back();
            return s;
        }

    } // namespace

    OptionsDialog::OptionsDialog(std::string name, const OptionsDialogStyle& style, const StringTable& strings, config::GameOptions& options, OptionsChoices choices)
        : Window(std::move(name), style.window, style.page_width + style.window.content_inset.x0 + style.window.content_inset.x1, style.tab_bar_height + style.page_height + style.window.content_inset.y0 + style.window.content_inset.y1)
        , style_(style)
        , options_(options)
        , choices_(std::move(choices))
        , keys_(choices_.keys)
    {
        title = strings.get(kTitle);
        icon = style.icon;
        Widget& body = content();

        caption_ = &body.add<Label>("caption", std::u16string { }, style.caption_font);
        caption_->style.vertical = Align::Centre;
        caption_->style.colour = style.caption_colour;
        caption_->placement = top_left(style.caption_x, 0, style.caption_width, style.caption_height);
        for (std::size_t i = 0; i < kOptionsPageCount; ++i)
        {
            auto& tab = body.add<Button>("tab" + std::to_string(i), std::string(kTabs[i].icon));
            tab.toggle = true;
            tab.tooltip = str(strings, kTabs[i].caption_key);
            tab.placement = top_left(style.tabs_x + style.tab_step * static_cast<float>(i), 0, style.tab_size, style.tab_size);
            tabs_[i] = &tab;
            captions_[i] = tab.tooltip;

            auto& page = body.add<Widget>("page" + std::to_string(i));
            page.placement = top_left(0, style.tab_bar_height, style.page_width, style.page_height);
            pages_[i] = &page;
        }
        build_general(*pages_[0], strings);
        build_keys(*pages_[1], strings);
        build_graphics(*pages_[2], strings);
        build_camera(*pages_[3], strings);
        show_page(OptionsPage::General);
        for (const auto& rows : rows_)
            rows->update();
        fit_page();
    }

    void OptionsDialog::fit_page()
    {
        // The window wraps the open page, as the game's do.
        float bottom = 0;
        for (const auto& child : pages_.at(static_cast<std::size_t>(page_))->children())
        {
            if (child->visible)
                bottom = std::max(bottom, child->placement.offsets.y1);
        }
        const Rect& in = style_.window.content_inset;
        set_height(in.y0 + style_.tab_bar_height + bottom + in.y1);
    }

    void OptionsDialog::build_general(Widget& page, const StringTable& strings)
    {
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, style_.rows, options_));
        rows.tick(O::Fullscreen, str(strings, "GameOptionsDialog_Fullscreen"));
        if (choices_.languages.size() > 1)
        {
            // Each language by its own name, so it can be found whatever the current one.
            std::vector<std::u16string> names;
            for (const std::string& l : choices_.languages)
            {
                const std::u16string own = str(strings, std::string(kLanguageName) + l);
                names.push_back(own.empty() ? ascii(l) : own);
            }
            // In the usual font: its fallback has every script, and the game's all-scripts font does not
            // draw right yet.
            language_ = &rows.list(str(strings, "GameOptionsDialog_Language"), std::move(names));
            const auto at = std::ranges::find(choices_.languages, choices_.language);
            if (at != choices_.languages.end())
                language_->select(static_cast<std::size_t>(at - choices_.languages.begin()));
        }
        std::vector<std::u16string> currencies;
        const int last_currency = static_cast<int>(config::game_option_info(O::Currency).maximum);
        for (int n = 0; n <= last_currency; ++n)
        {
            const std::string key = std::string(kCurrencyBefore) + std::to_string(n);
            std::u16string sign = trimmed(str(strings, key + "a") + str(strings, key + "b"));
            currencies.push_back(sign.empty() ? ascii(std::to_string(n + 1)) : std::move(sign));
        }
        rows.choice(O::Currency, str(strings, "GameOptionsDialog_Currency"), std::move(currencies));
        rows.choice(O::DistanceUnits, str(strings, "GameOptionsDialog_DistanceAndSpeed"), items(strings, { "GameOptionsDialog_Metric", "GameOptionsDialog_Imperial", "GameOptionsDialog_SI" }));
        rows.choice(O::TemperatureUnits, str(strings, "GameOptionsDialog_Temperature"), items(strings, { "GameOptionsDialog_Celcius", "GameOptionsDialog_Fahrenheit" }));
        rows.level(O::Brightness, str(strings, "GameOptionsDialog_Brightness"));
        rows.section();
        rows.tick(O::LiveContent, str(strings, "GameOptionsDialog_EnableLiveContent"), str(strings, "GameOptionsDialog_EnableLiveContent_TT"));
        rows.section();
        rows.level(O::GameVolume, str(strings, "GameOptionsDialog_SetVolumeGame"));
        rows.level(O::MusicVolume, str(strings, "GameOptionsDialog_SetVolumeMusic"));
        rows.level(O::InterfaceVolume, str(strings, "GameOptionsDialog_SetVolumeGUI"));
        rows.section();
        rows.tick(O::ImportMyMusic, str(strings, "GameOptionsDialog_ImportMusic"), str(strings, "GameOptionsDialog_ImportMusicTT"));
        rows.tick(O::CycleMenuMusic, str(strings, "GameOptionsDialog_CycleMusic"), str(strings, "GameOptionsDialog_CycleMusicTT"));
        if (!choices_.background_music.empty())
        {
            rows.section();
            std::vector<std::u16string> tracks;
            for (const std::string& name : choices_.background_music)
                tracks.push_back(ascii(name));
            tracks.push_back(str(strings, kNoMusic));
            auto& music = rows.list(str(strings, "GameOptionsDialog_BackgroundMusic"), std::move(tracks), str(strings, "GameOptionsDialog_BackgroundMusicTT"));
            const std::size_t track = choices_.background_track.value_or(choices_.background_music.size());
            music.select(std::min(track, choices_.background_music.size()));
        }
    }

    void OptionsDialog::build_keys(Widget& page, const StringTable& strings)
    {
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, style_.rows, options_));
        // Ours, in English until they have names in a string table.
        for (const CommandInfo& info : commands_table())
        {
            if (info.command == Command::DeveloperWindows)
                rows.section();
            KeyBox& box = rows.keys(std::u16string(info.label), keys_.keys(info.command), std::u16string(kPressKeys), std::u16string(kKeysTip));
            box.none = kNoKeys;
            key_boxes_.at(static_cast<std::size_t>(info.command)) = &box;
        }
        rows.section();
        rows.text(u"Move by a word", u"Ctrl+Left, Ctrl+Right", str(strings, kNoKeyChange));
        rows.update();

        usual_keys_ = &page.add<Button>("usual_keys", style_.button);
        usual_keys_->text = kUsualKeys;
        usual_keys_->font = style_.rows.font;
        usual_keys_->text_inset = style_.button_inset;
        usual_keys_->placement = top_left(style_.page_width - style_.button_width - style_.rows.left, rows.height() + style_.rows.section_gap, style_.button_width, style_.button_height);
    }

    void OptionsDialog::build_graphics(Widget& page, const StringTable& strings)
    {
        OptionRowsStyle top_style = style_.rows;
        top_style.control_x = 170; // The screen sizes are short; the lists sit nearer their names
        auto& top = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, top_style, options_));
        std::vector<std::u16string> sizes;
        for (const ScreenSize& s : choices_.screen_sizes)
            sizes.push_back(ascii(std::to_string(s.width) + " x " + std::to_string(s.height)));
        resolution_ = &top.list(str(strings, "GameOptionsDialog_ScreenResolution"), std::move(sizes));
        resolution_->select(choices_.screen_size);
        resolution_->enabled = !choices_.screen_sizes.empty();
        top.choice(O::GraphicsQuality, str(strings, "QualityVsPerformanceLabel"), items(strings, { "QualityVsPerformance_HighPerformance", "QualityVsPerformance_Performance", "QualityVsPerformance_Balanced", "QualityVsPerformance_Quality", "QualityVsPerformance_HighQuality", "QualityVsPerformance_Custom" }));
        graphics_top_ = &top;

        // The custom settings scroll below.
        custom_ = &page.add<ScrollArea>("custom", style_.scroll);
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(custom_->content(), style_.page_width - style_.scroll.thickness, style_.rows, options_));
        std::vector<std::u16string> samples = { str(strings, "GameOptionsDialog_AntiAliasNone") };
        for (const int level : choices_.anti_aliasing_levels)
            samples.push_back(str(strings, "GameOptionsDialog_AntiAliasLevel") + u" " + ascii(std::to_string(level)));
        rows.choice(O::AntiAliasing, str(strings, "GameOptionsDialog_AntiAlias"), std::move(samples));
        rows.choice(O::Shadows, str(strings, "GameOptionsDialog_Shadows"), items(strings, { "GameOptionsDialog_ShadowsTypeNone", "GameOptionsDialog_ShadowsTypeStatic", "GameOptionsDialog_ShadowsTypeDynamic" }));
        rows.section();
        rows.tick(O::ShowDetailLevels, str(strings, "GameOptionsDialog_LodOptions"), str(strings, "GameOptionsDialog_LodOptionsHelp"));
        const OptionIs details { O::ShowDetailLevels, 1 };
        rows.headings(str(strings, "GameOptionsDialog_LODTitle"), str(strings, "GameOptionsDialog_DrawDistanceTitle"));
        rows.shown_when(details);
        const std::array<std::string_view, config::kDetailPairs.size()> kinds = { "GameOptionsDialog_PeopleLOD", "GameOptionsDialog_AnimalLOD", "GameOptionsDialog_VegetationLOD", "GameOptionsDialog_FenceLOD", "GameOptionsDialog_BuildingLOD", "GameOptionsDialog_PathsLOD", "GameOptionsDialog_SceneryLOD", "GameOptionsDialog_SupportLOD", "GameOptionsDialog_TrackLOD", "GameOptionsDialog_RideLOD" };
        for (std::size_t i = 0; i < kinds.size(); ++i)
        {
            rows.pair(config::kDetailPairs[i], str(strings, kinds[i]));
            rows.shown_when(details);
        }
        rows.number(O::ImposterDistance, str(strings, "GameOptionsDialog_GeometryImposterDistance"), str(strings, "GameOptionsDialog_GeometryImposterDistance_TT"));
        rows.shown_when(details);
        rows.section();
        rows.tick(O::BlendDetailLevels, str(strings, "GameOptionsDialog_SceneryItemLODBlend"), str(strings, "GameOptionsDialog_SceneryItemLODBlend_TT"));
        rows.tick(O::CompressTextures, str(strings, "GameOptionsDialog_TextureCompress"), str(strings, "GameOptionsDialog_TextureCompress_TT"));
        rows.tick(O::AggressiveDepthBias, str(strings, "GameOptionsDialog_AggressiveZBias"), str(strings, "GameOptionsDialog_AggressiveZBiasHelp"));
        rows.level(O::TextureDetail, str(strings, "GameOptionsDialog_TextureLOD"), str(strings, "GameOptionsDialog_TextureLODHelp"));
        rows.tick(O::Trees3D, str(strings, "GameOptionsDialog_3DTreesAndPlants"), str(strings, "GameOptionsDialog_3DTreesAndPlantsHelp"));
        rows.tick(O::OffMapTrees, str(strings, "GameOptionsDialog_SkirtTrees"), str(strings, "GameOptionsDialog_SkirtTreesHelp"));
        rows.tick(O::SimpleTerrain, str(strings, "GameOptionsDialog_UntexturedTerrain"), str(strings, "GameOptionsDialog_UntexturedTerrainHelp"));
        rows.section();
        rows.tick(O::ReflectiveWater, str(strings, "GameOptionsDialog_WaterReflect_Enabled"), str(strings, "GameOptionsDialog_WaterReflect_EnabledHelp"));
        constexpr std::array<std::pair<O, std::string_view>, 5> kReflections = { {
            { O::ReflectTerrain, "GameOptionsDialog_WaterReflect_Terrain" },
            { O::ReflectScenery, "GameOptionsDialog_WaterReflect_Scenery" },
            { O::ReflectPeople, "GameOptionsDialog_WaterReflect_People" },
            { O::ReflectAnimals, "GameOptionsDialog_WaterReflect_Animals" },
            { O::ReflectParticles, "GameOptionsDialog_WaterReflect_Particles" },
        } };
        for (const auto& [option, key] : kReflections)
        {
            rows.tick(option, str(strings, key), str(strings, std::string(key) + "Help"));
            rows.indent();
            rows.shown_when({ O::ReflectiveWater, 1 });
        }
        rows.tick(O::WaterBumps, str(strings, "GameOptionsDialog_WaterBump_Enabled"));
        rows.tick(O::FlowingWaterEnvironment, str(strings, "GameOptionsDialog_WaterFlowEnvMapEnabled"));
        rows.tick(O::Clouds, str(strings, "GameOptionsDialog_CloudsEnabled"));
        rows.tick(O::Mountains, str(strings, "GameOptionsDialog_DistantMountainsEnabled"));
        rows.choice(O::DayNight, str(strings, "GameOptionsDialog_FastDayNightCycle"), items(strings, { "GameOptionsDialog_DNC_Original", "GameOptionsDialog_DNC_Blend", "GameOptionsDialog_DNC_Cycle" }), str(strings, "GameOptionsDialog_FastDayNightCycle_TT"));
        rows.tick(O::Bloom, str(strings, "GameOptionsDialog_Bloom"), str(strings, "GameOptionsDialog_Bloom_TT"));
        rows.tick(O::Refraction, str(strings, "GameOptionsDialog_Refraction"), str(strings, "GameOptionsDialog_Refraction_TT"));
        rows.tick(O::WaterDroplets, str(strings, "GameOptionsDialog_WaterDroplets"), str(strings, "GameOptionsDialog_WaterDroplets_TT"));
        rows.indent();
        rows.shown_when({ O::Refraction, 1 });
        rows.tick(O::LaserOcclusion, str(strings, "GameOptionsDialog_LaserRayHit"), str(strings, "GameOptionsDialog_LaserRayHit_TT"));
    }

    void OptionsDialog::build_camera(Widget& page, const StringTable& strings)
    {
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, style_.rows, options_));
        rows.tick(O::InvertRideCameraY, str(strings, "GameOptionsDialog_InvertRidecam"), str(strings, "GameOptionsDialog_InvertRidecamTT"));
        rows.tick(O::AllPhotos, str(strings, "GameOptionsDialog_AllowAllPhotos"), str(strings, "GameOptionsDialog_AllowAllPhotosTT"));
        rows.tick(O::PhotoSpotPhotos, str(strings, "GameOptionsDialog_AllowVistaPhotos"), str(strings, "GameOptionsDialog_AllowVistaPhotosTT"));
        rows.indent();
        rows.enabled_when({ O::AllPhotos, 1 });
        rows.tick(O::CoasterCameraPhotos, str(strings, "GameOptionsDialog_AllowCoasterPhotos"), str(strings, "GameOptionsDialog_AllowCoasterPhotosTT"));
        rows.indent();
        rows.enabled_when({ O::AllPhotos, 1 });
        rows.section();
        rows.tick(O::AutoScrollAtEdge, str(strings, "GameOptionsDialog_AutoScrollAtEdge"), str(strings, "GameOptionsDialog_AutoScrollAtEdgeHelp"));
        rows.section();
        rows.choice(O::CameraMode, str(strings, "GameOptionsDialog_UCM_Label"), items(strings, { "GameOptionsDialog_UCM_Scooping", "GameOptionsDialog_UCM_Fly", "GameOptionsDialog_UCM_Isometric", "GameOptionsDialog_UCM_Simple" }));
        rows.section();
        // Only the settings of the chosen mode.
        rows.number(O::CameraControls, str(strings, "GameOptionsDialog_CamereControls"));
        rows.shown_when({ O::CameraMode, 0 });
        rows.tick(O::CameraIgnoresScenery, str(strings, "GameOptionsDialog_CameraIgnoresScenery"), str(strings, "GameOptionsDialog_CameraIgnoresSceneryHelp"));
        rows.shown_when({ O::CameraMode, 0 });
        rows.tick(O::CameraFocusOnMiddle, str(strings, "GameOptionsDialog_CameraFocusPositionIsMiddleScreen"), str(strings, "GameOptionsDialog_CameraFocusPositionIsMiddleScreenHelp"));
        rows.shown_when({ O::CameraMode, 0 });
        rows.level(O::FreeLookSpeed, str(strings, "GameOptionsDialog_FlyCamSensi"));
        rows.shown_when({ O::CameraMode, 1 });
        rows.level(O::IsometricRotation, str(strings, "GameOptionsDialog_CamereIsometricRotTolLabel"));
        rows.shown_when({ O::CameraMode, 2 });
    }

    void OptionsDialog::show_page(OptionsPage page)
    {
        page_ = page;
        for (std::size_t i = 0; i < kOptionsPageCount; ++i)
        {
            const bool current = i == static_cast<std::size_t>(page);
            pages_[i]->visible = current;
            tabs_[i]->on = current;
        }
        caption_->text = captions_.at(static_cast<std::size_t>(page));
    }

    bool OptionsDialog::take_change()
    {
        return std::exchange(changes_, 0) > 0;
    }

    std::optional<std::string> OptionsDialog::take_language()
    {
        return std::exchange(picked_language_, std::nullopt);
    }

    std::optional<std::size_t> OptionsDialog::take_screen_size()
    {
        return std::exchange(picked_screen_size_, std::nullopt);
    }

    std::optional<KeyMap> OptionsDialog::take_keys()
    {
        return std::exchange(picked_keys_, std::nullopt);
    }

    bool OptionsDialog::waiting_for_keys() const
    {
        return std::ranges::any_of(key_boxes_, [](const KeyBox* box) { return box != nullptr && box->waiting(); });
    }

    void OptionsDialog::update_keys()
    {
        bool changed = false;
        for (std::size_t i = 0; i < kCommandCount; ++i)
        {
            if (key_boxes_[i]->take_change())
            {
                // Takes the keys from any other command too.
                keys_.set_keys(static_cast<Command>(i), key_boxes_[i]->combos);
                changed = true;
            }
        }
        if (usual_keys_->take_click())
        {
            keys_.reset();
            changed = true;
        }
        if (!changed)
            return;
        for (std::size_t i = 0; i < kCommandCount; ++i)
            key_boxes_[i]->combos = keys_.keys(static_cast<Command>(i));
        picked_keys_ = keys_;
    }

    void OptionsDialog::update(const UpdateContext& context)
    {
        Window::update(context);
        for (std::size_t i = 0; i < kOptionsPageCount; ++i)
        {
            if (tabs_[i]->take_click())
                show_page(static_cast<OptionsPage>(i));
        }
        show_page(page_); // A click on the open tab would have turned it off

        for (const auto& rows : rows_)
        {
            if (rows->update())
                ++changes_;
        }
        if (language_ != nullptr && language_->take_change() && language_->selected())
            picked_language_ = choices_.languages.at(*language_->selected());
        update_keys();
        if (resolution_->take_change() && resolution_->selected())
            picked_screen_size_ = *resolution_->selected();

        const float top = graphics_top_->height() + style_.rows.section_gap;
        custom_->visible = options_.choice(O::GraphicsQuality) == config::kCustomQuality;
        custom_->placement = top_left(0, top, style_.page_width, std::min(style_.scroll_height, style_.page_height - top));
        fit_page();
    }

} // namespace rct3::ui
