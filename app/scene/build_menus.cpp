// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/build_menus.hpp"

#include "logging/log.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/build_rows.hpp"
#include "ui/widgets/button.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

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

    namespace
    {
        /// Widens `into` to take in every shown button under `w`; the groups holding them span the screen.
        void take_in_shown(const ui::Widget& w, ui::Rect& into)
        {
            if (!w.visible)
                return;
            if (w.children().empty())
            {
                const ui::Rect& r = w.rect();
                into = { std::min(into.x0, r.x0), std::min(into.y0, r.y0), std::max(into.x1, r.x1), std::max(into.y1, r.y1) };
            }
            for (const auto& child : w.children())
                take_in_shown(*child, into);
        }
    } // namespace

    bool open_build_list(BuildMenus& menus, ui::Widget& root, ui::ToolItem item, const ui::Widget& button, const ui::Widget& bar, const ui::StringTable& strings, const ui::Palette& palette, const ui::Currency& currency)
    {
        const auto list = ui::build_list(item);
        if (!list || !menus.items.valid())
            return false;
        close_build_list(root);
        auto rows = ui::picker_rows(*list, menus.items.get(), menus.offer, strings, currency);
        const auto* menu_button = dynamic_cast<const ui::Button*>(&button);
        auto& picker = root.add<ui::BuildPicker>(std::string(kListName), skins::build_picker_style(palette), strings, button.tooltip, menu_button != nullptr ? menu_button->icon : std::string { }, std::move(rows), ui::has_theme_filters(*list) ? ui::picker_themes(menus.items.get()) : std::vector<ui::PickerTheme> { });
        picker.kinds_off = menus.kinds_off;
        picker.styles_off = menus.styles_off;
        picker.packs_only = menus.packs_only;
        if (!menus.offer.known())
            picker.note = std::u16string(strings.get(kAllShown));
        // Above the tool bar and its open menu, so it hides none of their buttons, and in the middle of them;
        // layout keeps it on the screen.
        const ui::Rect& r = root.rect();
        ui::Rect shown = button.rect();
        take_in_shown(bar, shown);
        const ui::Point size = menus.size.value_or(ui::Point { ui::build_picker::kWidth, ui::build_picker::kHeight });
        const float x = (shown.x0 + shown.x1) / 2 - size.x / 2 - r.x0;
        const float y = shown.y0 - ui::build_picker::kGap - size.y - r.y0;
        picker.placement = ui::top_left(x, y, size.x, size.y);
        return true;
    }

    void close_build_list(ui::Widget& root)
    {
        if (auto open = root.find(kListName))
            root.remove(*open);
    }

    bool build_list_open(const ui::Widget& root)
    {
        return root.find(kListName).has_value();
    }

    std::optional<park::BuildItem> update_build_menus(BuildMenus& menus, ui::Widget& root, std::optional<std::int64_t> cash)
    {
        if (menus.asked)
            return std::exchange(menus.asked, std::nullopt);
        auto picker = root.find_as<ui::BuildPicker>(kListName);
        if (!picker)
            return std::nullopt;
        picker->cash = cash;
        menus.kinds_off = picker->kinds_off;
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
