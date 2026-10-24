// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/save_browser.hpp"

#include "config/game_saves.hpp"
#include "logging/log.hpp"
#include "parkfile/saved_park.hpp"
#include "save/summary.hpp"
#include "strings/unicode.hpp"
#include "ui/ui_renderer.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>
#include <system_error>

namespace rct3::scene
{

    namespace
    {

        namespace fs = std::filesystem;

        bool is_save(const fs::directory_entry& entry)
        {
            std::error_code error;
            if (!entry.is_regular_file(error))
                return false;
            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".dat" || extension == ".park";
        }

        void free_pictures(render::Device& device, SaveBrowser& browser)
        {
            for (const auto& picture : browser.pictures)
                device.destroy_texture(picture);
            browser.pictures.clear();
        }

        /// The saves in `folder`, by name.
        std::vector<fs::path> saves_in(const fs::path& folder)
        {
            std::vector<fs::path> files;
            if (folder.empty())
                return files;
            std::error_code error;
            for (const auto& entry : fs::directory_iterator(folder, error))
            {
                if (is_save(entry))
                    files.push_back(entry.path());
            }
            std::ranges::sort(files, [](const fs::path& a, const fs::path& b) { return a.filename().u16string() < b.filename().u16string(); });
            return files;
        }

        /// Lists the folders again, ours and then the game's, and hands the names to the window, which starts
        /// reading again.
        void list(render::Device& device, SaveBrowser& browser, ui::FileDialog& dialog, const fs::path& game_folder)
        {
            free_pictures(device, browser);
            browser.next = 0;
            browser.files = saves_in(browser.folder);
            browser.kept.assign(browser.files.size(), false);
            for (auto& file : saves_in(game_folder))
            {
                browser.files.push_back(std::move(file));
                browser.kept.push_back(true);
            }
            std::vector<std::u16string> names;
            for (const auto& file : browser.files)
                names.push_back(file.filename().u16string());
            dialog.set_files(std::move(names), browser.kept);
        }

        /// The game's folder for ours, if there is one.
        fs::path game_folder(const SaveBrowser& browser, const ParkGame& game)
        {
            const auto name = config::game_saves_folder(game.folder);
            if (!browser.game_saves || !name || !game.games.empty())
                return { };
            return *browser.game_saves / *name;
        }

        /// Our file of that name; never one of the game's.
        std::optional<fs::path> ours(const SaveBrowser& browser, std::u16string_view name)
        {
            for (std::size_t i = 0; i < browser.files.size(); ++i)
            {
                if (!browser.kept[i] && browser.files[i].filename().u16string() == name)
                    return browser.files[i];
            }
            return std::nullopt;
        }

        std::optional<std::vector<std::byte>> read_bytes(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return std::nullopt;
            std::vector<char> chars { std::istreambuf_iterator<char>(in), { } };
            std::vector<std::byte> bytes(chars.size());
            std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
            return bytes;
        }

        void read_next(render::Device& device, SaveBrowser& browser, ui::FileDialog& dialog)
        {
            if (browser.next >= browser.files.size())
                return;
            const std::size_t index = browser.next++;
            const fs::path& path = browser.files[index];
            std::optional<save::SaveSummary> summary;
            if (const auto bytes = read_bytes(path))
            {
                if (parkfile::is_park_file(*bytes))
                {
                    if (auto read = parkfile::read_saved_park(*bytes))
                        summary = std::move(read->summary);
                    else
                        logging::logger("ui").info("saves: {}: {}", path.filename().string(), read.error());
                }
                else if (auto read = save::read_summary(*bytes))
                    summary = std::move(*read);
                else
                    logging::logger("ui").info("saves: {}: {}", path.filename().string(), save::to_string(read.error()));
            }
            ui::TextureId picture = 0;
            if (summary && !summary->preview.empty())
            {
                const std::string label = path.filename().string();
                auto texture = device.create_texture({ save::kPreviewWidth, save::kPreviewHeight, render::TextureFormat::Rgba8, std::as_bytes(std::span(summary->preview)), label });
                if (texture)
                {
                    browser.pictures.push_back(*texture);
                    picture = ui::texture_id(*texture);
                }
                else
                    logging::logger("ui").warning("saves: {}: {}", label, texture.error());
            }
            dialog.set_summary(index, summary, picture);
        }

