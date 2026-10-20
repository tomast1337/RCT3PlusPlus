// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/career_dialog.hpp"

#include "strings/unicode.hpp"
#include "ui/skin_library.hpp"
#include "ui/widgets/panel.hpp"

#include <cstdio>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kTitle = "ProfileWindow_Title";
        constexpr std::string_view kSubtitle = "ProfileWindow_SubTitle";
        constexpr std::string_view kLoadLabel = "ProfileWindow_Load_TT";
        constexpr std::string_view kNewLabel = "ProfileWindow_New_TT";
        constexpr std::string_view kDeleteLabel = "ProfileWindow_Delete_TT";
        constexpr std::string_view kCancelLabel = "ProfileWindow_Cancel_TT";
        constexpr std::string_view kConfirmDelete = "ProfileWindow_ConfirmDeleteMessage";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr std::string_view kQuestionName = "career_question";

        std::u16string number(std::size_t n)
        {
            return strings::to_utf16(std::to_string(n));
        }

    } // namespace

    std::u16string career_percent(float percent)
    {
        std::array<char, 32> text { };
        std::snprintf(text.data(), text.size(), "%.3g%%", static_cast<double>(percent));
        return strings::to_utf16(text.data());
    }

    /// One career: the hats and their counts, the picture, the name and the progress.
    class CareerDialog::Row : public Widget
    {
    public:
        Row(std::string name, const CareerDialogStyle& style)
            : Widget(std::move(name))
            , style_(style)
        {
            const float c = style.cell;
            for (std::size_t i = 0; i < counts.size(); ++i)
            {
                Label& l = add<Label>("count" + std::to_string(i), std::u16string { }, style.font);
                l.style.colour = style.text_colour;
                l.style.vertical = Align::Centre;
                l.placement = top_left(c, c * static_cast<float>(i), c, c);
                counts[i] = &l;
            }
            const float text_width = style.row_width - style.text_x;
            box = &add<EditBox>("name");
            box->skin = { };
            box->focused_skin = style.name_box_focused;
            box->font = style.font;
            box->max_length = style.name_length;
            box->padding.y0 = 0;
            box->padding.y1 = 0;
            box->placement = top_left(style.text_x, 0, text_width, c);
            progress = &add<Label>("progress", std::u16string { }, style.font);
            progress->style.colour = style.text_colour;
            progress->style.vertical = Align::Centre;
            progress->placement = top_left(style.text_x + box->padding.x0, 2 * c, text_width, c);
        }

        std::array<Label*, 3> counts { };
        EditBox* box = nullptr;
        Label* progress = nullptr;
        std::u16string known_name; // As the owner last gave it
        TextureId picture = 0;
        std::size_t position = 0;
        bool selected = false;

        void set(CareerRow career)
        {
            known_name = career.name;
            box->text = std::move(career.name);
            picture = career.picture;
            counts[0]->text = number(career.score.tycoon);
            counts[1]->text = number(career.score.entrepreneur);
            counts[2]->text = number(career.score.completed);
            progress->text = career_percent(career.score.percent);
        }

        [[nodiscard]] bool take_click()
        {
            return std::exchange(clicks_, 0) > 0;
        }
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void on_release(Point, bool inside) override
        {
            if (inside)
                ++clicks_;
        }

        void draw(DrawContext& context) const override
        {
            const Rect& r = rect();
            context.list.fill(r, style_.row_colours[position % 2]);
            const float c = style_.cell;
            for (std::size_t i = 0; i < style_.hats.size(); ++i)
            {
                const float y = r.y0 + c * static_cast<float>(i);
                draw_skin(context.list, context.skins.find(style_.hats[i]), { r.x0, y, r.x0 + c, y + c });
            }
            const Rect& p = style_.picture;
            const Rect at { r.x0 + p.x0, r.y0 + p.y0, r.x0 + p.x1, r.y0 + p.y1 };
            if (picture != 0)
                context.list.image(at, picture, { 0, 0, 1, 1 });
            else
                draw_skin(context.list, context.skins.find(style_.no_picture), at);
            draw_skin(context.list, context.skins.find(selected ? style_.row_frame_selected : style_.row_frame), r);
        }

    private:
        const CareerDialogStyle& style_;
        int clicks_ = 0;
    };

    CareerDialog::CareerDialog(std::string name, const CareerDialogStyle& style, const StringTable& strings)
        : Window(std::move(name), style.window, style.width + style.window.content_inset.x0 + style.window.content_inset.x1, style.subtitle_height + style.list_height + style.bar_height + style.window.content_inset.y0 + style.window.content_inset.y1)
        , style_(style)
        , strings_(strings)
    {
        title = text(kTitle);
        icon = style.icon;

        auto& subtitle = content().add<Label>("subtitle", text(kSubtitle), style.font);
        subtitle.style.colour = style.text_colour;
        subtitle.style.vertical = Align::Centre;
        subtitle.placement = top_left(style.subtitle_x, 0, style.width - style.subtitle_x, style.subtitle_height);

        list_ = &content().add<ScrollArea>("list", style.scroll);
        list_->placement = top_left(0, style.subtitle_height, style.width, style.list_height);

        const float bar_y = style.subtitle_height + style.list_height;
        const float buttons_x = style.width - 4 * style.button_size - 3 * style.button_gap;
        hover_ = &content().add<Label>("hover", std::u16string { }, style.font);
        hover_->style.colour = style.hover_colour;
        hover_->style.vertical = Align::Centre;
        hover_->placement = top_left(0, bar_y, buttons_x, style.bar_height);

        const auto button = [&](std::string id, const std::string& icon_skin, std::size_t slot) -> Button& {
            Button& b = content().add<Button>(std::move(id), style.button, icon_skin);
            b.placement = top_left(buttons_x + static_cast<float>(slot) * (style.button_size + style.button_gap), bar_y + (style.bar_height - style.button_size) / 2, style.button_size, style.button_size);
            return b;
        };
        new_ = &button("new", style.new_icon, 0);
        new_->icon_scale = style.big_icon_scale;
        load_ = &button("load", style.load_icon, 1);
        load_->icon_scale = style.big_icon_scale;
        cancel_ = &button("cancel", style.cancel_icon, 2);
        delete_ = &button("delete", style.delete_icon, 3);
        cancel_button = cancel_;
    }

    std::u16string CareerDialog::text(std::string_view key) const
    {
        return std::u16string(strings_.get(key));
    }

    CareerDialog::Row& CareerDialog::make_row(std::size_t index, CareerRow career)
    {
        Row& row = list_->content().add<Row>("row" + std::to_string(made_++), style_);
        row.position = index;
        row.set(std::move(career));
        return row;
    }

    void CareerDialog::place_rows()
    {
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            rows_[i]->position = i;
            rows_[i]->placement = top_left(0, static_cast<float>(i) * style_.row_height, style_.row_width, style_.row_height);
        }
    }

    void CareerDialog::set_careers(std::vector<CareerRow> careers)
    {
        for (Row* row : rows_)
            list_->content().remove(*row);
        rows_.clear();
        for (std::size_t i = 0; i < careers.size(); ++i)
            rows_.push_back(&make_row(i, std::move(careers[i])));
        place_rows();
        last_clicked_.reset();
        select(rows_.empty() ? std::nullopt : std::optional<std::size_t>(0));
    }

    void CareerDialog::set_career(std::size_t index, CareerRow career)
    {
        if (index < rows_.size())
            rows_[index]->set(std::move(career));
    }

    void CareerDialog::add_career(CareerRow career)
    {
        rows_.push_back(&make_row(rows_.size(), std::move(career)));
        place_rows();
        select(rows_.size() - 1);
    }

    void CareerDialog::select(std::optional<std::size_t> index)
    {
        if (index && *index >= rows_.size())
            index.reset();
        selected_ = index;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            rows_[i]->selected = index == i;
    }

    EditBox& CareerDialog::name_box(std::size_t index) const
    {
        return *rows_.at(index)->box;
    }

    std::array<std::u16string, 4> CareerDialog::row_text(std::size_t index) const
    {
        const Row& row = *rows_.at(index);
        return { row.counts[0]->text, row.counts[1]->text, row.counts[2]->text, row.progress->text };
    }

    void CareerDialog::click_row(std::size_t index)
    {
        if (index < rows_.size())
            rows_[index]->on_release({ }, true);
    }

    std::optional<std::size_t> CareerDialog::take_load()
    {
        return std::exchange(load_out_, std::nullopt);
    }

    bool CareerDialog::take_new()
    {
        return std::exchange(new_out_, false);
    }

    std::optional<std::size_t> CareerDialog::take_delete()
    {
        return std::exchange(delete_out_, std::nullopt);
    }

    std::optional<std::pair<std::size_t, std::u16string>> CareerDialog::take_rename()
    {
        return std::exchange(rename_out_, std::nullopt);
    }

    void CareerDialog::check_rename(std::size_t index)
    {
        Row& row = *rows_[index];
        if (row.box->text == row.known_name)
            return;
        if (row.box->text.empty())
        {
            row.box->text = row.known_name; // A career keeps a name
            return;
        }
        row.known_name = row.box->text;
        rename_out_ = { index, row.box->text };
    }

    void CareerDialog::load()
    {
        if (!selected_)
            return;
        check_rename(*selected_);
        load_out_ = selected_;
    }

    void CareerDialog::update(const UpdateContext& context)
    {
        Window::update(context);
        since_click_ += context.elapsed;

        if (question_ != nullptr)
        {
            if (const auto given = question_->take_answer())
            {
                question_ = nullptr; // It closes with the answer
                if (*given == 0 && deleting_ && *deleting_ < rows_.size())
                {
                    const std::size_t index = *deleting_;
                    list_->content().remove(*rows_[index]);
                    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(index));
                    place_rows();
                    select(std::nullopt);
                    last_clicked_.reset();
                    delete_out_ = index;
                }
                deleting_.reset();
            }
            return;
        }

        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            if (rows_[i]->box->take_commit())
                check_rename(i);
            if (!rows_[i]->take_click())
                continue;
            const bool twice = last_clicked_ == i && since_click_ <= style_.double_click;
            select(i);
            last_clicked_ = i;
            since_click_ = 0;
            if (twice)
                load();
        }

        if (new_->take_click())
            new_out_ = true;
        if (load_->take_click())
            load();
        if (cancel_->take_click())
            close();
        if (delete_->take_click() && selected_ && parent())
        {
            deleting_ = selected_;
            const std::vector<QuestionAnswer> answers { { text(kYes), style_.question.confirm_icon }, { text(kNo), style_.question.cancel_icon } };
            question_ = &parent()->add<QuestionBox>(std::string(kQuestionName), style_.question, text(kConfirmDelete), answers);
        }

        hover_->text.clear();
        if (new_->hovered())
            hover_->text = text(kNewLabel);
        else if (load_->hovered())
            hover_->text = text(kLoadLabel);
        else if (cancel_->hovered())
            hover_->text = text(kCancelLabel);
        else if (delete_->hovered())
            hover_->text = text(kDeleteLabel);
    }

} // namespace rct3::ui
