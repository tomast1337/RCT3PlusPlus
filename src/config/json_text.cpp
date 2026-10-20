// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/json_text.hpp"

#include <algorithm>

namespace rct3::config
{

    std::optional<Json> parse_json_object(std::string_view text, std::optional<std::vector<FileProblem>&> problems)
    {
        if (std::ranges::all_of(text, [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }))
            return Json::object();
        Json json = Json::parse(text, nullptr, false);
        if (json.is_discarded())
        {
            report(problems, "not JSON");
            return std::nullopt;
        }
        if (!json.is_object())
        {
            report(problems, "expected an object of names and values");
            return std::nullopt;
        }
        return json;
    }

    std::string json_text(const Json& json)
    {
        return json.dump(4) + '\n';
    }

    void report(std::optional<std::vector<FileProblem>&> problems, std::string message)
    {
        if (problems)
            problems->push_back({ std::move(message) });
    }

} // namespace rct3::config
