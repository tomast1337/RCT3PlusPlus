// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/delete_area_panel.hpp"

#include "ui/placement.hpp"

namespace rct3::ui
{

    namespace
    {
        // In units of the desktop.
        constexpr float kMargin = 10;
        constexpr float kButton = 32;
        constexpr float kGap = 8;
        constexpr float kHeight = kMargin * 2 + kButton;
    } // namespace

    DeleteAreaPanel::DeleteAreaPanel(std::string name, const DeleteAreaPanelStyle& style, const StringTable& strings)
        : Widget(std::move(name))
    {
        const float buttons = style.remove.empty() ? 1 : 2;
        placement = bottom_left(0, style.bottom, kMargin * 2 + kButton * buttons + kGap * (buttons - 1), kHeight);
        add<Panel>("back", style.back);
        clear_ = &add<Button>(style.clear, style.button, style.clear);
        clear_->tooltip = strings.get(style.clear_help);
        clear_->placement = top_left(kMargin, kMargin, kButton, kButton);
        if (!style.remove.empty())
        {
            delete_ = &add<Button>(style.remove, style.button, style.remove);
            delete_->tooltip = strings.get(style.remove_help);
            delete_->placement = top_left(kMargin + kButton + kGap, kMargin, kButton, kButton);
        }
        set_marked(false);
    }

    void DeleteAreaPanel::set_marked(bool marked)
    {
        clear_->enabled = marked;
        if (delete_)
            delete_->enabled = marked;
    }

    void DeleteAreaPanel::update(const UpdateContext&)
    {
        if (clear_->take_click())
            asked_ = DeleteAreaAction::Clear;
        if (delete_ && delete_->take_click())
            asked_ = DeleteAreaAction::Delete;
    }

} // namespace rct3::ui
