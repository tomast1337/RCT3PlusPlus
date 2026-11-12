// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/improvements.hpp"

#include "assets/embedded_files.hpp"
#include "config/json_text.hpp"
#include "logging/log.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <format>

namespace rct3::park::track
{

    std::expected<std::vector<Improvement>, std::string> parse_improvements(std::string_view json)
    {
        const auto root = config::parse_json_object(json);
        if (!root)
            return std::unexpected("not a JSON object");
        const auto list = root->find("improvements");
        if (list == root->end() || !list->is_array())
            return std::unexpected("needs a list of improvements");
        std::vector<Improvement> out;
        for (const config::Json& e : *list)
        {
            const std::string what = std::format("improvement {}", out.size() + 1);
            if (!e.is_object() || !e.contains("id") || !e["id"].is_string() || e["id"].get<std::string>().empty())
                return std::unexpected(std::format("{}: needs an id", what));
            Improvement& i = out.emplace_back(Improvement { .id = e["id"].get<std::string>() });
            if (std::ranges::count(out, i.id, &Improvement::id) > 1)
                return std::unexpected(std::format("{}: {} given twice", what, i.id));
            if (!e.contains("name") || !e["name"].is_string())
                return std::unexpected(std::format("{}: needs a name", i.id));
            i.name = e["name"].get<std::string>();
            if (!e.contains("cost") || !e["cost"].is_number_integer() || e["cost"].get<std::int64_t>() < 0)
                return std::unexpected(std::format("{}: needs a cost of nought or more, in hundredths", i.id));
            i.cost = e["cost"].get<std::int64_t>();
            if (!e.contains("pieces") || !e["pieces"].is_array() || e["pieces"].empty())
                return std::unexpected(std::format("{}: needs the words of its pieces", i.id));
            for (const config::Json& word : e["pieces"])
            {
                if (!word.is_string() || word.get<std::string>().empty())
                    return std::unexpected(std::format("{}: a piece word is not text", i.id));
                std::string lower = word.get<std::string>();
                std::ranges::transform(lower, lower.begin(), strings::ascii_lower);
                i.pieces.push_back(std::move(lower));
            }
        }
        return out;
    }

    std::span<const Improvement> builtin_improvements()
    {
        // Parsed once: the embedded file never changes while the program runs.
        static const std::vector<Improvement> table = [] {
            const auto file = assets::embedded_file("simulation/ride_improvements.json");
            auto parsed = file ? parse_improvements(assets::as_text(*file)) : std::unexpected(std::string("not built in"));
            if (!parsed)
                logging::logger("park").error("ride improvements: {}", parsed.error());
            return parsed.value_or(std::vector<Improvement> { });
        }();
        return table;
    }

    std::optional<const Improvement&> locking_improvement(std::span<const Improvement> table, std::span<const std::string> locked, const PieceShape& shape)
    {
        if (locked.empty() || shape.use != PieceUse::Special)
            return std::nullopt;
        std::string name = shape.record.name;
        std::ranges::transform(name, name.begin(), strings::ascii_lower);
        for (const Improvement& i : table)
        {
            if (std::ranges::contains(locked, i.id) && std::ranges::any_of(i.pieces, [&](const std::string& word) { return name.contains(word); }))
                return i;
        }
        return std::nullopt;
    }

} // namespace rct3::park::track
