// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/file_dialog.hpp"

#include "ui/save_text.hpp"
#include "ui/skin_library.hpp"
#include "ui/widgets/panel.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kLoadTitle = "LGD_Title";
        constexpr std::string_view kSaveTitle = "SGD_Title";
        constexpr std::string_view kOpenLabel = "FD_Label_Open";
        constexpr std::string_view kSaveLabel = "FD_Label_Save";
        constexpr std::string_view kCancelLabel = "FD_Label_Cancel";
        constexpr std::string_view kDeleteLabel = "FD_Label_Delete";
        constexpr std::string_view kOpenTip = "FD_TT_Open";
        constexpr std::string_view kSaveTip = "FD_TT_Save";
        constexpr std::string_view kCancelTip = "FD_TT_Cancel";
        constexpr std::string_view kNameTip = "FD_TT_FileName";
        constexpr std::string_view kListTip = "FD_TT_FileList";
        constexpr std::string_view kScanning = "FD_Scanning"; ///< Two "%d": the file being read and how many
        constexpr std::string_view kScanComplete = "FD_ScanComplete";
        constexpr std::string_view kNoFiles = "FD_NoFiles";
        constexpr std::string_view kDeleteFile = "FD_DeleteFile"; ///< With %FILENAME%
        constexpr std::string_view kRenameFile = "FD_RenameFile"; ///< With %OLDNAME% and %NEWNAME%
        constexpr std::string_view kRenameFailed = "FD_RenameFailed";
        constexpr std::string_view kReplace = "SGD_Save"; ///< With %FILENAME%
        constexpr std::string_view kNeedName = "SGD_NeedAName";
        constexpr std::string_view kNoDot = "SGD_NameCannotStartWithDot";
        constexpr std::string_view kNoSpace = "SGD_NameCannotStartWithSpace";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr std::string_view kOk = "SMB_Label_OK";
        constexpr std::u16string_view kUnread = u"<Scanning...>"; ///< A row's first line until its file is read
        constexpr std::u16string_view kExtension = u".dat";
        /// What file names may hold besides letters and digits, as in the game.
        constexpr std::u16string_view kNameCharacters = u" _.,'!()[]-+=#;";
        constexpr std::string_view kQuestionName = "file_question";

        std::u16string name_characters()
        {
            std::u16string out;
            for (char16_t c = u'a'; c <= u'z'; ++c)
                out += c;
            for (char16_t c = u'A'; c <= u'Z'; ++c)
                out += c;
            for (char16_t c = u'0'; c <= u'9'; ++c)
                out += c;
            return out + std::u16string(kNameCharacters);
        }

        char16_t lower(char16_t c)
        {
            return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c;
        }

        bool same_name(std::u16string_view a, std::u16string_view b)
        {
            return std::ranges::equal(a, b, [](char16_t x, char16_t y) { return lower(x) == lower(y); });
        }

        bool has_extension(std::u16string_view name)
        {
            return name.size() >= kExtension.size() && same_name(name.substr(name.size() - kExtension.size()), kExtension);
        }

        std::u16string replaced(std::u16string text, std::u16string_view key, std::u16string_view value)
        {
            const std::size_t at = text.find(key);
            if (at != std::u16string::npos)
                text.replace(at, key.size(), value);
            return text;
        }

        std::u16string number(std::size_t n)
        {
            const std::string s = std::to_string(n);
            return std::u16string(s.begin(), s.end());
        }

        Color mix(Color a, Color b, float t)
        {
            const auto m = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t)); };
            return { m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a) };
        }

    } // namespace

    std::u16string shown_file_name(std::u16string_view file)
    {
        return std::u16string(has_extension(file) ? file.substr(0, file.size() - kExtension.size()) : file);
    }

    std::u16string with_save_extension(std::u16string_view name)
    {
        return has_extension(name) ? std::u16string(name) : std::u16string(name) + std::u16string(kExtension);
    }

    /// One file: its picture, its name to rename it by, and three lines about it.
    class FileDialog::Row : public Widget
    {
    public:
        Row(std::string name, const FileDialogStyle& style, std::u16string shown)
            : Widget(std::move(name))
            , style_(style)
        {
            box = &add<EditBox>("name", std::move(shown));
            box->skin = { };
            box->focused_skin = style.name_box_focused;
            box->font = style.font;
            box->max_length = style.rename_length;
            box->padding.y0 = 0; // The box is only as tall as the text
            box->padding.y1 = 0;
            box->allowed = name_characters();
            box->placement = top_left(style.name_box_rect.x0, style.name_box_rect.y0, style.name_box_rect.width(), style.name_box_rect.height());
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                Label& l = add<Label>("line" + std::to_string(i), std::u16string { }, style.font);
                l.style.colour = style.line_colour;
                l.style.vertical = Align::Centre;
                l.placement = top_left(style.lines_x, style.lines_top + static_cast<float>(i) * (style.line_height - 4), style.lines_width, style.line_height - 4);
                labels[i] = &l;
            }
            set_lines({ std::u16string(kUnread), { }, { } });
        }

        EditBox* box = nullptr;
        std::array<Label*, 3> labels { };
        std::array<std::u16string, 3> lines;
        TextureId picture = 0;
        bool selected = false;

        void set_lines(std::array<std::u16string, 3> text)
        {
            lines = std::move(text);
            for (std::size_t i = 0; i < lines.size(); ++i)
                labels[i]->text = lines[i];
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
            if (selected)
                context.list.fill(r, style_.selected_colour);
            const Rect& p = style_.picture;
            const Rect frame { r.x0 + p.x0, r.y0 + p.y0, r.x0 + p.x1, r.y0 + p.y1 };
            draw_skin(context.list, context.skins.find(style_.border), frame);
            if (picture != 0)
            {
                const float i = style_.picture_inset;
                context.list.image({ frame.x0 + i, frame.y0 + i, frame.x1 - i, frame.y1 - i }, picture, { 0, 0, 1, 1 });
            }
        }

    private:
        const FileDialogStyle& style_;
        int clicks_ = 0;
    };

    FileDialog::FileDialog(std::string name, const FileDialogStyle& style, const StringTable& strings, FileDialogMode mode, bool twelve_hour)
        : Window(std::move(name), style.window, style.width + style.window.content_inset.x0 + style.window.content_inset.x1, style.height + style.window.content_inset.y0 + style.window.content_inset.y1)
        , style_(style)
        , strings_(strings)
        , mode_(mode)
        , twelve_hour_(twelve_hour)
    {
        const bool saving = mode == FileDialogMode::Save;
        const Rect& in = style.window.content_inset;
        placement = centred(style.width + in.x0 + in.x1, style.height + in.y0 + in.y1);
        title = text(saving ? kSaveTitle : kLoadTitle);
        icon = saving ? style.save_icon : style.load_icon;

        const float buttons_y = style.height - style.bottom - style.button_size;
        const auto button = [&](std::string id, const std::string& icon_skin, std::size_t slot, std::string_view tip) -> Button& {
            Button& b = content().add<Button>(std::move(id), style.button, icon_skin);
            b.placement = top_left(style.button_x.at(slot), buttons_y, style.button_size, style.button_size);
            b.tooltip = text(tip);
            return b;
        };
        open_ = &button("open", saving ? style.save_icon : style.load_icon, 0, saving ? kSaveTip : kOpenTip);
        cancel_ = &button("cancel", style.cancel_icon, 1, kCancelTip);
        delete_ = &button("delete", style.delete_icon, 2, { });
        default_button = open_;
        cancel_button = cancel_;

        hover_ = &content().add<Label>("hover", std::u16string { }, style.font);
        hover_->style.colour = style.line_colour;
        hover_->style.vertical = Align::Centre;
        hover_->placement = top_left(style.line_x, buttons_y + (style.button_size - style.line_height) / 2, style.button_x[0] - style.line_x, style.line_height);

        const float status_y = buttons_y - style.gap - style.line_height;
        status_ = &content().add<Label>("status", std::u16string { }, style.font);
        status_->style.colour = style.text_colour;
        status_->style.vertical = Align::Centre;
        status_->placement = top_left(style.line_x, status_y, style.list_right - style.line_x, style.line_height);

        float list_bottom = status_y - style.gap;
        if (saving)
        {
            const float box_y = status_y - style.gap - style.line_height;
            name_box_ = &content().add<EditBox>("file_name");
            name_box_->skin = style.name_box;
            name_box_->focused_skin = style.name_box_focused;
            name_box_->font = style.font;
            name_box_->max_length = style.name_length;
            name_box_->allowed = name_characters();
            name_box_->tooltip = text(kNameTip);
            name_box_->placement = top_left(style.list_x, box_y, style.list_right - style.list_x, style.line_height);
            list_bottom = box_y - style.gap;
        }

        auto& border = content().add<Panel>("list_border", style.border);
        border.placement = top_left(style.list_x, style.list_top, style.list_right - style.list_x, list_bottom - style.list_top);
        list_ = &content().add<ScrollArea>("list", style.scroll);
        list_->placement = top_left(style.list_x + 1, style.list_top + 1, style.list_right - style.list_x - 2, list_bottom - style.list_top - 2);
    }

    std::u16string FileDialog::text(std::string_view key) const
    {
        return std::u16string(strings_.get(key));
    }

    void FileDialog::set_files(std::vector<std::u16string> files)
    {
        for (Row* row : rows_)
            list_->content().remove(*row);
        rows_.clear();
        files_ = std::move(files);
        read_.assign(files_.size(), false);
        selected_.reset();
        last_clicked_.reset();
        renaming_.reset();
        for (std::size_t i = 0; i < files_.size(); ++i)
        {
            Row& row = list_->content().add<Row>("row" + std::to_string(i), style_, shown_file_name(files_[i]));
            row.placement = top_left(0, static_cast<float>(i) * style_.row_height, style_.row_width, style_.row_height);
            row.tooltip = text(kListTip);
            row.box->tooltip = text(kNameTip);
            rows_.push_back(&row);
        }
        since_complete_ = files_.empty() ? 0 : -1;
        show_status();
    }

    void FileDialog::set_summary(std::size_t index, const std::optional<save::SaveSummary>& summary, TextureId picture)
    {
        if (index >= rows_.size())
            return;
        Row& row = *rows_[index];
        row.picture = picture;
        row.set_lines(summary ? save_lines(strings_, *summary, twelve_hour_) : std::array<std::u16string, 3> { });
        read_[index] = true;
        if (std::ranges::all_of(read_, [](bool r) { return r; }))
            since_complete_ = 0;
        show_status();
    }

    void FileDialog::show_status()
    {
        if (files_.empty())
        {
            status_->text = text(kNoFiles);
            status_->style.colour = style_.text_colour;
            return;
        }
        if (since_complete_ < 0)
        {
            const auto done = static_cast<std::size_t>(std::ranges::count(read_, true));
            std::u16string s = text(kScanning);
            s = replaced(std::move(s), u"%d", number(std::min(done + 1, files_.size())));
            status_->text = replaced(std::move(s), u"%d", number(files_.size()));
            status_->style.colour = style_.text_colour;
            return;
        }
        if (since_complete_ >= style_.complete_time)
        {
            status_->text.clear();
            return;
        }
        status_->text = text(kScanComplete);
        const float fade = std::clamp(since_complete_ - (style_.complete_time - 1), 0.0f, 1.0f);
        status_->style.colour = mix(style_.text_colour, style_.faded_colour, fade);
    }

    void FileDialog::select(std::size_t index)
    {
        if (index >= rows_.size())
            return;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            rows_[i]->selected = i == index;
        selected_ = index;
        if (name_box_ != nullptr)
            name_box_->text = shown_file_name(files_[index]);
    }

    EditBox& FileDialog::row_name_box(std::size_t index) const
    {
        return *rows_.at(index)->box;
    }

    const std::array<std::u16string, 3>& FileDialog::row_lines(std::size_t index) const
    {
        return rows_.at(index)->lines;
    }

    void FileDialog::click_row(std::size_t index)
    {
        if (index < rows_.size())
            rows_[index]->on_release({ }, true);
    }

    std::optional<std::u16string> FileDialog::take_open()
    {
        // Closed only now: closed during the update, the window would be gone before the owner asks.
        if (open_out_)
            close();
        return std::exchange(open_out_, std::nullopt);
    }

    std::optional<std::u16string> FileDialog::take_delete()
    {
        return std::exchange(delete_out_, std::nullopt);
    }

    std::optional<std::pair<std::u16string, std::u16string>> FileDialog::take_rename()
    {
        return std::exchange(rename_out_, std::nullopt);
    }

    void FileDialog::rename_failed()
    {
        if (renaming_ && *renaming_ < rows_.size())
            rows_[*renaming_]->box->text = shown_file_name(files_[*renaming_]);
        renaming_.reset();
        ask(Asking::Message, text(kRenameFailed), true);
    }

    void FileDialog::ask(Asking asking, std::u16string message, bool only_ok)
    {
        if (question_ != nullptr || parent() == nullptr)
            return;
        std::vector<QuestionAnswer> answers;
        if (only_ok)
            answers.push_back({ text(kOk), style_.question.confirm_icon });
        else
            answers = { { text(kYes), style_.question.confirm_icon }, { text(kNo), style_.question.cancel_icon } };
        question_ = &parent()->add<QuestionBox>(std::string(kQuestionName), style_.question, std::move(message), answers);
        asking_ = asking;
    }

    void FileDialog::open_chosen()
    {
        if (mode_ == FileDialogMode::Load)
        {
            if (selected_)
            {
                open_out_ = files_[*selected_];
            }
            return;
        }
        const std::u16string& name = name_box_->text;
        if (name.empty())
            return ask(Asking::Message, text(kNeedName), true);
        if (name.front() == u'.')
            return ask(Asking::Message, text(kNoDot), true);
        if (name.front() == u' ')
            return ask(Asking::Message, text(kNoSpace), true);
        const std::u16string file = with_save_extension(name);
        const auto existing = std::ranges::find_if(files_, [&](const std::u16string& f) { return same_name(f, file); });
        if (existing != files_.end())
        {
            pending_ = *existing;
            return ask(Asking::Replace, replaced(text(kReplace), u"%FILENAME%", shown_file_name(*existing)), false);
        }
        open_out_ = file;
    }

    void FileDialog::answer(std::size_t given)
    {
        const bool yes = given == 0;
        switch (asking_)
        {
            case Asking::Delete:
                if (yes)
                    delete_out_ = pending_;
                break;
            case Asking::Rename:
                if (yes)
                    rename_out_ = { pending_, pending_new_ };
                else if (renaming_ && *renaming_ < rows_.size())
                    rows_[*renaming_]->box->text = shown_file_name(files_[*renaming_]);
                break;
            case Asking::Replace:
                if (yes)
                    open_out_ = pending_;
                break;
            case Asking::Message:
            case Asking::Nothing:
                break;
        }
        asking_ = Asking::Nothing;
    }

    void FileDialog::update(const UpdateContext& context)
    {
        Window::update(context);
        since_click_ += context.elapsed;
        if (since_complete_ >= 0 && since_complete_ < style_.complete_time)
        {
            since_complete_ += context.elapsed;
            show_status();
        }

        if (question_ != nullptr)
        {
            if (const auto given = question_->take_answer())
            {
                question_ = nullptr; // It closes with the answer
                answer(*given);
            }
            return;
        }

        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            if (!rows_[i]->take_click())
                continue;
            const bool twice = last_clicked_ == i && since_click_ <= style_.double_click;
            select(i);
            last_clicked_ = i;
            since_click_ = 0;
            if (twice)
                open_chosen();
        }
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            EditBox& box = *rows_[i]->box;
            if (!box.take_commit())
                continue;
            const std::u16string old_name = shown_file_name(files_[i]);
            if (box.text == old_name)
                continue;
            renaming_ = i;
            if (box.text.empty())
            {
                rename_failed();
                continue;
            }
            pending_ = files_[i];
            pending_new_ = with_save_extension(box.text);
            ask(Asking::Rename, replaced(replaced(text(kRenameFile), u"%OLDNAME%", old_name), u"%NEWNAME%", box.text), false);
        }

        if (open_->take_click())
            open_chosen();
        if (cancel_->take_click())
            close();
        if (delete_->take_click() && selected_)
        {
            pending_ = files_[*selected_];
            ask(Asking::Delete, replaced(text(kDeleteFile), u"%FILENAME%", shown_file_name(pending_)), false);
        }

        hover_->text.clear();
        if (open_->hovered())
            hover_->text = text(mode_ == FileDialogMode::Save ? kSaveLabel : kOpenLabel);
        else if (cancel_->hovered())
            hover_->text = text(kCancelLabel);
        else if (delete_->hovered())
            hover_->text = text(kDeleteLabel);
    }

} // namespace rct3::ui
