// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/truetype_font.hpp"

#include "ui/glyph_atlas.hpp"

#include <cmath>
#include <fstream>
#include <iterator>
#include <stb/stb_truetype.h>

namespace rct3::ui
{

    namespace
    {

        std::expected<stbtt_fontinfo, std::string> open(const std::vector<std::uint8_t>& data)
        {
            if (data.size() < 12)
                return std::unexpected("not a TrueType file");
            const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
            stbtt_fontinfo info { };
            if (offset < 0 || stbtt_InitFont(&info, data.data(), offset) == 0)
                return std::unexpected("not a TrueType file");
            return info;
        }

    } // namespace

    std::expected<TrueTypeFile, std::string> read_truetype(std::vector<std::uint8_t> data)
    {
        const auto info = open(data);
        if (!info)
            return std::unexpected(info.error());
        int ascent = 0;
        int descent = 0;
        int gap = 0;
        stbtt_GetFontVMetrics(&*info, &ascent, &descent, &gap);
        const float per_unit = stbtt_ScaleForMappingEmToPixels(&*info, 1);
        if (!(per_unit > 0) || ascent <= descent)
            return std::unexpected("TrueType file without usable line metrics");
        TrueTypeFile file { std::move(data), static_cast<float>(ascent) * per_unit, static_cast<float>(-descent) * per_unit, 0 };
        file.cap_per_em = file.ascent_per_em;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        if (stbtt_GetCodepointBox(&*info, 'H', &x0, &y0, &x1, &y1) != 0 && y1 > 0)
            file.cap_per_em = static_cast<float>(y1) * per_unit;
        return file;
    }

    std::expected<TrueTypeFile, std::string> load_truetype(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::unexpected("cannot open " + path.string());
        std::vector<std::uint8_t> data { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        return read_truetype(std::move(data));
    }

    std::expected<BakedFont, std::string> bake_truetype(const TrueTypeFile& file, float em_pixels, std::span<const CodeRange> ranges, const BakeOptions& options)
    {
        const std::uint32_t page_side = options.page_side;
        if (!(em_pixels > 0) || page_side == 0)
            return std::unexpected("bad font size or page size");
        auto info = open(file.data);
        if (!info)
            return std::unexpected(info.error());
        const float scale = stbtt_ScaleForMappingEmToPixels(&*info, em_pixels);

        BakedFont out;
        out.line_height = std::round((file.ascent_per_em + file.descent_per_em) * em_pixels);
        out.descent = std::round(file.descent_per_em * em_pixels);
        std::vector<CoverageBitmap> bitmaps;
        std::vector<AtlasSize> sizes;
        for (const CodeRange& range : ranges)
        {
            for (std::uint32_t code = range.first; code <= range.last; ++code)
            {
                const int index = stbtt_FindGlyphIndex(&*info, static_cast<int>(code));
                if (index == 0)
                    continue;
                int advance = 0;
                int bearing = 0;
                stbtt_GetGlyphHMetrics(&*info, index, &advance, &bearing);
                int x0 = 0;
                int y0 = 0;
                int x1 = 0;
                int y1 = 0;
                stbtt_GetGlyphBitmapBox(&*info, index, scale, scale, &x0, &y0, &x1, &y1);

                CoverageBitmap bitmap;
                if (x1 > x0 && y1 > y0)
                {
                    bitmap.width = static_cast<std::uint32_t>(x1 - x0);
                    bitmap.height = static_cast<std::uint32_t>(y1 - y0);
                    bitmap.coverage.assign(std::size_t { bitmap.width } * bitmap.height, 0);
                    stbtt_MakeGlyphBitmap(&*info, bitmap.coverage.data(), x1 - x0, y1 - y0, x1 - x0, scale, scale, index);
                    if (options.hard_edges)
                    {
                        for (std::uint8_t& v : bitmap.coverage)
                            v = v >= 128 ? 255 : 0;
                    }
                }
                BakedGlyph glyph;
                glyph.code = static_cast<char16_t>(code);
                glyph.advance = std::round(static_cast<float>(advance) * scale);
                if (bitmap.width > 0)
                    glyph.box = { static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1), static_cast<float>(y1) };
                out.glyphs.push_back(glyph);
                sizes.push_back({ bitmap.width, bitmap.height });
                bitmaps.push_back(std::move(bitmap));
            }
        }

        const auto slots = pack_atlas(sizes, page_side);
        const std::uint32_t page_count = atlas_page_count(slots);
        const auto side = static_cast<float>(page_side);
        for (std::size_t i = 0; i < out.glyphs.size(); ++i)
        {
            BakedGlyph& g = out.glyphs[i];
            const AtlasSlot& s = slots[i];
            if (s.page >= page_count || g.box.empty())
            {
                g.box = { }; // Too big for a page: advances but draws nothing
                continue;
            }
            g.page = s.page;
            const auto x = static_cast<float>(s.x);
            const auto y = static_cast<float>(s.y);
            g.uv = { x / side, y / side, (x + g.box.width()) / side, (y + g.box.height()) / side };
        }
        out.pages = atlas_pages(bitmaps, slots, page_side);
        return out;
    }

    Font font_from_baked(const BakedFont& baked, std::span<const Font::Page> pages)
    {
        std::vector<CodedGlyph> glyphs;
        glyphs.reserve(baked.glyphs.size());
        for (const BakedGlyph& b : baked.glyphs)
        {
            CodedGlyph g { b.code, { } };
            g.glyph.advance = b.advance;
            if (b.page < pages.size() && !b.box.empty())
            {
                g.glyph.box = b.box;
                g.glyph.uv = b.uv;
                g.glyph.page = pages[b.page].texture;
            }
            glyphs.push_back(g);
        }
        return Font::from_glyphs(glyphs, baked.line_height, baked.descent);
    }

} // namespace rct3::ui
