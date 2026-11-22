// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path/styles.hpp"

#include "core/types.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <format>
#include <system_error>

namespace rct3::park::path
{

    namespace
    {

        constexpr usize kGroundNames = 2;
        constexpr usize kNamesPerPiece = 4;
        constexpr usize kPieces = 19;
        /// The longer layout's next four slots name no piece; the capped pieces follow them, in DeckPiece's order.
        constexpr usize kCappedNamesAt = kGroundNames + (kPieces + 1) * kNamesPerPiece;
        constexpr usize kCappedPieces = 16;
        constexpr std::string_view kCommonEnding = ".common.ovl";

        units::Money first_cost(const std::vector<ovl::SceneryItem>& items)
        {
            return units::Money { items.empty() ? 0 : items.front().cost };
        }

        std::string first_group(const std::vector<ovl::SceneryItem>& items)
        {
            const auto named = std::ranges::find_if(items, [](const ovl::SceneryItem& i) { return !i.group.empty(); });
            return named != items.end() ? std::string(named->group) : std::string();
        }

    } // namespace

    PathStyle path_style(const ovl::PathType& type)
    {
        PathStyle style { std::string(type.name), std::string(type.name_text), std::string(type.icon), first_cost(type.items), false, { }, { }, first_group(type.items) };
        style.names_category = !type.research.empty();
        for (usize at = kGroundNames; at < type.piece_names.size() && style.pieces.size() < kPieces; at += kNamesPerPiece)
            style.pieces.emplace_back(type.piece_names[at]);
        for (usize at = kCappedNamesAt; at < type.piece_names.size() && style.pieces.size() < kPieces + kCappedPieces; at += kNamesPerPiece)
            style.pieces.emplace_back(type.piece_names[at]);
        return style;
    }

    PathStyle queue_style(const ovl::QueueType& type)
    {
        PathStyle style { std::string(type.name), std::string(type.name_text), std::string(type.icon), first_cost(type.items), true, { }, { }, first_group(type.items) };
        style.names_category = !type.research.empty();
        for (const std::string_view name : type.piece_names)
            style.pieces.emplace_back(name);
        return style;
    }

    std::vector<PathStyle> path_styles(const ovl::Overlay& overlay)
    {
        std::vector<PathStyle> paths;
        std::vector<PathStyle> queues;
        for (const auto part : { core::maybe(&overlay.common()), overlay.unique() })
        {
            if (!part)
                continue;
            for (const ovl::PathTypeEntry& e : ovl::read_path_types(overlay, *part))
            {
                if (e.path)
                {
                    // Some stubs leave the type's own name empty; the symbol names the style then.
                    paths.push_back(path_style(*e.path));
                    if (paths.back().name.empty())
                        paths.back().name = std::string(e.name);
                }
            }
            for (const ovl::QueueTypeEntry& e : ovl::read_queue_types(overlay, *part))
            {
                if (e.queue)
                {
                    queues.push_back(queue_style(*e.queue));
                    if (queues.back().name.empty())
                        queues.back().name = std::string(e.name);
                }
            }
        }
        paths.insert(paths.end(), std::make_move_iterator(queues.begin()), std::make_move_iterator(queues.end()));
        return paths;
    }

    std::vector<PathStyle> folder_path_styles(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> folders;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        {
            if (entry.is_directory(error))
                folders.push_back(entry.path());
        }
        std::ranges::sort(folders);
        std::vector<PathStyle> styles;
        for (const std::filesystem::path& style : folders)
        {
            // Told by what the files hold, not by their names: a downloaded set may keep several styles in one
            // folder, under names of its own.
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(style, error))
            {
                const std::string name = entry.path().filename().string();
                const bool common = name.size() > kCommonEnding.size() && strings::equals_ignoring_case(std::string_view(name).substr(name.size() - kCommonEnding.size()), kCommonEnding);
                if (common && (ovl::lists_loader(entry.path(), ovl::kPathTypeTag) || ovl::lists_loader(entry.path(), ovl::kQueueTypeTag)))
                    files.push_back(entry.path());
            }
            std::ranges::sort(files);
            for (const std::filesystem::path& path : files)
            {
                if (const auto overlay = ovl::Overlay::load(path))
                {
                    std::vector<PathStyle> found = path_styles(*overlay);
                    std::string file = path.filename().string();
                    file.resize(file.size() - kCommonEnding.size());
                    for (PathStyle& s : found)
                    {
                        if (s.name.empty())
                            s.name = style.filename().string();
                        s.overlay = std::format("{}/{}/{}", folder.filename().string(), style.filename().string(), file);
                    }
                    styles.insert(styles.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
                }
            }
        }
        return styles;
    }

} // namespace rct3::park::path
