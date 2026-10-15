// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/build_menus.hpp"

#include "logging/log.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/build_rows.hpp"

#include <string_view>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kListName = "build_list";
        constexpr std::string_view kAllShown = "Rct3pp_AllItemsShown";

    } // namespace

    BuildMenus start_build_menus(const config::GameInstall& install, const save::RequiredContent* content)
    {
        BuildMenus menus;
        menus.items = std::async(std::launch::async, [root = install.root()] { return park::read_build_catalogue(root); }).share();
        if (content != nullptr)
            menus.offer = park::BuildOffer(*content);
        return menus;
    }

    bool open_build_list(BuildMenus& menus, ui::Widget& root, ui::ToolItem item, const ui::Widget& button, const ui::StringTable& strings, const ui::Currency& currency)
    {
        const auto list = ui::build_list(item);
        if (!list || !menus.items.valid())
            return false;
        if (ui::Widget* open = root.find(kListName))
            root.remove(*open);
        auto rows = ui::picker_rows(*list, menus.items.get(), menus.offer, strings, currency);
        auto& picker = root.add<ui::BuildPicker>(std::string(kListName), skins::build_picker_style(), strings, button.tooltip, std::move(rows), menus.large);
        if (!menus.offer.known())
            picker.note = std::u16string(strings.get(kAllShown));
        // To the right of the button, its middle level with the button's; layout keeps it on the screen.
        const ui::Rect& r = root.rect();
        const ui::Rect& b = button.rect();
        const float x = b.x1 + ui::build_picker::kSide - r.x0;
        const float y = (b.y0 + b.y1) / 2 - ui::build_picker::kHeight / 2 - r.y0;
        picker.placement = ui::top_left(x, y, ui::build_picker::kWidth, ui::build_picker::kHeight);
        return true;
    }

    void update_build_menus(BuildMenus& menus, ui::Widget& root)
    {
        auto* picker = static_cast<ui::BuildPicker*>(root.find(kListName));
        if (picker == nullptr)
            return;
        menus.large = picker->large();
        if (const auto picked = picker->take_picked(); picked && menus.items.valid())
        {
            const auto& items = menus.items.get();
            if (*picked < items.size())
                logging::logger("ui").info("picked {} from {}", items[*picked].symbol, items[*picked].overlay);
        }
    }

} // namespace rct3::scene
