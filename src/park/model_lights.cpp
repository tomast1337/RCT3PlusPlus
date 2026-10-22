// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/model_lights.hpp"

#include <algorithm>
#include <charconv>
#include <ranges>
#include <string>

namespace rct3::park
{

    namespace
    {
        /// A point named `key` and a number, and the words after the number.
        struct Mark
        {
            int number = 0;
            std::string words; ///< Lower case, from the underscore after the number
        };

        std::optional<Mark> mark_of(std::string_view name, std::string_view key)
        {
            std::string lower(name);
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::size_t at = lower.find(key);
            if (at == std::string::npos)
                return std::nullopt;
            const char* first = lower.data() + at + key.size();
            const char* end = lower.data() + lower.size();
            Mark mark;
            const auto [after, error] = std::from_chars(first, end, mark.number);
            if (error != std::errc { })
                return std::nullopt;
            mark.words.assign(after, end);
            return mark;
        }

        /// The number at the start of `text`, and what follows it.
        std::optional<std::pair<int, std::string_view>> leading_number(std::string_view text)
        {
            int value = 0;
            const auto [after, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc { })
                return std::nullopt;
            return std::pair { value, text.substr(static_cast<std::size_t>(after - text.data())) };
        }

        struct Words
        {
            std::optional<glm::vec3> colour;
            std::optional<int> slot;
            std::optional<float> radius;
        };

        Words read_words(std::string_view words)
        {
            Words out;
            for (const auto part : std::views::split(words, '_'))
            {
                std::string_view word(part);
                while (!word.empty() && word.front() == ' ')
                    word.remove_prefix(1);
                if (word.starts_with("radius"))
                {
                    if (const auto n = leading_number(word.substr(6)))
                        out.radius = n->second.starts_with("cm") ? static_cast<float>(n->first) / 100 : static_cast<float>(n->first);
                }
                else if (word.starts_with("flexi"))
                {
                    if (const auto n = leading_number(word.substr(5)); n && n->first >= 0 && n->first <= 2)
                        out.slot = n->first;
                }
                else if (word.starts_with('r'))
                {
                    const auto r = leading_number(word.substr(1));
                    const auto g = r && r->second.starts_with('g') ? leading_number(r->second.substr(1)) : std::nullopt;
                    const auto b = g && g->second.starts_with('b') ? leading_number(g->second.substr(1)) : std::nullopt;
                    if (b)
                        out.colour = glm::clamp(glm::vec3(static_cast<float>(r->first), static_cast<float>(g->first), static_cast<float>(b->first)) / 255.0f, 0.0f, 1.0f);
                }
            }
            return out;
        }

        glm::vec3 position_of(const NamedPlace& p)
        {
            return glm::vec3(p.place[3]);
        }
    } // namespace

    std::vector<ModelLight> model_lights(std::span<const NamedPlace> places)
    {
        std::vector<std::pair<int, ModelLight>> lights;
        for (const NamedPlace& start : places)
        {
            const auto mark = mark_of(start.name, "lightstart");
            if (!mark)
                continue;
            Words words = read_words(mark->words);
            std::optional<float> reach;
            for (const NamedPlace& other : places)
            {
                if (const auto end = mark_of(other.name, "lightend"); end && end->number == mark->number && !reach)
                    reach = glm::distance(position_of(start), position_of(other));
                if (const auto colour = mark_of(other.name, "lightcolour"); colour && colour->number == mark->number && !words.colour && !words.slot)
                {
                    const Words named = read_words(colour->words);
                    words.colour = named.colour;
                    words.slot = named.slot;
                }
            }
            const float radius = reach.value_or(words.radius.value_or(0));
            if (radius <= 0)
                continue;
            lights.emplace_back(mark->number, ModelLight { .position = position_of(start), .colour = words.colour.value_or(glm::vec3(1.0f)), .colour_slot = words.slot, .radius = radius, .bone = start.bone });
        }
        std::ranges::stable_sort(lights, { }, &std::pair<int, ModelLight>::first);
        std::vector<ModelLight> out;
        for (auto& l : lights)
            out.push_back(l.second);
        return out;
    }

} // namespace rct3::park
