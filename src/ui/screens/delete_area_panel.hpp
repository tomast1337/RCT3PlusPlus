// SPDX-License-Identifier: GPL-3.0-or-later
//
// The small panel shown while the area delete tool is out, over the tool row: a button that clears the tiles
// marked so far and one that deletes what stands on them. The owner takes what was asked with take_action().
// The structure tools use it too, with their own pictures and help: picking out a structure (clear the marks,
// save them), and placing one (a lone turn button, as the second is left out when its picture is empty).
#pragma once

#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/panel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace rct3::ui
{

    struct DeleteAreaPanelStyle
    {
        std::string back;   ///< Behind the whole panel
        std::string button; ///< Each button's face
        std::string clear;
        std::string remove;                                        ///< Empty for no second button
        std::string clear_help = "GUI2_DeleteSelection_ClearHelp"; ///< String keys
        std::string remove_help = "GUI2_DeleteSelection_DeleteHelp";
        float bottom = 0; ///< Its bottom edge, up from the screen's
    };

    enum class DeleteAreaAction : std::uint8_t
    {
        Clear,
        Delete,
    };

    class DeleteAreaPanel : public Widget
    {
    public:
        DeleteAreaPanel(std::string name, const DeleteAreaPanelStyle& style, const StringTable& strings);

        /// What a button asked for since the last call.
        [[nodiscard]] std::optional<DeleteAreaAction> take_action()
        {
            return std::exchange(asked_, std::nullopt);
        }
        /// Whether there are tiles marked to clear and delete; the buttons grey out without.
        void set_marked(bool marked);

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void update(const UpdateContext& context) override;

    private:
        Button* clear_ = nullptr;
        Button* delete_ = nullptr;
        std::optional<DeleteAreaAction> asked_;
    };

} // namespace rct3::ui
