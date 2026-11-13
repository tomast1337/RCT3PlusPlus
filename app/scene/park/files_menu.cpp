// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/files_menu.hpp"

#include "career/career_file.hpp"
#include "logging/log.hpp"
#include "parkfile/structure_file.hpp"
#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/screens/about_window.hpp"
#include "ui/screens/file_dialog.hpp"
#include "ui/widgets/question_box.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kAboutName = "about";
        constexpr std::string_view kFilesName = "park_files";
        constexpr std::string_view kStructureFilesName = "structure_files";
        constexpr std::string_view kLoadQuestionName = "load_question"; ///< Asks whether to save before another game is loaded
        constexpr std::string_view kLoadQuestionTitle = "FSM_SaveBeforeLoad_Title";
        constexpr std::string_view kLoadQuestionText = "FSM_SaveBeforeLoad_Message";
        constexpr std::array<std::string_view, 3> kLoadAnswers { "SMB_Label_Yes", "SMB_Label_No", "SMB_Label_Cancel" };
        constexpr std::u16string_view kStructureName = u"MyBuilding"; ///< Offered for a new structure, as in the game
        /// A new sandbox starts from the blank landscape in the game's folder; its first save is offered this name.
        constexpr std::string_view kBlankLandscape = "BlankLandscape";
        constexpr std::u16string_view kNewSandbox = u"New Sandbox";
        /// An add-on pack is installed when its files are; numbered as the game numbers them.
        constexpr std::array<std::string_view, 2> kPackFiles = { "ContentPacks/ContentPack0000.common.ovl", "ContentPacks/ContentPack0001.common.ovl" };

        void open_about(ParkScene& park, const FrontEndScene& front)
        {
            ui::Widget& root = park.desktop->root();
            if (auto shown = root.find(kAboutName))
            {
                root.raise(*shown);
                return;
            }
            ui::screens::AddOnPacks packs;
            for (std::size_t i = 0; i < kPackFiles.size(); ++i)
            {
                if (front.assets.install && front.assets.install->find(kPackFiles.at(i)))
                    packs.push_back(static_cast<int>(i));
            }
            root.add<ui::screens::GameInfoBox>(std::string(kAboutName), skins::about_window_style(front.assets.palette), *front.assets.strings, packs, park.seed);
        }

        void open_options(ParkScene& park, FrontEndScene& front)
        {
            if (!park.options)
            {
                // What the front end's window offers, its keys and screen size as they are now.
                ui::screens::OptionsChoices choices = front.live->front->options().choices();
                choices.language_locked = true;
                park.options = std::make_unique<ui::screens::OptionsSession>(*park.desktop, skins::options_dialog_style(front.assets.palette), *front.assets.strings, front.live->options, std::move(choices));
            }
            park.options->open();
        }

        std::optional<ui::screens::FileDialog&> file_dialog(const ParkScene& park)
        {
            return park.desktop->root().find_as<ui::screens::FileDialog>(kFilesName);
        }

        void open_files(ParkScene& park, const FrontEndScene& front, ui::screens::FileDialogMode mode)
        {
            ui::Widget& root = park.desktop->root();
            if (auto shown = file_dialog(park))
            {
                if (shown->mode() == mode && !shown->closing())
                {
                    root.raise(*shown);
                    return;
                }
                root.remove(*shown);
            }
            park.saves.data = front.saves.data;
            park.saves.game_saves = front.saves.game_saves;
            ++park.file_windows_opened;
            auto& dialog = root.add<ui::screens::FileDialog>(std::string(kFilesName), skins::file_dialog_style(front.assets.palette), *front.assets.strings, mode, front.assets.fonts.language == ui::kDefaultLanguage);
            dialog.set_file_name(park.file.stem() == kBlankLandscape ? std::u16string(kNewSandbox) : park.file.filename().u16string());
        }

        std::optional<ui::screens::FileDialog&> structure_dialog(const ParkScene& park)
        {
            return park.desktop->root().find_as<ui::screens::FileDialog>(kStructureFilesName);
        }

        /// The window that saves the structure picked out, or lists the structures to place one, in place of
        /// another such window.
        /// shortcut: the window shows a structure's file name only, without a picture or its size; give the
        /// rows those once structure files carry a picture.
        void open_structure_files(ParkScene& park, const FrontEndScene& front, ui::screens::FileDialogMode mode)
        {
            ui::Widget& root = park.desktop->root();
            if (auto shown = structure_dialog(park))
                root.remove(*shown);
            park.structures.browser.data = front.saves.data;
            park.structures.browser.game_saves = front.saves.game_saves;
            ++park.structures.windows_opened;
            auto& dialog = root.add<ui::screens::FileDialog>(std::string(kStructureFilesName), skins::file_dialog_style(front.assets.palette), *front.assets.strings, mode, front.assets.fonts.language == ui::kDefaultLanguage);
            dialog.set_file_name(kStructureName);
        }

    } // namespace

    void update_park_windows(render::Device& device, ParkScene& park, FrontEndScene& front)
    {
        if (const auto mode = std::exchange(park.structures.asked, std::nullopt))
            open_structure_files(park, front, *mode);
        const ParkGame structures { .folder = config::UserFolder::Structures };
        if (const auto picked = update_save_browser(device, park.structures.browser, structure_dialog(park), park.structures.windows_opened, structures, [&park](const std::filesystem::path& path) {
                save::model::Structure s = park.structures.structure.value_or(save::model::Structure { });
                s.name = path.stem().string();
                return parkfile::write_structure_file(s);
            }))
            (void)start_structure_placing(park, *picked, front.assets);
        for (const ui::screens::ToolItem item : std::exchange(park.window_requests, { }))
        {
            if (item == ui::screens::ToolItem::About)
                open_about(park, front);
            else if (item == ui::screens::ToolItem::Options)
                open_options(park, front);
            else if (item == ui::screens::ToolItem::Save)
                open_files(park, front, ui::screens::FileDialogMode::Save);
            else if (item == ui::screens::ToolItem::Load && !park.desktop->root().find(kLoadQuestionName))
            {
                // Loading drops the park as it is, so it asks first whether to save it.
                const ui::StringTable& strings = *front.assets.strings;
                std::vector<ui::widgets::QuestionAnswer> answers;
                for (const std::string_view answer : kLoadAnswers)
                    answers.push_back({ std::u16string(strings.get(answer)), { } });
                auto& box = park.desktop->root().add<ui::widgets::QuestionBox>(std::string(kLoadQuestionName), skins::question_box_style(front.assets.palette), std::u16string(strings.get(kLoadQuestionText)), answers);
                box.title = std::u16string(strings.get(kLoadQuestionTitle));
            }
        }
        // Yes opens the save window, no the load window, and cancel neither.
        if (auto question = park.desktop->root().find_as<ui::widgets::QuestionBox>(kLoadQuestionName))
            if (const auto answer = question->take_answer(); answer && *answer < 2)
                open_files(park, front, *answer == 0 ? ui::screens::FileDialogMode::Save : ui::screens::FileDialogMode::Load);
        if (auto picked = update_save_browser(device, park.saves, file_dialog(park), park.file_windows_opened, park.game, [&park](const std::filesystem::path& path) {
                // Saved again, it is offered under this name.
                park.file = path;
                // In a career, the game the scenario resumes from.
                if (!park.game.career.empty())
                {
                    const std::string last_save = path.lexically_relative(park.game.games.parent_path()).generic_string();
                    if (auto changed = career::change_career(
                            park.game.career,
                            [&](career::Career& c) {
                                if (const auto s = std::ranges::find(c.scenarios, park.game.scenario, &career::ScenarioProgress::id); s != c.scenarios.end())
                                    s->last_save = last_save;
                            });
                        !changed)
                        logging::logger("ui").warning("careers: {}", changed.error());
                }
                return save_park(park);
            }))
            park.park_to_load = std::move(picked);
        if (const auto follow = std::exchange(park.footpaths.follow_changed, std::nullopt))
        {
            front.live->options.set(config::game::GameOption::PathCameraFollow, *follow ? 1.0f : 0.0f);
            save_front_end_options(front);
        }
        if (!park.options)
            return;
        park.options->update();
        if (park.options->take_closed())
            save_front_end_options(front);
    }

    FrontEndRequests take_park_window_requests(ParkScene& park, FrontEndScene& front)
    {
        FrontEndRequests r;
        r.park = std::exchange(park.park_to_load, std::nullopt);
        r.game = park.game;
        if (park.options)
        {
            ui::screens::OptionsChoices& shared = front.live->front->options().choices();
            r.screen_size = park.options->take_screen_size();
            if (r.screen_size)
                shared.screen_size = r.screen_size;
            r.keys = park.options->take_keys();
            if (r.keys)
                shared.keys = *r.keys;
        }
        const bool fullscreen = front.live->options.flag(config::game::GameOption::Fullscreen);
        if (fullscreen != front.fullscreen)
        {
            front.fullscreen = fullscreen;
            r.fullscreen = fullscreen;
        }
        return r;
    }

    bool park_waiting_for_keys(const ParkScene& park)
    {
        return park.options != nullptr && park.options->waiting_for_keys();
    }

    void close_park_windows(render::Device& device, ParkScene& park, FrontEndScene& front)
    {
        destroy_save_browser(device, park.saves);
        destroy_save_browser(device, park.structures.browser);
        if (!park.options)
            return;
        park.options->close();
        if (park.options->take_closed())
            save_front_end_options(front);
        park.options.reset();
    }

} // namespace rct3::scene::park
