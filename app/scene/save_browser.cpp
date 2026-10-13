// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/save_browser.hpp"

#include "logging/log.hpp"
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

        constexpr std::string_view kExtension = ".dat";

        bool is_save(const fs::directory_entry& entry)
        {
            std::error_code error;
            if (!entry.is_regular_file(error))
                return false;
            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == kExtension;
        }

        void free_pictures(render::Device& device, SaveBrowser& browser)
        {
            for (const auto& picture : browser.pictures)
                device.destroy_texture(picture);
            browser.pictures.clear();
        }

        /// Lists the folder again and hands the names to the window, which starts reading again.
        void list(render::Device& device, SaveBrowser& browser, ui::FileDialog& dialog)
        {
            free_pictures(device, browser);
            browser.files.clear();
            browser.next = 0;
            std::error_code error;
            for (const auto& entry : fs::directory_iterator(browser.folder, error))
            {
                if (is_save(entry))
                    browser.files.push_back(entry.path());
            }
            std::ranges::sort(browser.files, [](const fs::path& a, const fs::path& b) { return a.filename().u16string() < b.filename().u16string(); });
            std::vector<std::u16string> names;
            for (const auto& file : browser.files)
                names.push_back(file.filename().u16string());
            dialog.set_files(std::move(names));
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
                if (auto read = save::read_summary(*bytes))
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

        void carry_out(render::Device& device, SaveBrowser& browser, ui::FileDialog& dialog)
        {
            const auto log = logging::logger("ui");
            if (const auto file = dialog.take_delete())
            {
                std::error_code error;
                if (!fs::remove(browser.folder / *file, error))
                    log.warning("saves: deleting {}: {}", strings::to_utf8(*file), error ? error.message() : "not there");
                list(device, browser, dialog);
            }
            if (const auto rename = dialog.take_rename())
            {
                const fs::path from = browser.folder / rename->first;
                const fs::path to = browser.folder / rename->second;
                std::error_code error;
                const bool taken = fs::exists(to, error) && !fs::equivalent(from, to, error);
                if (!taken)
                    fs::rename(from, to, error);
                if (taken || error)
                {
                    log.warning("saves: renaming {}: {}", from.filename().string(), taken ? "the name is taken" : error.message());
                    dialog.rename_failed();
                }
                else
                    list(device, browser, dialog);
            }
        }

    } // namespace

    void update_save_browser(render::Device& device, SaveBrowser& browser, ui::FrontEnd& front)
    {
        ui::FileDialog* dialog = front.file_dialog();
        if (dialog == nullptr)
        {
            free_pictures(device, browser);
            browser.files.clear();
            return;
        }
        if (browser.opened != front.file_dialogs_opened())
        {
            browser.opened = front.file_dialogs_opened();
            browser.folder.clear();
            if (browser.data)
            {
                if (auto made = browser.data->make_folder(front.file_folder()))
                    browser.folder = std::move(*made);
                else
                    logging::logger("ui").warning("saves: {}", made.error());
            }
            list(device, browser, *dialog);
        }
        if (const auto file = dialog->take_open())
        {
            logging::logger("ui").info("saves: loading {} does nothing yet", strings::to_utf8(*file));
            return;
        }
        carry_out(device, browser, *dialog);
        read_next(device, browser, *dialog);
    }

    void destroy_save_browser(render::Device& device, SaveBrowser& browser)
    {
        free_pictures(device, browser);
        browser = { };
    }

} // namespace rct3::scene
