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
        constexpr float kWidth = kMargin * 2 + kButton * 2 + kGap;
        constexpr float kHeight = kMargin * 2 + kButton;
    } // namespace

    DeleteAreaPanel::DeleteAreaPanel(std::string name, const DeleteAreaPanelStyle& style, const StringTable& strings)
        : Widget(std::move(name))
    {
        placement = bottom_left(0, style.bottom, kWidth, kHeight);
        add<Panel>("back", style.back);
        clear_ = &add<Button>(style.clear, style.button, style.clear);
        clear_->tooltip = strings.get("GUI2_DeleteSelection_ClearHelp");
        clear_->placement = top_left(kMargin, kMargin, kButton, kButton);
        delete_ = &add<Button>(style.remove, style.button, style.remove);
        delete_->tooltip = strings.get("GUI2_DeleteSelection_DeleteHelp");
        delete_->placement = top_left(kMargin + kButton + kGap, kMargin, kButton, kButton);
        set_marked(false);
    }

    void DeleteAreaPanel::set_marked(bool marked)
    {
        clear_->enabled = marked;
        delete_->enabled = marked;
    }

    void DeleteAreaPanel::update(const UpdateContext&)
    {
        if (clear_->take_click())
            asked_ = DeleteAreaAction::Clear;
        if (delete_->take_click())
            asked_ = DeleteAreaAction::Delete;
    }

} // namespace rct3::ui
