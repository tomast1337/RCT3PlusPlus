// SPDX-License-Identifier: GPL-3.0-or-later
//
// lang-audit: how each key of the built-in English strings is reached, to see where a string shows and which
// ones nothing shows. Run it from the repository root, after a build so the strings built in are current.
//
//   lang-audit [--game DIR] [--report FILE]
//
// Every key falls in the first group that fits:
//
//   literal     named in full by a string literal in src/, app/ or tools/
//   prefix      starts with a literal keys are built from ("SceneryItem_" + an asset's name)
//   data        named by the game's data files: an item's text field or a text item of its own
//   own-unused  one of our keys (Rct3pp_) that nothing names
//   game-only   in the game's own key list, but nothing above reaches it
//   unreached   none of the above
//
// The game's groups need an install: DIR, else RCT3PP_GAME_PATH. Without one, data and game-only stay empty.
// Its overlays are read one at a time and only their names are kept.
//
// The summary goes to standard output; --report writes one tab-separated line per key: key, group, the file
// it is in, and where it is reached (file:line, the prefix and its comment, or the overlay naming it).
#include "assets/embedded_files.hpp"
#include "config/environment.hpp"
#include "config/game_install.hpp"
#include "ovl/overlay.hpp"
#include "ovl/text.hpp"
#include "strings/ascii.hpp"
#include "ui/language.hpp"
#include "ui/translation_audit.hpp"
#include "ui/translation_file.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <print>
#include <set>
#include <string>
#include <vector>

using namespace rct3;

namespace
{

    std::string lower(std::string_view text)
    {
        std::string out(text);
        std::ranges::transform(out, out.begin(), strings::ascii_lower);
        return out;
    }

    struct GameKeys
    {
        std::set<std::string, std::less<>> listed;            ///< The game's English text items, lower case
        std::map<std::string, std::string, std::less<>> data; ///< Text names other overlays hold, lower case, to the first overlay
        int unread = 0;
    };

