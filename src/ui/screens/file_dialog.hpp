// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that loads and saves games, laid out as the game's: a list of the saves in one folder, three
// rows in sight, each with a picture of the park, its name, date and time, and guests, and the file's name
// in a box that renames it; under the list a line saying how the reading of the files goes; along the bottom
// the round open (or save), cancel and delete buttons, the name of the one under the pointer on the left.
// To save, a box above the list takes the new file's name.
//
// The window knows nothing of files. The owner lists the folder's saves with set_files(), then reads them,
// one a frame, and gives each one's summary and picture with set_summary(); the line under the list counts
// them as they come. What the user asks for comes back as file names, once asked and confirmed:
// take_open() for the file to load or to save to, which closes the window, take_delete() and
// take_rename(). After deleting or renaming the owner lists the folder again, and calls rename_failed()
// when a rename did not happen.
//
// Clicking a row picks it, and puts its name in the save box; clicking it again soon after opens it. Saving
// checks the name first, as the game does: it may not be empty or start with a dot or a space, gets ".dat"
// when it lacks it, and replacing a file in the list asks first. Deleting and renaming ask too.
//
// Files the owner marks as kept (another program's, say) show and open but cannot be deleted, renamed or
// saved over. Saving can be switched off as a whole, when the owner has no way to write the file: the
// window still shows the folder and the name, its save button greyed.
#pragma once

