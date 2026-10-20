// SPDX-License-Identifier: GPL-3.0-or-later
//
// The JSON the configuration files are written in. Each file is one object; its members keep the order they
// are written in, so a file reads in the order of the table it comes from. Included by source files only:
// the JSON library is large.
#pragma once

#include "config/text_file.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::config
{

    using Json = nlohmann::ordered_json;

    /// The object `text` holds; empty text is an empty object. Nothing, with a problem reported, when it is
    /// not JSON or not an object.
    [[nodiscard]] std::optional<Json> parse_json_object(std::string_view text, std::optional<std::vector<FileProblem>&> problems = std::nullopt);

    /// Indented, ending in a line break.
    [[nodiscard]] std::string json_text(const Json& json);

    void report(std::optional<std::vector<FileProblem>&> problems, std::string message);

} // namespace rct3::config
