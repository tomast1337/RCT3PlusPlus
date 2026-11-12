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

#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/spinner.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui::screens
{

    struct LandPanelStyle
    {
        CodedInfoPanel coded;
        std::string tab_face;       ///< Behind each tab's icon
        std::string close_icon;     ///< On the close button
        widgets::SpinnerStyle size; ///< The brush's size at the disc's foot
        /// From the screen's bottom edge to the disc's: the tool row and its open menu stay clear below.
        float bottom = info_panel::kDiscMargin;
        Color text_colour = hex("#ffffff");
        Color chosen = hex("#00000040"); ///< Behind the chosen tool
        Color brush = hex("#56abe5");    ///< The brush's circle on the disc's grey
        Color brush_dot = hex("#1d5f8a");
    };

    namespace land_panel
    {
        inline constexpr float kToolCell = 40; ///< A tool's picture and the room round it
        inline constexpr float kToolIcon = 40; ///< Of the tools' own pictures
        inline constexpr std::size_t kRows = 2;
        inline constexpr float kBodyLeft = 40; ///< From the strip's left edge to the tools, past the ring's tabs
        inline constexpr float kBodyRight = 16;
        inline constexpr float kBodyTop = 28;
        inline constexpr float kStatusGap = 8;
        inline constexpr float kSizeWidth = 44; ///< Of the brush's number
        inline constexpr float kSizeHeight = 16;
        inline constexpr float kNumberGap = 8;   ///< From a tab's tools to its number
        inline constexpr float kSizeBottom = 10; ///< From the disc's foot to the number's
        inline constexpr float kCloseLeft = 9;   ///< From the screen's left edge
        inline constexpr float kCloseBottom = 9;
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
        int slot = 0; ///< On the ring, as footpath_tab_offset counts them
        std::vector<LandTool> tools;
        /// A tab of settings: its tools are a choice of their own, `on` the one in force. A pick there is told
        /// like any other but leaves the tool chosen as it is.
        bool setting = false;
        std::size_t on = 0;
        /// A number after its tools, when `number` names one: its widget's name, its help and its range.
        std::string number { };
        std::u16string number_help { };
        int number_value = 1;
        int number_least = 1;
        int number_most = 1;
    };

    /// A tool chosen: its tab and its place there.
    struct LandPick
    {
        std::size_t tab = 0;
        std::size_t tool = 0;
        friend bool operator==(const LandPick&, const LandPick&) = default;
    };

    class LandPanel : public Widget
    {
    public:
        LandPanel(std::string name, LandPanelStyle style, const StringTable& strings);

        /// Opens the panel with `tabs`, the tab of `chosen` out and that tool marked, and the brush `size` of
        /// at most `largest`.
        void show(std::vector<LandTab> tabs, LandPick chosen, int size, int largest);
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
        [[nodiscard]] std::optional<std::size_t> page() const
        {
            return page_;
        }
        [[nodiscard]] int brush() const
        {
            return size_->value();
        }

        /// The number on the tab named `tab`, as the player left it; that tab's own when it is not out.
        [[nodiscard]] int number(std::size_t tab) const
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
        [[nodiscard]] widgets::Button& tab(std::size_t i) const
        {
            return *tabs_.at(i);
        }
        [[nodiscard]] widgets::Button& tool(std::size_t i) const
        {
            return *tools_.at(i);
        }
        [[nodiscard]] std::size_t tool_count() const
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
        void open(std::optional<std::size_t> page);
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
        std::optional<std::size_t> page_;
        LandPick chosen_;
        std::optional<LandPick> pick_now_;
        int largest_ = 1;
        bool closed_ = false;
    };

} // namespace rct3::ui::screens
