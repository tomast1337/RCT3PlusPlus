// SPDX-License-Identifier: GPL-3.0-or-later
//
// The round panel in the bottom-left corner of the park view that shapes and paints the ground, opened by the
// land menu's tools, standing on the tool row so it hides none of its buttons. It has the path panel's shape: a disc in the corner with tabs on a ring round it, and a
// strip running out to the right of the disc that shows the tools of the tab picked, in two rows.
//
// The owner says which tabs there are with show(): each with its picture, its help, the tools it holds and
// which of them can be used. The tool chosen is marked; one tool is chosen across all the tabs. A tab of
// settings marks its own choice instead and may show a number after its tools. The disc
// shows the brush, its circle growing with the size set in the number at its foot. The band along the strip's
// top tells what the tool under the pointer does, else the tab's help.
//
// The panel keeps no park state; the owner hears through take_pick() and brush() what the player chose.
#pragma once

#include "core/types.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/spinner.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui::screens::build
{

    struct LandPanelStyle
    {
        info::CodedInfoPanel coded;
        std::string tab_face;       ///< Behind each tab's icon
        std::string close_icon;     ///< On the close button
        widgets::SpinnerStyle size; ///< The brush's size at the disc's foot
        /// From the screen's bottom edge to the disc's: the tool row and its open menu stay clear below.
        f32 bottom = info::info_panel::kDiscMargin;
        Color text_colour = hex("#ffffff");
        Color chosen = hex("#00000040"); ///< Behind the chosen tool
        Color brush = hex("#56abe5");    ///< The brush's circle on the disc's grey
        Color brush_dot = hex("#1d5f8a");
    };

    namespace land_panel
    {
        inline constexpr f32 kToolCell = 40; ///< A tool's picture and the room round it
        inline constexpr f32 kToolIcon = 40; ///< Of the tools' own pictures
        inline constexpr usize kRows = 2;
        inline constexpr f32 kBodyLeft = 40; ///< From the strip's left edge to the tools, past the ring's tabs
        inline constexpr f32 kBodyRight = 16;
        inline constexpr f32 kBodyTop = 28;
        inline constexpr f32 kStatusGap = 8;
        inline constexpr f32 kSizeWidth = 44; ///< Of the brush's number
        inline constexpr f32 kSizeHeight = 16;
        inline constexpr f32 kNumberGap = 8;   ///< From a tab's tools to its number
        inline constexpr f32 kSizeBottom = 10; ///< From the disc's foot to the number's
        inline constexpr f32 kCloseLeft = 9;   ///< From the screen's left edge
        inline constexpr f32 kCloseBottom = 9;
    } // namespace land_panel

    struct LandTool
    {
        std::string icon;
        std::u16string help;
        bool usable = true;
    };

    struct LandTab
    {
        std::string icon;
        std::u16string help;
        i32 slot = 0; ///< On the ring, as footpath_tab_offset counts them
        std::vector<LandTool> tools;
        /// A tab of settings: its tools are a choice of their own, `on` the one in force. A pick there is told
        /// like any other but leaves the tool chosen as it is.
        bool setting = false;
        usize on = 0;
        /// A number after its tools, when `number` names one: its widget's name, its help and its range.
        std::string number { };
        std::u16string number_help { };
        i32 number_value = 1;
        i32 number_least = 1;
        i32 number_most = 1;
    };

    /// A tool chosen: its tab and its place there.
    struct LandPick
    {
        usize tab = 0;
        usize tool = 0;
        friend bool operator==(const LandPick&, const LandPick&) = default;
    };

    class LandPanel : public Widget
    {
    public:
        LandPanel(std::string name, LandPanelStyle style, const StringTable& strings);

        /// Opens the panel with `tabs`, the tab of `chosen` out and that tool marked, and the brush `size` of
        /// at most `largest`.
        void show(std::vector<LandTab> tabs, LandPick chosen, i32 size, i32 largest);
        void hide();
        [[nodiscard]] bool showing() const
        {
            return visible;
        }

        [[nodiscard]] LandPick chosen() const
        {
            return chosen_;
        }
        /// The tab out, if any.
        [[nodiscard]] std::optional<usize> page() const
        {
            return page_;
        }
        [[nodiscard]] i32 brush() const
        {
            return size_->value();
        }

        /// The number on the tab named `tab`, as the player left it; that tab's own when it is not out.
        [[nodiscard]] i32 number(usize tab) const
        {
            return data_.at(tab).number_value;
        }

        /// The tool picked since the last call.
        [[nodiscard]] std::optional<LandPick> take_pick()
        {
            return std::exchange(pick_now_, std::nullopt);
        }
        /// True once after the player closed the panel.
        [[nodiscard]] bool take_closed()
        {
            return std::exchange(closed_, false);
        }

        [[nodiscard]] Rect disc() const;
        [[nodiscard]] Rect strip() const;
        [[nodiscard]] widgets::Button& tab(usize i) const
        {
            return *tabs_.at(i);
        }
        [[nodiscard]] widgets::Button& tool(usize i) const
        {
            return *tools_.at(i);
        }
        [[nodiscard]] usize tool_count() const
        {
            return tools_.size();
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool covers(Point p) const override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] Point body_size() const;
        void open(std::optional<usize> page);
        void place();
        void mark();

        LandPanelStyle style_;
        const StringTable& strings_;
        widgets::Button* close_ = nullptr;
        widgets::Spinner* size_ = nullptr;
        widgets::Spinner* number_ = nullptr; ///< Of the tab out, when it has one
        Widget* body_ = nullptr;
        widgets::Label* status_ = nullptr;
        std::vector<widgets::Button*> tabs_;
        std::vector<widgets::Button*> tools_; ///< Of the tab out
        std::vector<LandTab> data_;
        std::optional<usize> page_;
        LandPick chosen_;
        std::optional<LandPick> pick_now_;
        i32 largest_ = 1;
        bool closed_ = false;
    };

} // namespace rct3::ui::screens::build