#include "save/summary.hpp"
#include "ui/draw_list.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/question_box.hpp"
#include "ui/widgets/scroll_area.hpp"
#include "ui/widgets/window.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    enum class FileDialogMode : std::uint8_t
    {
        Load,
        Save,
    };

    /// Sizes in units, the content's top-left the origin.
    struct FileDialogStyle
    {
        WindowStyle window;
        QuestionBoxStyle question;
        ScrollBarStyle scroll;
        std::string load_icon; ///< On the window and its open button
        std::string save_icon;
        std::string cancel_icon;
        std::string delete_icon;
        std::string button; ///< Skin family of the round buttons
        std::string border; ///< Around the list and each picture
        std::string name_box;
        std::string name_box_focused;
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Color line_colour = hex("#2484be"); ///< Of a row's lines and the hovered button's name
        Color selected_colour = hex("#efe8ef");
        Color faded_colour = hex("#ffffff"); ///< What the end of the reading fades to

        float width = 410;
        float height = 296;
        float list_x = 6;
        float list_right = 408;
        float list_top = 3;
        float gap = 4; ///< Between the list, the save box, the line and the buttons
        float line_height = 18;
        float line_x = 8;
        float button_size = 30;
        float bottom = 1; ///< Under the buttons
        std::array<float, 3> button_x { 300, 334, 368 };
        float row_width = 385;
        float row_height = 78;
        Rect picture { 8, 7, 88, 71 }; ///< In a row, with its border
        float picture_inset = 3;
        Rect name_box_rect { 91, 9, 371, 23 }; ///< In a row
        float lines_x = 96;
        float lines_width = 200;
        float lines_top = 23;           ///< In a row, each line `line_height - 4` below the last
        std::size_t name_length = 39;   ///< In the save box
        std::size_t rename_length = 80; ///< In a row's box
        float double_click = 0.4f;      ///< Seconds between the clicks of a double click (ours)
        float complete_time = 3;        ///< Seconds the end of the reading shows, the last one fading
    };

    class FileDialog : public Window
    {
    public:
        FileDialog(std::string name, const FileDialogStyle& style, const StringTable& strings, FileDialogMode mode, bool twelve_hour);

        /// The folder's files, by name with their extension, in the order to show. Starts reading again.
        /// `kept[i]` true marks file i as one the window must not delete, rename or save over; missing
        /// entries count as false.
        void set_files(std::vector<std::u16string> files, std::vector<bool> kept = { });
        /// Puts `name` in the save box, without ".dat". Nothing when loading.
        void set_file_name(std::u16string_view name);
        /// Whether saving can happen; when not, the save button is greyed and nothing is saved.
        void set_can_save(bool can);
        [[nodiscard]] bool can_save() const
        {
            return can_save_;
        }
        [[nodiscard]] bool kept(std::size_t index) const
        {
            return index < kept_.size() && kept_[index];
        }
        /// What file `index` holds, read: nothing when it is not a save; `picture` 0 for none. The texture
        /// stays the owner's, used until the files are set again or the window goes.
        void set_summary(std::size_t index, const std::optional<save::SaveSummary>& summary, TextureId picture);

        [[nodiscard]] FileDialogMode mode() const
        {
            return mode_;
        }
        [[nodiscard]] const std::vector<std::u16string>& files() const
        {
            return files_;
        }
        [[nodiscard]] std::optional<std::size_t> selected() const
        {
            return selected_;
        }
        void select(std::size_t index);

        [[nodiscard]] std::optional<std::u16string> take_open();
        [[nodiscard]] std::optional<std::u16string> take_delete();
        [[nodiscard]] std::optional<std::pair<std::u16string, std::u16string>> take_rename();
        /// The last rename did not happen: says so and puts the old name back.
        void rename_failed();

        // For tests and owners that drive the window.
        [[nodiscard]] Button& open_button() const
        {
            return *open_;
        }
        [[nodiscard]] Button& cancel_button_widget() const
        {
            return *cancel_;
        }
        [[nodiscard]] Button& delete_button() const
        {
            return *delete_;
        }
        /// Only when saving.
        [[nodiscard]] EditBox* name_box() const
        {
            return name_box_;
        }
        [[nodiscard]] EditBox& row_name_box(std::size_t index) const;
        [[nodiscard]] const std::array<std::u16string, 3>& row_lines(std::size_t index) const;
        [[nodiscard]] const Label& status() const
        {
            return *status_;
        }
        [[nodiscard]] const Label& hover() const
        {
            return *hover_;
        }
        /// The question or message showing, if any.
        [[nodiscard]] QuestionBox* question() const
        {
            return question_;
        }
        /// Clicks row `index`, as the pointer does.
        void click_row(std::size_t index);

        void update(const UpdateContext& context) override;

    private:
        class Row;
        enum class Asking : std::uint8_t
        {
            Nothing,
            Delete,
            Rename,
            Replace,
            Message,
        };

        [[nodiscard]] std::u16string text(std::string_view key) const;
        void ask(Asking asking, std::u16string text, bool message);
        void open_chosen();
        void answer(std::size_t answer);
        void show_status();

        FileDialogStyle style_;
        const StringTable& strings_;
        FileDialogMode mode_;
        bool twelve_hour_;
        std::vector<std::u16string> files_;
        std::vector<Row*> rows_;
        std::vector<bool> read_;
        std::vector<bool> kept_;
        bool can_save_ = true;
        std::optional<std::size_t> selected_;
        ScrollArea* list_ = nullptr;
        Label* status_ = nullptr;
        Label* hover_ = nullptr;
        Button* open_ = nullptr;
        Button* cancel_ = nullptr;
        Button* delete_ = nullptr;
        EditBox* name_box_ = nullptr;
        QuestionBox* question_ = nullptr;
        Asking asking_ = Asking::Nothing;
        std::u16string pending_;              // The file a question is about
        std::u16string pending_new_;          // A rename's new name
        std::optional<std::size_t> renaming_; // Row whose name a rename changes
        std::optional<std::u16string> open_out_;
        std::optional<std::u16string> delete_out_;
        std::optional<std::pair<std::u16string, std::u16string>> rename_out_;
        float since_complete_ = -1; // Seconds since every file was read; negative while reading
        float since_click_ = 1e9f;
        std::optional<std::size_t> last_clicked_;
    };

    /// The file name without ".dat", as a row shows it.
    [[nodiscard]] std::u16string shown_file_name(std::u16string_view file);
    /// `name` with ".dat" when it lacks it, any case.
    [[nodiscard]] std::u16string with_save_extension(std::u16string_view name);

} // namespace rct3::ui
