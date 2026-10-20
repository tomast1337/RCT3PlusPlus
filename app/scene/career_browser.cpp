// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/career_browser.hpp"

#include "career/prf_import.hpp"
#include "logging/log.hpp"
#include "save/summary.hpp"
#include "strings/unicode.hpp"
#include "ui/career_scenarios.hpp"
#include "ui/ui_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace rct3::scene
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::string_view kPrfExtension = ".prf";
        constexpr std::string_view kImported = "imported";

        std::int64_t now()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::optional<std::vector<std::byte>> read_bytes(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::nullopt;
            const std::vector<char> chars { std::istreambuf_iterator<char>(in), { } };
            std::vector<std::byte> bytes(chars.size());
            std::memcpy(bytes.data(), chars.data(), chars.size());
            return bytes;
        }

        void free_pictures(render::Device& device, CareerBrowser& browser)
        {
            for (const auto& picture : browser.pictures)
                device.destroy_texture(picture);
            browser.pictures.clear();
        }

        /// The number in a career file's name plus one, as the game numbers its careers' default names.
        std::size_t career_number(const fs::path& path)
        {
            const std::string stem = path.stem().string();
            std::size_t n = 0;
            for (const char c : stem)
            {
                if (c >= '0' && c <= '9')
                    n = n * 10 + static_cast<std::size_t>(c - '0');
            }
            return n + 1;
        }

        /// Brings one of the game's career files over: ours written, its games copied, the old ones moved aside.
        void import_prf(const fs::path& prf_path, const fs::path& folder, const ui::StringTable& strings)
        {
            const auto log = logging::logger("ui");
            const auto bytes = read_bytes(prf_path);
            const auto prf = bytes ? career::read_prf(*bytes) : std::unexpected(career::PrfError::NotASave);
            if (!prf)
            {
                log.warning("careers: {}: {}", prf_path.filename().string(), bytes ? career::to_string(prf.error()) : "cannot read");
                return;
            }
            career::CareerData data;
            data.career = career::career_from_prf(*prf, ui::career_scenario_ids(strings, prf->pack));
            const fs::path path = career::free_career_path(folder);
            std::error_code error;
            const fs::path old_games = fs::path(prf_path).replace_extension();
            if (fs::is_directory(old_games, error))
                fs::copy(old_games, fs::path(path).replace_extension(), fs::copy_options::recursive, error);
            if (error)
            {
                log.warning("careers: copying the games of {}: {}", prf_path.filename().string(), error.message());
                return;
            }
            if (auto saved = career::save_career(path, data); !saved)
            {
                log.warning("careers: {}", saved.error());
                return;
            }
            const fs::path aside = folder / kImported / prf_path.parent_path().lexically_relative(folder);
            fs::create_directories(aside, error);
            fs::rename(prf_path, aside / prf_path.filename(), error);
            if (fs::is_directory(old_games, error))
                fs::rename(old_games, aside / old_games.filename(), error);
            if (error)
                log.warning("careers: moving {} aside: {}", prf_path.filename().string(), error.message());
            log.info("careers: brought {} over as {}", prf_path.filename().string(), path.filename().string());
        }

        /// The game's career files in the folder and the folders in it, other than those already brought over.
        void import_all(const fs::path& folder, const ui::StringTable& strings)
        {
            std::vector<fs::path> found;
            std::error_code error;
            for (auto it = fs::recursive_directory_iterator(folder, error); it != fs::recursive_directory_iterator(); it.increment(error))
            {
                if (error)
                    break;
                if (it->is_directory(error) && (it.depth() > 0 || it->path().filename() == kImported))
                    it.disable_recursion_pending();
                else if (it->is_regular_file(error) && it->path().extension() == kPrfExtension)
                    found.push_back(it->path());
            }
            std::ranges::sort(found);
            for (const fs::path& path : found)
                import_prf(path, folder, strings);
        }

        ui::CareerRow row_of(const career::Career& c)
        {
            return { strings::to_utf16(c.name), career::score(c), 0 };
        }

        void list(render::Device& device, CareerBrowser& browser, ui::CareerDialog& dialog, int pack, const ui::StringTable& strings)
        {
            free_pictures(device, browser);
            browser.ids = ui::career_scenario_ids(strings, pack);
            std::vector<config::FileProblem> problems;
            browser.careers = browser.folder.empty() ? std::vector<career::CareerEntry> { } : career::list_careers(browser.folder, pack, problems);
            for (const auto& problem : problems)
                logging::logger("ui").warning("careers: {}", problem.message);
            std::vector<ui::CareerRow> rows;
            for (auto& entry : browser.careers)
            {
                career::fit(entry.data.career, browser.ids);
                rows.push_back(row_of(entry.data.career));
            }
            browser.next_picture = 0;
            dialog.set_careers(std::move(rows));
        }

        void save(const career::CareerEntry& entry)
        {
            if (auto saved = career::save_career(entry.path, entry.data); !saved)
                logging::logger("ui").warning("careers: {}", saved.error());
        }

        /// The picture of the last game of the career's current scenario, if it has one.
        void read_picture(render::Device& device, CareerBrowser& browser, ui::CareerDialog& dialog)
        {
            if (browser.next_picture >= browser.careers.size())
                return;
            const std::size_t index = browser.next_picture++;
            const career::CareerEntry& entry = browser.careers[index];
            const career::Career& c = entry.data.career;
            if (c.current >= c.scenarios.size() || c.scenarios[c.current].last_save.empty())
                return;
            const fs::path relative = fs::path(c.scenarios[c.current].last_save).lexically_normal();
            if (relative.is_absolute() || relative.empty() || *relative.begin() == "..")
                return; // Only games in the career's own folder
            const auto bytes = read_bytes(fs::path(entry.path).replace_extension() / relative);
            const auto summary = bytes ? save::read_summary(*bytes) : std::unexpected(save::SummaryError::NotASave);
            if (!summary || summary->preview.empty())
                return;
            auto texture = device.create_texture({ save::kPreviewWidth, save::kPreviewHeight, render::TextureFormat::Rgba8, std::as_bytes(std::span(summary->preview)), entry.path.filename().string() });
            if (!texture)
                return;
            browser.pictures.push_back(*texture);
            ui::CareerRow row = row_of(c);
            row.picture = ui::texture_id(*texture);
            dialog.set_career(index, std::move(row));
        }

        bool can_resume(const career::CareerEntry& entry, std::size_t index)
        {
            const career::Career& c = entry.data.career;
            if (index >= c.scenarios.size() || c.scenarios[index].last_save.empty())
                return false;
            const fs::path relative = fs::path(c.scenarios[index].last_save).lexically_normal();
            if (relative.is_absolute() || *relative.begin() == "..")
                return false;
            std::error_code error;
            return fs::is_regular_file(fs::path(entry.path).replace_extension() / relative, error);
        }

        /// Shows the map of career `index`.
        void open_map(CareerBrowser& browser, ui::FrontEnd& front, std::size_t index, const ui::StringTable& strings)
        {
            browser.playing = browser.careers[index];
            const career::Career& c = browser.playing->data.career;
            std::vector<ui::CareerBoardScenario> scenarios;
            for (std::size_t i = 0; i < c.scenarios.size(); ++i)
            {
                ui::CareerBoardScenario s;
                s.info = ui::career_scenario(strings, c.pack, i, c.scenarios[i].id);
                s.status = c.scenarios[i].status;
                s.lock = career::lock(c, i);
                s.can_resume = can_resume(*browser.playing, i);
                scenarios.push_back(std::move(s));
            }
            const ui::CareerBoard& map = front.open_career_board(std::move(scenarios), c.current);
            browser.map_opened = front.career_boards_opened();
            if (map.size() > 0)
                browser.slideshow = ui::career_scenario(strings, c.pack, map.picked(), c.scenarios[map.picked()].id).slideshow;
        }

        /// The park file scenario `index` of the career starts from, if the game has it.
        std::optional<fs::path> scenario_park(const career::Career& c, std::size_t index, const ui::StringTable& strings, const config::GameInstall& install)
        {
            const auto folder = install.find(ui::career_park_folder(strings, c.pack));
            if (!folder || index >= c.scenarios.size())
                return std::nullopt;
            std::vector<fs::path> files;
            std::error_code error;
            for (const auto& entry : fs::directory_iterator(*folder, error))
            {
                if (entry.is_regular_file(error))
                    files.push_back(entry.path());
            }
            std::ranges::sort(files);
            const auto found = ui::find_career_park(files, ui::career_scenario(strings, c.pack, index, c.scenarios[index].id).file_part);
            return found ? std::optional(files[*found]) : std::nullopt;
        }

        void carry_out_map(CareerBrowser& browser, ui::CareerBoard& map, const ui::StringTable& strings, const config::GameInstall& install)
        {
            const auto log = logging::logger("ui");
            career::CareerEntry& entry = *browser.playing;
            career::Career& c = entry.data.career;
            if (const auto picked = map.take_picked(); picked && *picked < c.scenarios.size())
            {
                c.current = *picked;
                save(entry);
                browser.slideshow = ui::career_scenario(strings, c.pack, *picked, c.scenarios[*picked].id).slideshow;
            }
            if (const auto index = map.take_new())
            {
                const auto park = scenario_park(c, *index, strings, install);
                log.info("career map: starting {} from {} does nothing yet", c.scenarios.at(*index).id, park ? park->filename().string() : "no file");
            }
            if (const auto index = map.take_resume())
                log.info("career map: resuming {} does nothing yet", c.scenarios.at(*index).last_save);
            if (map.take_load())
                log.info("career map: loading a game does nothing yet");
        }

        void carry_out(CareerBrowser& browser, ui::CareerDialog& dialog, ui::FrontEnd& front, int pack, const ui::StringTable& strings)
        {
            const auto log = logging::logger("ui");
            if (const auto rename = dialog.take_rename(); rename && rename->first < browser.careers.size())
            {
                career::CareerEntry& entry = browser.careers[rename->first];
                entry.data.career.name = strings::to_utf8(rename->second);
                save(entry);
            }
            if (dialog.take_new() && !browser.folder.empty())
            {
                career::CareerEntry entry;
                entry.path = career::free_career_path(browser.folder);
                entry.data.career = career::new_career(strings::to_utf8(ui::default_career_name(strings, career_number(entry.path))), pack, browser.ids);
                entry.data.career.created = now();
                save(entry);
                dialog.add_career(row_of(entry.data.career));
                browser.careers.push_back(std::move(entry));
            }
            if (const auto index = dialog.take_delete(); index && *index < browser.careers.size())
            {
                if (auto deleted = career::delete_career(browser.careers[*index].path); !deleted)
                    log.warning("careers: {}", deleted.error());
                browser.careers.erase(browser.careers.begin() + static_cast<std::ptrdiff_t>(*index));
                // Rows past it moved up: reading goes on with the same career.
                if (*index < browser.next_picture)
                    --browser.next_picture;
            }
            if (const auto index = dialog.take_load(); index && *index < browser.careers.size())
                open_map(browser, front, *index, strings); // Closes the window
        }

    } // namespace

    void update_career_browser(render::Device& device, CareerBrowser& browser, ui::FrontEnd& front, const ui::StringTable& strings, const config::GameInstall& install)
    {
        auto map = front.career_board();
        if (map && browser.playing && browser.map_opened == front.career_boards_opened())
            carry_out_map(browser, *map, strings, install);
        else if (!map)
        {
            browser.playing.reset();
            browser.slideshow.reset();
        }

        auto dialog = front.career_dialog();
        if (!dialog)
        {
            free_pictures(device, browser);
            browser.careers.clear();
            return;
        }
        if (browser.opened != front.career_dialogs_opened())
        {
            browser.opened = front.career_dialogs_opened();
            browser.folder.clear();
            if (browser.data)
            {
                if (auto made = browser.data->make_folder(config::UserFolder::Careers))
                {
                    browser.folder = std::move(*made);
                    import_all(browser.folder, strings);
                }
                else
                    logging::logger("ui").warning("careers: {}", made.error());
            }
            list(device, browser, *dialog, front.career_pack(), strings);
        }
        read_picture(device, browser, *dialog);
        carry_out(browser, *dialog, front, front.career_pack(), strings);
    }

    void destroy_career_browser(render::Device& device, CareerBrowser& browser)
    {
        free_pictures(device, browser);
        browser = { };
    }

} // namespace rct3::scene
