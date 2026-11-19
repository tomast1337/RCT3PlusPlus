// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/text_layout.hpp"

#include "core/types.hpp"

#include <cstddef>

namespace rct3::ui
{

    namespace
    {

        f32 advance(const font::Font& font, char16_t c)
        {
            const auto g = font.find(c);
            return !g ? 0 : g->advance;
        }

        f32 offset(Align align, f32 space, f32 used)
        {
            switch (align)
            {
                case Align::Start:
                    return 0;
                case Align::Centre:
                    return (space - used) / 2;
                case Align::End:
                    return space - used;
            }
            return 0;
        }

        // Chinese and Japanese characters, their punctuation and full-width forms: lines may break between
        // them. Korean is left out; it breaks at spaces.
        bool is_wide(char16_t c)
        {
            const bool hangul_letters = c >= 0x3130 && c <= 0x318F;
            return (c >= 0x2E80 && c <= 0x9FFF && !hangul_letters) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
        }

        // Punctuation that must not start a line.
        bool is_closing(char16_t c)
        {
            constexpr std::u16string_view kClosing = u"、。，．！？：；）〕］｝〉》」』】〙〗〟’”…ー々ゝゞヽヾ％＞!%),.:;>?]}";
            return kClosing.find(c) != std::u16string_view::npos;
        }

        // Punctuation that must not end a line.
        bool is_opening(char16_t c)
        {
            constexpr std::u16string_view kOpening = u"（〔［｛〈《「『【〘〖〝‘“＜([{<";
            return kOpening.find(c) != std::u16string_view::npos;
        }

        // Whether a line may break between `before` and `after` without a space.
        bool breaks_between(char16_t before, char16_t after)
        {
            return (is_wide(before) || is_wide(after)) && !is_closing(after) && !is_opening(before);
        }

        // Width without trailing spaces, which a line ending in a break should not count.
        TextLine trimmed(const font::Font& font, std::u16string_view text)
        {
            while (!text.empty() && text.back() == u' ')
                text.remove_suffix(1);
            return { text, text_width(font, text) };
        }

    } // namespace

    f32 text_width(const font::Font& font, std::u16string_view text)
    {
        f32 width = 0;
        for (const char16_t c : text)
            width += advance(font, c);
        return width;
    }

    std::vector<TextLine> break_lines(const font::Font& font, std::u16string_view text, f32 max_width)
    {
        std::vector<TextLine> lines;
        usize start = 0;
        while (true)
        {
            const usize feed = text.find(u'\n', start);
            const std::u16string_view paragraph = text.substr(start, feed == std::u16string_view::npos ? std::u16string_view::npos : feed - start);

            // Greedy fill: remember the last place a break is allowed and go back to it when the line is full.
            usize line = 0;
            usize last_break = std::u16string_view::npos; // Index where the next line would start
            f32 width = 0;
            for (usize i = 0; i < paragraph.size(); ++i)
            {
                const char16_t c = paragraph[i];
                const f32 a = advance(font, c);
                if (i > line && breaks_between(paragraph[i - 1], c))
                    last_break = i;
                if (max_width > 0 && width + a > max_width && c != u' ' && i > line)
                {
                    const usize next = last_break != std::u16string_view::npos && last_break > line ? last_break : i;
                    lines.push_back(trimmed(font, paragraph.substr(line, next - line)));
                    line = next;
                    while (line < paragraph.size() && paragraph[line] == u' ')
                        ++line;
                    last_break = std::u16string_view::npos;
                    width = text_width(font, paragraph.substr(line, i - line));
                }
                width += a;
                if (c == u' ' || c == u'-')
                    last_break = i + 1;
            }
            lines.push_back(trimmed(font, paragraph.substr(line)));

            if (feed == std::u16string_view::npos)
                break;
            start = feed + 1;
        }
        return lines;
    }

    void draw_text(DrawList& list, const font::Font& font, std::u16string_view text, const Rect& box, const TextStyle& style)
    {
        const std::vector<TextLine> lines = break_lines(font, text, style.wrap ? box.width() : 0);
        const f32 height = font.line_height() * static_cast<f32>(lines.size());
        f32 top = box.y0 + offset(style.vertical, box.height(), height);
        for (const TextLine& line : lines)
        {
            const f32 baseline = top + font.ascent();
            f32 pen = box.x0 + offset(style.horizontal, box.width(), line.width);
            for (const char16_t c : line.text)
            {
                const auto g = font.find(c);
                if (!g)
                    continue;
                if (!g->box.empty())
                    list.image({ pen + g->box.x0, baseline + g->box.y0, pen + g->box.x1, baseline + g->box.y1 }, g->page, g->uv, style.colour);
                pen += g->advance;
            }
            top += font.line_height();
        }
    }

} // namespace rct3::ui