        /// Writes beside the file and then puts it in place, so a failed write leaves the old save whole.
        void write_save(const fs::path& path, const std::vector<std::byte>& bytes)
        {
            const auto log = logging::logger("ui");
            fs::path written = path;
            written += ".new";
            {
                std::ofstream out(written, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!out)
                {
                    log.warning("saves: writing {} failed", path.filename().string());
                    return;
                }
            }
            std::error_code error;
            fs::rename(written, path, error);
            if (error)
                log.warning("saves: writing {}: {}", path.filename().string(), error.message());
            else
                log.info("saves: saved {} ({} bytes)", path.filename().string(), bytes.size());
        }

        void carry_out(render::Device& device, SaveBrowser& browser, ui::FileDialog& dialog, const fs::path& game_folder)
        {
            const auto log = logging::logger("ui");
            if (const auto file = dialog.take_delete())
            {
                std::error_code error;
                const auto path = ours(browser, *file);
                if (!path || !fs::remove(*path, error))
                    log.warning("saves: deleting {}: {}", strings::to_utf8(*file), error ? error.message() : "not one of ours");
                list(device, browser, dialog, game_folder);
            }
            if (const auto rename = dialog.take_rename())
            {
                const auto from = ours(browser, rename->first);
                const fs::path to = browser.folder / rename->second;
                std::error_code error;
                const bool taken = !from || (fs::exists(to, error) && !fs::equivalent(*from, to, error));
                if (!taken)
                    fs::rename(*from, to, error);
                if (taken || error)
                {
                    log.warning("saves: renaming {}: {}", strings::to_utf8(rename->first), taken ? "the name is taken" : error.message());
                    dialog.rename_failed();
                }
                else
                    list(device, browser, dialog, game_folder);
            }
        }

    } // namespace

    std::optional<fs::path> update_save_browser(render::Device& device, SaveBrowser& browser, std::optional<ui::FileDialog&> dialog, std::size_t opened, const ParkGame& game, const SaveWriter& write)
    {
        if (!dialog)
        {
            free_pictures(device, browser);
            browser.files.clear();
            browser.kept.clear();
            return std::nullopt;
        }
        const fs::path game_saves = game_folder(browser, game);
        if (browser.opened != opened)
        {
            browser.opened = opened;
            browser.folder.clear();
            std::error_code error;
            if (!game.games.empty())
            {
                fs::create_directories(game.games, error);
                if (error)
                    logging::logger("ui").warning("saves: {}: {}", game.games.string(), error.message());
                else
                    browser.folder = game.games;
            }
            else if (browser.data)
            {
                if (auto made = browser.data->make_folder(game.folder))
                    browser.folder = std::move(*made);
                else
                    logging::logger("ui").warning("saves: {}", made.error());
            }
            dialog->set_can_save(write != nullptr && !browser.folder.empty());
            list(device, browser, *dialog, game_saves);
        }
        if (const auto name = dialog->take_open())
        {
            const auto picked = dialog->selected();
            if (dialog->mode() == ui::FileDialogMode::Load && picked && *picked < browser.files.size())
                return browser.files[*picked];
            if (dialog->mode() == ui::FileDialogMode::Save && write && !browser.folder.empty())
            {
                const fs::path path = browser.folder / *name;
                write_save(path, write(path));
            }
            return std::nullopt;
        }
        carry_out(device, browser, *dialog, game_saves);
        read_next(device, browser, *dialog);
        return std::nullopt;
    }

    void destroy_save_browser(render::Device& device, SaveBrowser& browser)
    {
        free_pictures(device, browser);
        browser.files.clear();
        browser.kept.clear();
        browser.next = 0;
    }

} // namespace rct3::scene
