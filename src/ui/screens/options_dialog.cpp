// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/options_dialog.hpp"

#include "ui/money_text.hpp"

#include <algorithm>
#include <cmath>
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
            { "File_GameOptions_Scenic", "GameOptionsDialog_ScenicSettingsTabHelp" },
            { "File_GameOptions_Camera", "GameOptionsDialog_CameraSettingsTabHelp" },
        } };

        constexpr std::string_view kTitle = "GameOptionsDialog_Title";
        constexpr std::string_view kLanguageName = "Language_"; ///< + the game's name: the language in its own script
        constexpr std::string_view kNoKeyChange = "GameOptionsDialog_NoKeyBindingHelp";
        constexpr std::string_view kKeyChange = "GameOptionsDialog_KeyBindingHelp";
        constexpr std::string_view kKeySettingPrefix = "KeySetting_";       ///< Of the game commands' names
        constexpr std::string_view kInterfaceKeys = "Rct3pp_InterfaceKeys"; ///< Ours: the group of our commands
        constexpr std::string_view kNoMusic = "AttractionViewDialogNoMusic";

        // Ours, from our translations (translation_file.hpp).
        constexpr std::string_view kPressKeys = "Rct3pp_PressKeys";
        constexpr std::string_view kNoKeys = "Rct3pp_NoKeys";
        constexpr std::string_view kKeysTip = "Rct3pp_KeysTip";
        constexpr std::string_view kUsualKeys = "Rct3pp_UsualKeys";
        constexpr std::string_view kShuffle = "Rct3pp_Shuffle"; ///< A track at random each time
        constexpr std::string_view kMoveByWord = "Rct3pp_MoveByWord";
        constexpr std::string_view kMoveByWordKeys = "Rct3pp_MoveByWordKeys";
        constexpr std::string_view kCloudsHelp = "Rct3pp_CloudsHelp";
        constexpr std::string_view kSkyHelp = "Rct3pp_SkyHelp";
        constexpr std::string_view kMountainsHelp = "Rct3pp_MountainsHelp";
        constexpr std::string_view kBlendBand = "Rct3pp_DetailBlendBand";
        constexpr std::string_view kBlendBandHelp = "Rct3pp_DetailBlendBandHelp";

        constexpr std::string_view kAutoDetect = "AutoDetectLabel";
        constexpr std::string_view kAutoDetectTip = "GameOptionsDialog_GraphicsDetailLevel";

        // The camera page's help: what each mouse button and the wheel does in each control mode, as the game
        // tells it. The lines start with the button's name; a button with nothing to do shows only that.
        enum class CameraAction : std::uint8_t
        {
            None,
            Move,
            Rotate,
            RotateAndTilt,
            Zoom,
            ZoomAndRotate,
            Tilt,
        };
        constexpr std::array<std::string_view, 7> kCameraActionKeys = { "", "GameOptionsDialog_CameraControlsActionMove", "GameOptionsDialog_CameraControlsActionRotate", "GameOptionsDialog_CameraControlsActionRotateAndTilt", "GameOptionsDialog_CameraControlsActionZoom", "GameOptionsDialog_CameraControlsActionZoomAndRotate", "GameOptionsDialog_CameraControlsActionTilt" };
        constexpr std::array<std::string_view, 5> kCameraButtonKeys = { "GameOptionsDialog_CameraControlsLeftMouse", "GameOptionsDialog_CameraControlsRightMouse", "GameOptionsDialog_CameraControlsBothMouse", "GameOptionsDialog_CameraControlsMiddleMouse", "GameOptionsDialog_CameraControlsMouseWheel" };
        constexpr std::string_view kCameraKeysInfo = "GameOptionsDialog_CameraControlsKeysInfo";

        using CameraLines = std::array<CameraAction, 5>; ///< Left, right, both, middle, wheel
        using A = CameraAction;
        /// The advanced mode, by its "camera controls" number.
        constexpr std::array<CameraLines, 3> kAdvancedCamera = { {
            { A::None, A::ZoomAndRotate, A::Move, A::RotateAndTilt, A::Tilt },
            { A::None, A::Move, A::RotateAndTilt, A::RotateAndTilt, A::Zoom },
            { A::None, A::RotateAndTilt, A::Move, A::RotateAndTilt, A::Zoom },
        } };
        /// The other modes: freelook, isometric, normal.
        constexpr std::array<CameraLines, 3> kOtherCamera = { {
            { A::None, A::Rotate, A::None, A::Move, A::None },
            { A::None, A::Move, A::Rotate, A::None, A::Zoom },
            { A::None, A::Move, A::Move, A::Rotate, A::Zoom },
        } };
        constexpr float kFreelookMode = 1;

        CameraLines camera_lines(int mode, int controls)
        {
            if (mode <= 0)
                return kAdvancedCamera.at(static_cast<std::size_t>(std::clamp(controls, 0, 2)));
            return kOtherCamera.at(static_cast<std::size_t>(std::min(mode, 3) - 1));
        }

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
        build_scenic(*pages_[3], strings);
        build_camera(*pages_[4], strings);
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
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, style_.rows, options_, choices_.inert));
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
            if (choices_.language_locked)
                rows.disable();
        }
        std::vector<std::u16string> currencies;
        const int last_currency = static_cast<int>(config::game_option_info(O::Currency).maximum);
        for (int n = 0; n <= last_currency; ++n)
        {
            const Currency c = currency(strings, n);
            std::u16string sign = trimmed(c.before + c.after);
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
            tracks.push_back(str(strings, kShuffle));
            tracks.push_back(str(strings, kNoMusic));
            music_ = &rows.list(str(strings, "GameOptionsDialog_BackgroundMusic"), std::move(tracks), str(strings, "GameOptionsDialog_BackgroundMusicTT"));
            show_background_music();
        }
    }

    void OptionsDialog::show_background_music()
    {
        const std::vector<std::string>& files = choices_.background_music;
        const std::size_t shuffle = files.size();
        const std::size_t none = shuffle + 1;
        std::size_t shown = choices_.background_track.value_or(none);
        if (options_.flag(O::ShuffleBackgroundMusic))
            shown = shuffle;
        else if (const auto& track = options_.background_music())
        {
            const auto it = std::ranges::find(files, *track);
            shown = track->empty() ? none : it != files.end() ? static_cast<std::size_t>(it - files.begin()) : shown;
        }
        music_->select(std::min(shown, none));
    }

    void OptionsDialog::update_background_music()
    {
        if (music_ == nullptr || !music_->take_change() || !music_->selected())
            return;
        const std::vector<std::string>& files = choices_.background_music;
        const std::size_t picked = *music_->selected();
        options_.set(O::ShuffleBackgroundMusic, picked == files.size() ? 1.0f : 0.0f);
        if (picked < files.size())
            options_.set_background_music(files[picked]);
        else if (picked > files.size())
            options_.set_background_music(std::string { });
        ++changes_;
    }

    void OptionsDialog::build_keys(Widget& page, const StringTable& strings)
    {
        const KeyListStyle& k = style_.keys;
        const float width = style_.page_width;
        auto& back = page.add<Panel>("key_back", std::string { });
        back.colour = k.background;
        back.placement = top_left(0, 0, width, k.height);
        auto& area = page.add<ScrollArea>("key_list", style_.scroll);
        area.placement = top_left(0, 0, width, k.height);
        Widget& list = area.content();

        float y = k.top;
        int count = 0;
        const auto bar = [&](std::u16string text) {
            y += k.group_gap;
            auto& fill = list.add<Panel>("bar" + std::to_string(count), std::string { });
            fill.colour = k.bar_colour;
            fill.placement = top_left(0, y, width, k.bar_height);
            auto& name = list.add<Label>("bar_name" + std::to_string(count++), std::move(text), style_.rows.font);
            name.style.colour = k.bar_text_colour;
            name.style.vertical = Align::Centre;
            name.placement = top_left(k.bar_text_inset, y, width - 2 * k.bar_text_inset, k.bar_height);
            y += k.bar_height;
        };
        const auto row = [&](std::u16string text, Widget& keys) {
            y += k.row_gap;
            auto& name = list.add<Label>("name" + std::to_string(count++), std::move(text), style_.rows.font);
            name.style.colour = style_.rows.text_colour;
            name.style.vertical = Align::Centre;
            name.placement = top_left(k.name_x, y, k.name_width, k.row_height);
            keys.placement = top_left(k.keys_x, y, width - k.keys_right - k.keys_x, k.row_height);
            y += k.row_height;
        };
        const auto box = [&](std::vector<KeyCombo> combos) -> KeyBox& {
            auto& b = list.add<KeyBox>("keys" + std::to_string(count++), std::move(combos));
            b.focused_skin = k.focused;
            b.font = style_.rows.font;
            b.text_colour = style_.rows.text_colour;
            b.padding = { 3, 0, 3, 0 };
            return b;
        };
        const auto fixed = [&](std::u16string keys) -> Label& {
            auto& l = list.add<Label>("fixed" + std::to_string(count++), std::move(keys), style_.rows.font);
            l.style.colour = style_.rows.text_colour;
            l.style.vertical = Align::Centre;
            l.tooltip = str(strings, kNoKeyChange);
            return l;
        };

        // The game's commands, as the game groups and names them.
        for (const PlayKeyGroup group : { PlayKeyGroup::Universal, PlayKeyGroup::DuringPlay })
        {
            bar(str(strings, play_key_group_key(group)));
            for (const PlayCommandInfo& info : play_commands_table())
            {
                if (info.group != group)
                    continue;
                std::u16string name = str(strings, info.label_key);
                if (name.empty())
                    name = ascii(info.label_key.substr(kKeySettingPrefix.size()));
                if (!info.fixed.empty())
                {
                    Label& keys = fixed(std::u16string(info.fixed));
                    keys.style.horizontal = Align::Start;
                    row(std::move(name), keys);
                    keys.placement.offsets.x0 += 3;
                    continue;
                }
                const auto& combo = keys_.play_key(info.command);
                KeyBox& b = box(combo ? std::vector { *combo } : std::vector<KeyCombo> { });
                b.game_key = true;
                b.tooltip = str(strings, kKeyChange);
                play_boxes_.at(static_cast<std::size_t>(info.command)) = &b;
                row(std::move(name), b);
            }
        }

        // Ours.
        bar(str(strings, kInterfaceKeys));
        for (const CommandInfo& info : commands_table())
        {
            KeyBox& b = box(keys_.keys(info.command));
            b.prompt = str(strings, kPressKeys);
            b.none = str(strings, kNoKeys);
            b.tooltip = str(strings, kKeysTip);
            key_boxes_.at(static_cast<std::size_t>(info.command)) = &b;
            row(str(strings, info.label_key), b);
        }
        row(str(strings, kMoveByWord), fixed(str(strings, kMoveByWordKeys)));
        auto& end = list.add<Widget>("end");
        end.placement = top_left(0, y, 1, k.bottom);

        usual_keys_ = &page.add<Button>("usual_keys", style_.button);
        usual_keys_->text = str(strings, kUsualKeys);
        usual_keys_->font = style_.rows.font;
        usual_keys_->text_inset = style_.button_inset;
        usual_keys_->placement = top_left(style_.page_width - style_.button_width - style_.rows.left, k.height + style_.rows.section_gap, style_.button_width, style_.button_height);
    }

    void OptionsDialog::build_graphics(Widget& page, const StringTable& strings)
    {
        OptionRowsStyle top_style = style_.rows;
        top_style.control_x = 170; // The screen sizes are short; the lists sit nearer their names
        auto& top = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, top_style, options_, choices_.inert));
        // With the screen size, not on the general page as in the game.
        top.tick(O::Fullscreen, str(strings, "GameOptionsDialog_Fullscreen"));
        std::vector<std::u16string> sizes;
        for (const ScreenSize& s : choices_.screen_sizes)
            sizes.push_back(ascii(std::to_string(s.width) + " x " + std::to_string(s.height)));
        resolution_ = &top.list(str(strings, "GameOptionsDialog_ScreenResolution"), std::move(sizes));
        resolution_->select(choices_.screen_size);
        resolution_->enabled = !choices_.screen_sizes.empty();
        top.choice(O::GraphicsQuality, str(strings, "QualityVsPerformanceLabel"), items(strings, { "QualityVsPerformance_HighPerformance", "QualityVsPerformance_Performance", "QualityVsPerformance_Balanced", "QualityVsPerformance_Quality", "QualityVsPerformance_HighQuality", "QualityVsPerformance_Custom" }));
        graphics_top_ = &top;

        // The game picks a quality level for the computer here; we cannot tell yet, so it shows unusable.
        const float icon_x = style_.page_width - style_.auto_detect_right - style_.auto_detect_size;
        auto_detect_ = &page.add<Button>("auto_detect", std::string { }, style_.auto_detect_icon);
        auto_detect_->tooltip = str(strings, kAutoDetectTip);
        auto_detect_->placement = top_left(icon_x, 0, style_.auto_detect_size, style_.auto_detect_size);
        auto_detect_->enabled = false;
        auto& detect = page.add<Label>("auto_detect_name", str(strings, kAutoDetect), style_.rows.font);
        detect.style.colour = style_.rows.disabled_colour;
        detect.style.horizontal = Align::Centre;
        detect.placement = top_left(icon_x - style_.auto_detect_size / 2, style_.auto_detect_size, 2 * style_.auto_detect_size, style_.rows.row_height);

        // The custom settings scroll below.
        custom_ = &page.add<ScrollArea>("custom", style_.scroll);
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(custom_->content(), style_.page_width - style_.scroll.thickness, style_.rows, options_, choices_.inert));
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
        rows.level(O::DetailBlendBand, str(strings, kBlendBand), str(strings, kBlendBandHelp));
        rows.indent();
        rows.enabled_when({ O::BlendDetailLevels, 1 });
        rows.tick(O::CompressTextures, str(strings, "GameOptionsDialog_TextureCompress"), str(strings, "GameOptionsDialog_TextureCompress_TT"));
        rows.tick(O::AggressiveDepthBias, str(strings, "GameOptionsDialog_AggressiveZBias"), str(strings, "GameOptionsDialog_AggressiveZBiasHelp"));
        rows.level(O::TextureDetail, str(strings, "GameOptionsDialog_TextureLOD"), str(strings, "GameOptionsDialog_TextureLODHelp"));
        rows.section();
        rows.tick(O::Bloom, str(strings, "GameOptionsDialog_Bloom"), str(strings, "GameOptionsDialog_Bloom_TT"));
        rows.tick(O::Refraction, str(strings, "GameOptionsDialog_Refraction"), str(strings, "GameOptionsDialog_Refraction_TT"));
        rows.tick(O::WaterDroplets, str(strings, "GameOptionsDialog_WaterDroplets"), str(strings, "GameOptionsDialog_WaterDroplets_TT"));
        rows.indent();
        rows.shown_when({ O::Refraction, 1 });
        rows.tick(O::LaserOcclusion, str(strings, "GameOptionsDialog_LaserRayHit"), str(strings, "GameOptionsDialog_LaserRayHit_TT"));
    }

    void OptionsDialog::build_scenic(Widget& page, const StringTable& strings)
    {
        // Ours, after the game's manual: the scenery and surroundings, whatever the quality level.
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, style_.rows, options_, choices_.inert));
        rows.tick(O::Trees3D, str(strings, "GameOptionsDialog_3DTreesAndPlants"), str(strings, "GameOptionsDialog_3DTreesAndPlantsHelp"));
        rows.tick(O::OffMapTrees, str(strings, "GameOptionsDialog_SkirtTrees"), str(strings, "GameOptionsDialog_SkirtTreesHelp"));
        rows.tick(O::SimpleTerrain, str(strings, "GameOptionsDialog_UntexturedTerrain"), str(strings, "GameOptionsDialog_UntexturedTerrainHelp"));
        rows.section();
        rows.tick(O::ReflectiveWater, str(strings, "GameOptionsDialog_WaterReflect_Enabled"), str(strings, "GameOptionsDialog_WaterReflect_EnabledHelp"));
        constexpr std::array<std::pair<O, std::string_view>, 6> kReflections = { {
            { O::ReflectBackground, "GameOptionsDialog_WaterReflect_Background" },
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
        rows.section();
        rows.tick(O::Clouds, str(strings, "GameOptionsDialog_CloudsEnabled"), str(strings, kCloudsHelp));
        rows.tick(O::Sky, str(strings, "GameOptionsDialog_SkyEnabled"), str(strings, kSkyHelp));
        rows.tick(O::Mountains, str(strings, "GameOptionsDialog_DistantMountainsEnabled"), str(strings, kMountainsHelp));
        rows.choice(O::DayNight, str(strings, "GameOptionsDialog_FastDayNightCycle"), items(strings, { "GameOptionsDialog_DNC_Original", "GameOptionsDialog_DNC_Blend", "GameOptionsDialog_DNC_Cycle" }), str(strings, "GameOptionsDialog_FastDayNightCycle_TT"));
    }

    void OptionsDialog::build_camera(Widget& page, const StringTable& strings)
    {
        auto& rows = *rows_.emplace_back(std::make_unique<OptionRows>(page, style_.page_width, style_.rows, options_, choices_.inert));
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

        // What the mouse does in the chosen mode; update_camera_help() fills the lines in.
        rows.section();
        for (std::size_t i = 0; i < camera_help_.size(); ++i)
        {
            camera_buttons_.at(i) = str(strings, kCameraButtonKeys.at(i));
            camera_help_.at(i) = &rows.note(camera_buttons_.at(i));
        }
        for (const std::string_view key : kCameraActionKeys)
            camera_actions_.push_back(key.empty() ? std::u16string { } : str(strings, key));
        rows.note(str(strings, kCameraKeysInfo), 2);
        rows.shown_when({ O::CameraMode, kFreelookMode });
        update_camera_help();
    }

    void OptionsDialog::update_camera_help()
    {
        const CameraLines lines = camera_lines(static_cast<int>(options_.choice(O::CameraMode)), static_cast<int>(std::lround(options_.get(O::CameraControls))));
        for (std::size_t i = 0; i < camera_help_.size(); ++i)
        {
            const std::u16string& action = camera_actions_.at(static_cast<std::size_t>(lines.at(i)));
            camera_help_.at(i)->text = action.empty() ? camera_buttons_.at(i) : camera_buttons_.at(i) + u" " + action;
        }
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
        const auto waiting = [](const KeyBox* box) { return box != nullptr && box->waiting(); };
        return std::ranges::any_of(key_boxes_, waiting) || std::ranges::any_of(play_boxes_, waiting);
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
        for (std::size_t i = 0; i < kPlayCommandCount; ++i)
        {
            KeyBox* box = play_boxes_[i];
            if (box != nullptr && box->take_change())
            {
                // Takes the key from any other game command too.
                keys_.bind_play(static_cast<PlayCommand>(i), box->combos.empty() ? std::nullopt : std::optional(box->combos.front()));
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
        for (std::size_t i = 0; i < kPlayCommandCount; ++i)
        {
            if (KeyBox* box = play_boxes_[i])
            {
                const auto& combo = keys_.play_key(static_cast<PlayCommand>(i));
                box->combos = combo ? std::vector { *combo } : std::vector<KeyCombo> { };
            }
        }
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
        update_background_music();
        if (resolution_->take_change() && resolution_->selected())
            picked_screen_size_ = *resolution_->selected();
        update_camera_help();

        const float top = graphics_top_->height() + style_.rows.section_gap;
        custom_->visible = options_.choice(O::GraphicsQuality) == config::kCustomQuality;
        custom_->placement = top_left(0, top, style_.page_width, std::min(style_.scroll_height, style_.page_height - top));
        fit_page();
    }

} // namespace rct3::ui
