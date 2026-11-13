// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/questions.hpp"

#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/scenery_info.hpp"
#include "ui/widgets/question_box.hpp"

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
            names += (names.empty() ? u"" : u", ") + ui::screens::attraction_name(a, strings);
        }
        std::u16string text(strings.get(kDemolishText));
        if (const auto at = text.find(u"%NAME%"); at != std::u16string::npos)
            text.replace(at, 6, names);
        const ui::widgets::QuestionBoxStyle style = skins::question_box_style(palette);
        auto& box = scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kDemolishName), style, std::move(text), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        box.title = std::u16string(strings.get(kDemolishTitle));
        scene.demolish_asked = std::move(refs);
    }

    void ask_sell_pool_complex(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::size_t room)
    {
        if (scene.desktop->root().find(kSellPoolName))
            return;
        const std::string& named = scene.pools.rooms.at(room).complex_name;
        std::u16string text(strings.get(kSellPoolText));
        if (const auto at = text.find(kSellPoolMark); at != std::u16string::npos)
            text.replace(at, kSellPoolMark.size(), named.empty() ? std::u16string(strings.get(kPoolDefaultName)) : strings::to_utf16(named));
        // It names the room asked from as well.
        if (const auto at = text.find(u"%NAME%"); at != std::u16string::npos)
            text.replace(at, 6, ui::screens::scenery_name({ .symbol = scene.pools.rooms.at(room).symbol }, strings));
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

} // namespace rct3::scene::park
