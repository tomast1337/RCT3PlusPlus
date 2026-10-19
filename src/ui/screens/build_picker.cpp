// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/build_picker.hpp"

#include "strings/unicode.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        using namespace build_picker;

        constexpr std::string_view kNothing = "NoItemsMeetFilterCriteria";
        constexpr std::string_view kSearchHint = "Rct3pp_BuildPicker_Search";
        constexpr std::string_view kPackPrefix = "pack";

        /// The kinds and add-on packs of `rows`.
        void collect(const std::vector<PickerRow>& rows, std::set<std::string, std::less<>>& kinds, std::set<std::uint32_t>& packs)
        {
            for (const PickerRow& r : rows)
            {
                if (!r.kind.empty())
                    kinds.insert(r.kind);
                if (r.addon != 0 && r.children.empty())
                    packs.insert(r.addon);
                collect(r.children, kinds, packs);
            }
        }

    } // namespace

    BuildPicker::BuildPicker(std::string name, BuildPickerStyle style, const StringTable& strings, std::u16string title_text, std::string icon_skin, std::vector<PickerRow> rows, std::vector<PickerTheme> themes)
        : Window(std::move(name), style.window, kWidth, kHeight)
        , style_(std::move(style))
        , strings_(strings)
        , data_(std::move(rows))
    {
        title = std::move(title_text);
        icon = std::move(icon_skin);
        resizable = true;
        min_size = { kCaptionX + 3 * kTile, kHeight / 2 };
        Widget& body = content();

        search_ = &body.add<EditBox>("search");
        search_->placement = { { 0, 0, 1, 0 }, { kCaptionX, 0, 0, kBarHeight } };
        search_->skin = style_.search_box;
        search_->focused_skin = style_.search_box_focused;

        add_filters(themes);
        const Rect& inset = style_.window.content_inset;
        if (!filters_.empty())
            min_size.x = std::max(min_size.x, filters_.back()->placement.offsets.x1 + inset.x0 + inset.x1);
        const float top = kBarHeight + kGap + (filters_.empty() ? 0 : kFilter + kGap);
        list_ = &body.add<ScrollArea>("list", style_.scroll_bar);
        list_->placement = { { 0, 0, 1, 1 }, { 0, top, 0, 0 } };

        add_rows(data_, nullptr);
    }

    void BuildPicker::add_filters(const std::vector<PickerTheme>& themes)
    {
        std::set<std::string, std::less<>> kinds;
        std::set<std::uint32_t> packs;
        collect(data_, kinds, packs);
        float x = 0;
        const auto add = [&](FilterKey key, std::u16string help) -> Button& {
            auto& b = content().add<Button>(key.group == FilterKey::Group::Pack ? std::string(kPackPrefix) + std::to_string(key.pack - 1) : key.name, std::string { });
            b.toggle = true;
            b.tooltip = std::move(help);
            b.placement = top_left(x, kBarHeight + kGap, kFilter, kFilter);
            x += kFilter;
            filters_.push_back(&b);
            keys_.push_back(std::move(key));
            return b;
        };
        // A group of one filter would only hide everything; it is left out.
        if (kinds.size() > 1)
        {
            for (const std::string& kind : kinds)
                add({ FilterKey::Group::Object, kind }, std::u16string(strings_.get(style_.object_help + kind)));
            x += kGroupGap;
        }
        if (themes.size() > 1)
        {
            const std::u16string help(strings_.get(style_.style_help));
            for (const PickerTheme& theme : themes)
                add({ FilterKey::Group::Theme, theme.name }, theme.title + u'\n' + help);
            x += kGroupGap;
        }
        for (const std::uint32_t pack : packs)
        {
            const std::string number = std::to_string(pack - 1);
            Button& b = add({ FilterKey::Group::Pack, { }, pack }, std::u16string(strings_.get(style_.pack_name + number)));
            b.icon = style_.pack_filter + number;
            b.icon_scale = style_.pack_icon_scale;
        }
    }

    void BuildPicker::add_rows(const std::vector<PickerRow>& rows, BuildPickerRow* heading)
    {
        for (const PickerRow& r : rows)
        {
            auto& view = list_->content().add<BuildPickerRow>("row", style_, r, heading);
            rows_.push_back(&view);
            add_rows(r.children, &view);
        }
    }

    std::optional<std::size_t> BuildPicker::take_picked()
    {
        return std::exchange(picked_out_, std::nullopt);
    }

    bool BuildPicker::on_key(const KeyPress& key)
    {
        if (key.key == Key::Escape && !search_->text.empty())
        {
            search_->text.clear();
            return true;
        }
        return Window::on_key(key);
    }

    bool BuildPicker::shows(const PickerRow& row) const
    {
        if (!row.children.empty())
            return std::ranges::any_of(row.children, [&](const PickerRow& r) { return shows(r); });
        if ((!row.kind.empty() && kinds_off.contains(row.kind)) || (!row.style.empty() && styles_off.contains(row.style)))
            return false;
        if (!packs_only.empty() && !packs_only.contains(row.addon))
            return false;
        return query_.empty() || strings::search_folded(row.name).find(query_) != std::u16string::npos;
    }

    void BuildPicker::lay_out()
    {
        list_->placement.offsets.y1 = note.empty() ? 0 : -kNote;
        laid_note_ = !note.empty();
        // As many columns as fit; a heading takes a tile, and its open items follow it. While searching,
        // headings show their matching items as if open.
        laid_width_ = list_->content().rect().width();
        const int columns = std::max(1, static_cast<int>(laid_width_ / kTile));
        int column = 0;
        float y = 0;
        for (BuildPickerRow* row : rows_)
        {
            bool shown = shows(row->row());
            for (const BuildPickerRow* h = row->heading(); h != nullptr && query_.empty(); h = h->heading())
                shown = shown && h->open;
            row->visible = shown;
            if (!shown)
                continue;
            const float x = static_cast<float>(column) * kTile;
            row->placement = top_left(x, y, kTile, kTile);
            if (++column == columns)
            {
                column = 0;
                y += kTile;
            }
        }
        dirty_ = false;
    }

    void BuildPicker::update(const UpdateContext& context)
    {
        Window::update(context);
        if (list_->content().rect().width() != laid_width_ || note.empty() == laid_note_)
            dirty_ = true;
        if (std::u16string query = strings::search_folded(search_->text); query != query_)
        {
            query_ = std::move(query);
            dirty_ = true;
        }
        for (std::size_t i = 0; i < filters_.size(); ++i)
        {
            Button& b = *filters_[i];
            const FilterKey& key = keys_[i];
            if (key.group == FilterKey::Group::Pack)
            {
                if (b.take_click())
                {
                    if (!packs_only.erase(key.pack))
                        packs_only.insert(key.pack);
                    dirty_ = true;
                }
                b.on = packs_only.contains(key.pack);
                continue;
            }
            const bool object = key.group == FilterKey::Group::Object;
            auto& off = object ? kinds_off : styles_off;
            if (b.take_click())
            {
                if (!off.erase(key.name))
                    off.insert(key.name);
                dirty_ = true;
            }
            b.on = !off.contains(key.name);
            b.icon = (b.on ? (object ? style_.object_filter : style_.style_filter) : (object ? style_.object_filter_off : style_.style_filter_off)) + key.name;
        }
        for (BuildPickerRow* row : rows_)
            row->dear = cash && row->row().cost > *cash;
        for (BuildPickerRow* row : rows_)
        {
            if (!row->take_click())
                continue;
            if (row->is_heading())
            {
                row->open = !row->open;
                dirty_ = true;
                continue;
            }
            for (BuildPickerRow* other : rows_)
                other->picked = other == row;
            picked_ = row->row().item;
            picked_out_ = picked_;
        }
        if (dirty_)
            lay_out();
    }

    void BuildPicker::draw(DrawContext& context) const
    {
        Window::draw(context);
        for (std::size_t i = 0; i < filters_.size(); ++i)
        {
            if (keys_[i].group == FilterKey::Group::Pack && filters_[i]->on)
                context.list.fill(filters_[i]->rect(), style_.pack_on_colour);
        }
        const Font* small = context.fonts.get(FontRole::Small);
        if (small == nullptr)
            return;
        const Rect& s = search_->rect();
        if (search_->text.empty() && !search_->has_keyboard_focus())
            draw_text(context.list, *small, strings_.get(kSearchHint), { s.x0 + 4, s.y0, s.x1 - 4, s.y1 }, { style_.hint_colour, Align::Start, Align::Centre, false });
        if (std::ranges::none_of(rows_, [](const BuildPickerRow* row) { return row->visible; }))
            draw_text(context.list, *small, strings_.get(kNothing), list_->rect(), { style_.text_colour, Align::Centre, Align::Centre, true });
        if (!note.empty())
        {
            const Rect& l = list_->rect();
            draw_text(context.list, *small, note, { l.x0, l.y1, l.x1, l.y1 + kNote }, { style_.text_colour, Align::End, Align::Centre, true });
        }
    }

    BuildPickerRow::BuildPickerRow(std::string name, const BuildPickerStyle& style, const PickerRow& row, BuildPickerRow* heading)
        : Widget(std::move(name))
        , style_(style)
        , row_(row)
        , heading_(heading)
    {
        // Name, then what the list once showed under it: the size or what it starts, and the price.
        tooltip = row_.name;
        for (const std::u16string* line : { &row_.size, &row_.price })
        {
            if (!line->empty())
                tooltip += u'\n' + *line;
        }
    }

    void BuildPickerRow::on_release(Point, bool inside)
    {
        if (inside)
            clicked_ = true;
    }

    void BuildPickerRow::draw(DrawContext& context) const
    {
        const Rect& r = rect();
        if (picked || hovered())
            context.list.fill(r, picked ? style_.picked_colour : style_.hover_colour);
        const float inset = (kTile - kTileIcon) / 2;
        if (const SkinRef* skin = context.skins.find(row_.icon))
            draw_skin(context.list, skin, { r.x0 + inset, r.y0 + inset, r.x0 + inset + kTileIcon, r.y0 + inset + kTileIcon });
        const std::string& corner = is_heading() ? (open ? style_.open_heading : style_.closed_heading) : row_.badge;
        if (const SkinRef* skin = context.skins.find(corner))
            draw_skin(context.list, skin, { r.x1 - kBadge, r.y0, r.x1, r.y0 + kBadge });
        if (const SkinRef* skin = row_.style.empty() ? nullptr : context.skins.find(style_.style_filter + row_.style))
            draw_skin(context.list, skin, { r.x0, r.y0, r.x0 + kBadge, r.y0 + kBadge });
        if (dear)
            context.list.fill(r, style_.dear_colour);
    }

} // namespace rct3::ui