    GameKeys read_game(const config::GameInstall& install)
    {
        GameKeys out;
        if (const auto text = install.find(ui::text_overlay(ui::kDefaultLanguage)))
        {
            if (const auto overlay = ovl::Overlay::load(*text))
            {
                for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
                {
                    for (const ovl::TextItem& item : part ? ovl::read_texts(*overlay, *part) : std::vector<ovl::TextItem> { })
                        out.listed.insert(lower(item.name));
                }
            }
        }
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(install.root(), error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        {
            const std::string name = it->path().filename().string();
            if (!it->is_regular_file() || !lower(name).ends_with(".common.ovl"))
                continue;
            // The language text overlays are the key list, not data naming keys.
            if (lower(it->path().parent_path().filename().string()) == "gui" && ui::find_language(name.substr(0, name.find('.'))))
                continue;
            const auto overlay = ovl::Overlay::load(it->path());
            if (!overlay)
            {
                ++out.unread;
                continue;
            }
            const std::string where = it->path().lexically_relative(install.root()).generic_string();
            const auto space = overlay->space();
            for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
            {
                if (!part)
                    continue;
                for (const ovl::Symbol& s : part->symbols)
                {
                    if (const auto n = ovl::split_symbol_name(s.name); n.tag == "txt")
                        out.data.try_emplace(lower(n.name), where);
                }
                if (const auto refs = ovl::read_references(part->file, part->bytes, space))
                {
                    for (const ovl::Reference& r : *refs)
                    {
                        if (const auto n = ovl::split_symbol_name(r.name); n.tag == "txt")
                            out.data.try_emplace(lower(n.name), where);
                    }
                }
            }
        }
        return out;
    }

} // namespace

int main(int argc, char** argv)
{
    std::optional<std::filesystem::path> game_dir;
    std::optional<std::filesystem::path> report_path;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game_dir = argv[++i];
        else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc)
            report_path = argv[++i];
        else
        {
            std::println(stderr, "usage: lang-audit [--game DIR] [--report FILE]");
            return 2;
        }
    }

    // Each key with the file it is in.
    std::map<std::string, std::string, std::less<>> keys;
    ui::StringTable all;
    const std::string folder = ui::translation_path(ui::kOwnBaseLanguage);
    for (const assets::EmbeddedFile& f : assets::embedded_files())
    {
        if (!f.path.starts_with(folder.substr(0, folder.rfind('.'))) || !f.path.ends_with(".json"))
            continue;
        const auto table = ui::parse_translation(assets::as_text(f.bytes));
        if (!table)
        {
            std::println(stderr, "{}: {}", f.path, table.error());
            return 1;
        }
        for (const std::string& key : table->keys())
            keys.try_emplace(key, f.path);
        all.merge(*table);
    }

    const ui::CodeKeys code = ui::scan_code(".");
    if (code.literals.empty())
    {
        std::println(stderr, "no sources under src/, app/ or tools/: run from the repository root");
        return 1;
    }

    GameKeys game;
    const auto install = game_dir ? config::open_game_install(*game_dir) : config::find_game_install(config::Environment::from_process());
    if (install)
    {
        game = read_game(*install);
        std::println("game: {} keys listed, {} text names in data, {} overlays unread", game.listed.size(), game.data.size(), game.unread);
    }
    else if (game_dir)
    {
        std::println(stderr, "{}", config::describe(install.error()));
        return 1;
    }
    else
        std::println("no game install (--game DIR or RCT3PP_GAME_PATH): data and game-only stay empty");

    constexpr std::array<std::string_view, 6> kGroups = { "literal", "prefix", "data", "own-unused", "game-only", "unreached" };
    std::map<std::string_view, std::map<std::string, int>> by_group; ///< Group, then the key up to its first '_'
    std::ofstream report;
    if (report_path)
        report.open(*report_path);
    for (const auto& [key, file] : keys)
    {
        std::string_view group;
        std::string where;
        const std::string folded = lower(key);
        if (const auto it = code.literals.find(key); it != code.literals.end())
        {
            group = "literal";
            where = std::format("{}:{}", it->second.file, it->second.line);
        }
        else if (const auto p = key.starts_with(ui::kOwnKeyPrefix) ? std::nullopt : ui::prefix_of(code, key))
        {
            group = "prefix";
            where = std::format("{} {}:{} {}", p->text, p->place.file, p->place.line, p->note);
            if (const auto d = game.data.find(folded); d != game.data.end())
                where += std::format(" (data: {})", d->second);
        }
        else if (const auto d = game.data.find(folded); d != game.data.end())
        {
            group = "data";
            where = d->second;
        }
        else if (key.starts_with(ui::kOwnKeyPrefix))
            group = "own-unused";
        else if (game.listed.contains(folded))
            group = "game-only";
        else
            group = "unreached";
        ++by_group[group][key.substr(0, key.find('_'))];
        if (report)
            std::println(report, "{}\t{}\t{}\t{}", key, group, file, where);
    }

    std::println("{} keys", keys.size());
    for (const std::string_view group : kGroups)
    {
        const auto& heads = by_group[group];
        int total = 0;
        std::vector<std::pair<int, std::string>> top;
        for (const auto& [head, count] : heads)
        {
            total += count;
            top.emplace_back(-count, head);
        }
        std::ranges::sort(top);
        std::string line;
        for (std::size_t i = 0; i < std::min<std::size_t>(top.size(), 12); ++i)
            line += std::format(" {} {},", top[i].second, -top[i].first);
        std::println("{:>11} {:>6} {}", group, total, line.empty() ? line : line.substr(0, line.size() - 1));
    }
    const auto missing = ui::missing_keys(code, all);
    for (const auto& [literal, place] : missing)
        std::println("missing: {} at {}:{}", literal, place.file, place.line);
    return missing.empty() ? 0 : 1;
}
