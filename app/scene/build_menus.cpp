// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/build_menus.hpp"

#include "logging/log.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/build_rows.hpp"
#include "ui/widgets/button.hpp"

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
        const auto* menu_button = dynamic_cast<const ui::Button*>(&button);
        auto& picker = root.add<ui::BuildPicker>(std::string(kListName), skins::build_picker_style(), strings, button.tooltip, menu_button != nullptr ? menu_button->icon : std::string { }, std::move(rows));
        picker.styles_off = menus.styles_off;
        picker.packs_only = menus.packs_only;
        if (!menus.offer.known())
            picker.note = std::u16string(strings.get(kAllShown));
        // To the right of the button, its middle level with the button's; layout keeps it on the screen.
        const ui::Rect& r = root.rect();
        const ui::Rect& b = button.rect();
        const float x = b.x1 + ui::build_picker::kGap - r.x0;
        const ui::Point size = menus.size.value_or(ui::Point { ui::build_picker::kWidth, ui::build_picker::kHeight });
        const float y = (b.y0 + b.y1) / 2 - size.y / 2 - r.y0;
        picker.placement = ui::top_left(x, y, size.x, size.y);
        return true;
    }

    bool build_list_open(const ui::Widget& root)
    {
        return root.find(kListName) != nullptr;
    }

    std::optional<park::BuildItem> update_build_menus(BuildMenus& menus, ui::Widget& root, std::optional<std::int64_t> cash)
    {
        auto* picker = static_cast<ui::BuildPicker*>(root.find(kListName));
        if (picker == nullptr)
            return std::nullopt;
        picker->cash = cash;
        menus.styles_off = picker->styles_off;
        menus.packs_only = picker->packs_only;
        const ui::Rect& o = picker->placement.offsets;
        menus.size = ui::Point { o.x1 - o.x0, o.y1 - o.y0 };
        if (const auto picked = picker->take_picked(); picked && menus.items.valid())
        {
            const auto& items = menus.items.get();
            if (*picked < items.size())
            {
                logging::logger("ui").info("picked {} from {}", items[*picked].symbol, items[*picked].overlay);
                return items[*picked];
            }
        }
        return std::nullopt;
    }

} // namespace rct3::scene
