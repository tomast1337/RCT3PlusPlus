// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/questions.hpp"

#include "core/types.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/animal_release.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/screens/info/scenery.hpp"
#include "ui/widgets/question_box.hpp"

#include <chrono>
#include <future>
#include <span>
#include <string>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr std::string_view kLeaveTutorialText = "TutorialWindow_ConfirmQuit";
        constexpr std::string_view kLeaveCareerText = "FSM_ConfirmExitToCareerMap_Message";
        constexpr std::string_view kLeaveText = "FSM_ConfirmExit_Message";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
        constexpr std::string_view kDemolishTitle = "SingleItemDeleteEditor_AI_Title";
        constexpr std::string_view kDemolishText = "SingleItemDeleteEditor_AI_Message";
        constexpr std::string_view kSellPoolTitle = "ConfirmSellPoolComplex_Title";
        constexpr std::string_view kSellPoolText = "ConfirmSellPoolComplex_Text";
        constexpr std::u16string_view kSellPoolMark = u"%POOLCOMPLEXNAME%";
        constexpr std::string_view kPoolDefaultName = "PoolComplexDefaultName";
        constexpr std::string_view kRemoveLakeText = "Water_DeleteResponse_WaterfallPrompt";
        constexpr std::string_view kFloodFallsText = "Water_FloodResponse_WaterfallPrompt";
        constexpr std::string_view kReleaseText = "ReleaseAnimalConfirmMessage";
        constexpr std::string_view kReleaseRefused = "ReleaseAnimalFailMessageBoxMessage";
        constexpr std::string_view kReleased = "ReleaseAnimalReleasedMessage";

    } // namespace

    void ask_leave(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette)
    {
        if (scene.desktop->root().find(kQuestionName))
            return;
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        // The question names where leaving goes: out of the tutorial, back to the career screen or to the main menu.
        const std::string_view text = scene.desktop->root().find(kGuideName) ? kLeaveTutorialText : (scene.game.career.empty() ? kLeaveText : kLeaveCareerText);
        scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kQuestionName), style, std::u16string(strings.get(text)), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
    }

    void ask_demolish(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::vector<rct3::park::AttractionRef> refs)
    {
        if (refs.empty() || scene.desktop->root().find(kDemolishName))
            return;
        std::u16string names;
        for (const rct3::park::AttractionRef ref : refs)
        {
            const save::model::AttractionCommon& a = ref.kind == rct3::park::AttractionRef::Kind::Stall ? scene.rides.stalls.at(ref.index).base : scene.rides.rides.at(ref.index).base;
            names += (names.empty() ? u"" : u", ") + ui::screens::info::attraction_name(a, strings);
        }
        std::u16string text(strings.get(kDemolishText));
        if (const auto at = text.find(u"%NAME%"); at != std::u16string::npos)
            text.replace(at, 6, names);
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        auto& box = scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kDemolishName), style, std::move(text), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        box.title = std::u16string(strings.get(kDemolishTitle));
        scene.demolish_asked = std::move(refs);
    }

    void ask_sell_pool_complex(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, usize room)
    {
        if (scene.desktop->root().find(kSellPoolName))
            return;
        const std::string& named = scene.pools.rooms.at(room).complex_name;
        std::u16string text(strings.get(kSellPoolText));
        if (const auto at = text.find(kSellPoolMark); at != std::u16string::npos)
            text.replace(at, kSellPoolMark.size(), named.empty() ? std::u16string(strings.get(kPoolDefaultName)) : strings::to_utf16(named));
        // It names the room asked from as well.
        if (const auto at = text.find(u"%NAME%"); at != std::u16string::npos)
            text.replace(at, 6, ui::screens::info::scenery_name({ .symbol = scene.pools.rooms.at(room).symbol }, strings));
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        auto& box = scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kSellPoolName), style, std::move(text), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        box.title = std::u16string(strings.get(kSellPoolTitle));
        scene.pool_complex_asked = room;
    }

    void ask_remove_lake(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, save::model::Tile tile)
    {
        if (scene.desktop->root().find(kLakeName))
            return;
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kLakeName), style, std::u16string(strings.get(kRemoveLakeText)), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        scene.land.lake_asked = tile;
    }

    void ask_flood_falls(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::vector<save::model::WorldPoint3> tops)
    {
        if (scene.desktop->root().find(kFloodName))
            return;
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kFloodName), style, std::u16string(strings.get(kFloodFallsText)), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        scene.land.falls_asked = std::move(tops);
    }

    void ask_release(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, entt::entity animal)
    {
        const auto* a = scene.sim.registry.valid(animal) ? scene.sim.registry.try_get<simulation::Animal>(animal) : nullptr;
        if (a == nullptr || scene.desktop->root().find(kReleaseName))
            return;
        if (!simulation::may_release(*a))
        {
            scene.prizes.emplace_back(kReleaseRefused);
            return;
        }
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kReleaseName), style, std::u16string(strings.get(kReleaseText)), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        scene.release_asked = animal;
    }

    bool release_animal(ParkScene& scene, entt::entity animal)
    {
        const auto* a = scene.sim.registry.valid(animal) ? scene.sim.registry.try_get<simulation::Animal>(animal) : nullptr;
        if (a == nullptr)
            return false;
        // The rarity of its kind as the catalogue lists the grown animal of its sex: each kind lists its adult male
        // first, then its adult female.
        const bool listed = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        const std::span<const rct3::park::BuildItem> catalogue = listed ? std::span<const rct3::park::BuildItem>(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { };
        f32 rarity = 0;
        usize nth = a->male ? 0 : 1;
        for (const rct3::park::BuildItem& item : catalogue)
            if (item.kind == rct3::park::BuildKind::Animal && strings::equals_ignoring_case(item.symbol, a->species))
            {
                rarity = item.rarity;
                if (nth-- == 0)
                    break;
            }
        if (!simulation::release_animal(scene.sim, animal, rarity, scene.park_model.objectives, scene.park.time))
        {
            scene.prizes.emplace_back(kReleaseRefused);
            return false;
        }
        if (scene.shown_animal == animal)
        {
            scene.shown_animal.reset();
            if (auto info = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName))
                info->hide();
        }
        scene.prizes.emplace_back(kReleased);
        return true;
    }

} // namespace rct3::scene::park
